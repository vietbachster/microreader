#include <cstdio>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "epd.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "font_partition.h"
#include "input.h"
#include "microreader/Application.h"
#include "microreader/HeapLog.h"
#include "microreader/Loop.h"
#include "microreader/content/BitmapFont.h"
#include "microreader/content/Book.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/display/DrawBuffer.h"
#include "runtime.h"
#include "sdcard.h"
#include "serial_communication.h"

static void verify_ota() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }
}

// When the device boots on battery (no USB), require the power button to be
// held for at least kPowerWakeupMs milliseconds before allowing boot.
// A brief accidental touch goes back to sleep immediately without any display
// activity, just like the original microreader firmware.
// Exception: software resets (e.g. after esptool flash) boot immediately.
static constexpr gpio_num_t kPowerPin = GPIO_NUM_3;
static constexpr uint32_t kPowerWakeupMs = 250;

static void verify_wakeup_press() {
#ifndef QEMU_BUILD
  // A software reset means we just got flashed or restarted by a tool —
  // boot immediately without requiring a button hold.
  if (esp_reset_reason() == ESP_RST_SW)
    return;

  gpio_config_t cfg{};
  cfg.pin_bit_mask = (1ULL << kPowerPin);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);

  // Wait up to 2× the threshold; if the button isn't held long enough, sleep.
  const uint32_t deadline_ms = kPowerWakeupMs * 2;
  uint32_t held_ms = 0;
  for (uint32_t elapsed = 0; elapsed < deadline_ms; elapsed += 10) {
    vTaskDelay(pdMS_TO_TICKS(10));
    if (gpio_get_level(kPowerPin) == 0) {
      held_ms += 10;
      if (held_ms >= kPowerWakeupMs)
        return;  // confirmed long press — boot normally
    } else {
      held_ms = 0;  // button released, reset counter
    }
  }

  // Short press — go back to sleep without waking up.
  ESP_LOGI("pwr", "Short press on wakeup (held %lu ms) — returning to sleep", (unsigned long)held_ms);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << kPowerPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
#endif
}

