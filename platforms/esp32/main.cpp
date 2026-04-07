#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "epd.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "hal/usb_serial_jtag_ll.h"
#include "input.h"
#include "microreader/Application.h"
#include "microreader/HeapLog.h"
#include "microreader/Loop.h"
#include "microreader/content/Book.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/display/DisplayQueue.h"
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

// Toggled by the menu; controls whether the serial LUT editor
// overrides the fast (active) LUT or the settle LUT.
bool g_lut_target_settle = false;  // kept for MenuDemo extern

extern "C" void app_main(void) {
  verify_ota();

  static Esp32InputSource input;
  static EInkDisplay epd;
  static Esp32Runtime runtime(50);
  static microreader::Application app;
  static microreader::DisplayQueue queue(epd);

  // Only wait for serial monitor if USB is connected.
  if (usb_serial_jtag_ll_txfifo_writable()) {
    vTaskDelay(pdMS_TO_TICKS(3000));
  }

  // --- Memory audit: log heap at every stage ---
  ESP_LOGI("mem", "after static init (DisplayQueue+App etc): free=%lu largest=%lu",
           (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  MR_LOGI("app", "Booting up...");

  epd.begin();

  ESP_LOGI("mem", "after epd.begin: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // Mount SD card (shares SPI bus with display).
  if (sd_init()) {
    MR_LOGI("app", "SD card ready");

    ESP_LOGI("mem", "after sd_init: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Ensure books directory exists.
    mkdir("/sdcard/books", 0775);

    // Register the books directory for the selection screen.
    app.set_books_dir("/sdcard/books");
  } else {
    MR_LOGI("app", "SD card not available");
  }

  serial_start();

  ESP_LOGI("mem", "after serial_start: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  static uint8_t lut_buf[kLutSize];

  app.start(queue);

  ESP_LOGI("mem", "after app.start: free=%lu largest=%lu", (unsigned long)esp_get_free_heap_size(),
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  // Discard the power-button press that woke us from deep sleep.
  input.clear_button(microreader::Button::Power);

  while (runtime.should_continue() && app.running()) {
    if (serial_lut_take(lut_buf)) {
      if (g_lut_target_settle)
        epd.setCustomSettleLUT(lut_buf);
      else
        epd.setCustomLUT(lut_buf);
    }

    // Dispatch serial path commands (open book, benchmarks).
    {
      const char* cmd_path = nullptr;
      switch (serial_cmd_take(&cmd_path)) {
        case SerialCmdType::Open:
          app.auto_open_book(cmd_path, queue);
          break;
        case SerialCmdType::Bench: {
          microreader::Book book;
          book.open(cmd_path);
          uint8_t* work_buf = queue.scratch_buf1();
          uint8_t* xml_buf = queue.scratch_buf2();
          microreader::benchmark_epub_conversion(book, "/sdcard/bench_tmp.mrb", work_buf, xml_buf);
          queue.reset_buffers();
          break;
        }
        case SerialCmdType::ImgBench: {
          microreader::Book book;
          book.open(cmd_path);
          microreader::benchmark_image_size_read(book, queue.scratch_buf1());
          queue.reset_buffers();
          break;
        }
        default:
          break;
      }
    }

    microreader::run_loop_iteration(app, queue, input, runtime);
  }

  MR_LOGI("app", "Shutting down, entering deep sleep...");

  // Enter deep sleep; wake on power button press (active LOW, GPIO 3).
  constexpr gpio_num_t kPowerPin = GPIO_NUM_3;
  gpio_set_direction(kPowerPin, GPIO_MODE_INPUT);
  gpio_pullup_en(kPowerPin);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << kPowerPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}