extern "C" void app_main(void) {
  verify_ota();
  verify_wakeup_press();

  static Esp32InputSource input;
  static EInkDisplay epd;
  static Esp32Runtime runtime(50);
  static microreader::Application app;
  static microreader::DrawBuffer buf(epd);

#ifndef QEMU_BUILD
  // After a software reset (post-flash) wait briefly for the serial monitor.
  if (esp_reset_reason() == ESP_RST_SW) {
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
#endif

  // --- Memory audit: log heap at every stage ---
  ESP_LOGI("mem", "after static init (DrawBuffer+App etc): free=%lu largest=%lu",
           (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  MR_LOGI("app", "Booting up...");

#ifndef QEMU_BUILD
  epd.begin();

  ESP_LOGI("mem", "after epd.begin: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#else
  ESP_LOGI("app", "QEMU build: skipping epd.begin()");
#endif

  // Mount SD card (shares SPI bus with display).
  if (sd_init()) {
    MR_LOGI("app", "SD card ready");

    ESP_LOGI("mem", "after sd_init: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Ensure books directory exists.
    mkdir("/sdcard/books", 0775);

    // Data directory for converted books, settings, reading state.
    mkdir("/sdcard/.microreader", 0775);
    mkdir("/sdcard/.microreader/cache", 0775);
    mkdir("/sdcard/.microreader/data", 0775);

    // Register the books directory for the selection screen.
    app.set_books_dir("/sdcard/books");
    app.set_data_dir("/sdcard/.microreader");
  } else {
    MR_LOGI("app", "SD card not available");
  }

  serial_start();

  ESP_LOGI("mem", "after serial_start: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // Try to mmap fonts from the spiffs partition (zero-RAM, XIP access).
  // Expects a v1 FNTS bundle containing up to kFontSizeCount sizes
  // (Small/Normal/Large/XLarge/XXLarge) with an embedded font name.
  static FontPartition font_part;
  static microreader::BitmapFont prop_fonts[microreader::kFontSizeCount];
  static microreader::BitmapFontSet font_set;

  auto load_fonts = [&]() {
    for (auto& f : prop_fonts)
      f = microreader::BitmapFont();
    font_set = microreader::BitmapFontSet();

    const uint8_t* d = font_part.data;
    size_t sz = font_part.size;

    if (sz < 40 || memcmp(d, "FNTS", 4) != 0 || d[5] < 1) {
      ESP_LOGE("font", "Invalid font partition (expected FNTS v1 bundle)");
      return;
    }

    // FNTS v1: [FNTS:4][num:1][version:1][res:2][name:32][num×size:4][data...]
    uint8_t num = d[4];
    if (num > microreader::kFontSizeCount)
      num = microreader::kFontSizeCount;

    char font_name[33] = {};
    memcpy(font_name, d + 8, 32);
    font_name[32] = '\0';
    ESP_LOGI("font", "Bundle font: \"%s\" (v%u, %u sizes)", font_name, d[5], num);

    constexpr size_t kSizeTableOff = 8 + 32;
    uint32_t sizes[microreader::kFontSizeCount] = {};
    for (int i = 0; i < num; i++) {
      const uint8_t* p = d + kSizeTableOff + i * 4;
      sizes[i] = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }
    size_t off = kSizeTableOff + static_cast<size_t>(num) * 4;
    for (int i = 0; i < num; i++) {
      if (off + sizes[i] > sz)
        break;
      prop_fonts[i].init(d + off, sizes[i]);
      off += sizes[i];
    }

    static constexpr microreader::FontSize kAllSizes[] = {microreader::FontSize::Small, microreader::FontSize::Normal,
                                                          microreader::FontSize::Large, microreader::FontSize::XLarge,
                                                          microreader::FontSize::XXLarge};
    for (int i = 0; i < microreader::kFontSizeCount; i++)
      font_set.set(kAllSizes[i], &prop_fonts[i]);
  };

  if (font_part.mmap()) {
    load_fonts();
    if (font_set.valid()) {
      static const char* names[] = {"Small", "Normal", "Large", "XLarge", "XXLarge"};
      for (int i = 0; i < microreader::kFontSizeCount; i++) {
        if (prop_fonts[i].valid())
          ESP_LOGI("font", "%s: %u glyphs, height=%u baseline=%u", names[i], (unsigned)prop_fonts[i].num_glyphs(),
                   (unsigned)prop_fonts[i].glyph_height(), (unsigned)prop_fonts[i].baseline());
      }
      app.set_reader_font(&font_set);
    } else {
      ESP_LOGW("font", "no valid Normal font found");
    }
  }

  ESP_LOGI("mem", "after font mmap: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  app.start(buf);

  ESP_LOGI("mem", "after app.start: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // Discard the power-button press that woke us from deep sleep.
  input.clear_button(microreader::Button::Power);

  while (runtime.should_continue() && app.running()) {
    // Check if a new font was uploaded via serial.
    if (g_font_uploaded) {
      g_font_uploaded = false;
      if (font_part.mmap()) {
        load_fonts();
        if (font_set.valid()) {
          ESP_LOGI("font", "re-loaded fonts after upload");
          app.set_reader_font(&font_set);
        }
      }
    }

    // Check if a new grayscale LUT was uploaded via serial.
    uint8_t lut_buf[112];
    uint8_t lut_type = 0;
    if (serial_lut_take(lut_buf, &lut_type)) {
      switch (lut_type) {
        case 0:
          epd.set_grayscale_lut(lut_buf);
          ESP_LOGI("epd", "Custom grayscale LUT set via serial (type=0)");
          break;
        case 1:
          epd.set_grayscale_revert_lut(lut_buf);
          ESP_LOGI("epd", "Custom grayscale REVERT LUT set via serial (type=1)");
          break;
        default:
          ESP_LOGI("epd", "Received LUT with unknown type %u", lut_type);
          break;
      }
    }

    // Dispatch serial path commands (open book, benchmarks).
    {
      const char* cmd_path = nullptr;
      switch (serial_cmd_take(&cmd_path)) {
        case SerialCmdType::Open:
          app.auto_open_book(cmd_path, buf);
          break;
        case SerialCmdType::Bench: {
          microreader::Book book;
          uint8_t* work_buf = buf.scratch_buf1();
          uint8_t* xml_buf = buf.scratch_buf2();
          int64_t t_open = esp_timer_get_time();
          book.open(cmd_path, work_buf, xml_buf);
          long open_ms = (long)((esp_timer_get_time() - t_open) / 1000);
          microreader::benchmark_epub_conversion(book, "/sdcard/bench_tmp.mrb", open_ms, work_buf, xml_buf);
          buf.reset_after_scratch();
          break;
        }
        case SerialCmdType::ImgBench: {
          microreader::Book book;
          book.open(cmd_path);
          microreader::benchmark_image_size_read(book, buf.scratch_buf1());
          buf.reset_after_scratch();
          break;
        }
        case SerialCmdType::ImgDecode: {
          microreader::Book book;
          book.open(cmd_path);
          microreader::benchmark_image_decode(book, buf.scratch_buf1());
          buf.reset_after_scratch();
          break;
        }
        default:
          break;
      }
    }

    microreader::run_loop_iteration(app, buf, input, runtime);
  }

  MR_LOGI("app", "Shutting down, entering deep sleep...");

#ifndef QEMU_BUILD
  // Enter deep sleep; wake on power button press (active LOW, GPIO 3).
  constexpr gpio_num_t kPowerPin = GPIO_NUM_3;
  gpio_set_direction(kPowerPin, GPIO_MODE_INPUT);
  gpio_pullup_en(kPowerPin);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << kPowerPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
#endif  // QEMU_BUILD
}
