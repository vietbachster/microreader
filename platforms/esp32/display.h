#pragma once

#include <cstdlib>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microreader/Display.h"

// ---- Pin assignments ----
static constexpr gpio_num_t kEpdSclk = GPIO_NUM_8;
static constexpr gpio_num_t kEpdMosi = GPIO_NUM_10;
static constexpr gpio_num_t kEpdCs = GPIO_NUM_21;
static constexpr gpio_num_t kEpdDc = GPIO_NUM_4;
static constexpr gpio_num_t kEpdRst = GPIO_NUM_5;
static constexpr gpio_num_t kEpdBusy = GPIO_NUM_6;

// ---- SSD1677 commands ----
static constexpr uint8_t kCmdSoftReset = 0x12;
static constexpr uint8_t kCmdBoosterSoftStart = 0x0C;
static constexpr uint8_t kCmdDriverOutputCtrl = 0x01;
static constexpr uint8_t kCmdBorderWaveform = 0x3C;
static constexpr uint8_t kCmdTempSensorCtrl = 0x18;
static constexpr uint8_t kCmdWriteTemp = 0x1A;
static constexpr uint8_t kCmdDataEntryMode = 0x11;
static constexpr uint8_t kCmdSetRamXRange = 0x44;
static constexpr uint8_t kCmdSetRamYRange = 0x45;
static constexpr uint8_t kCmdSetRamXCounter = 0x4E;
static constexpr uint8_t kCmdSetRamYCounter = 0x4F;
static constexpr uint8_t kCmdWriteRamBw = 0x24;
static constexpr uint8_t kCmdWriteRamRed = 0x26;
static constexpr uint8_t kCmdAutoWriteBwRam = 0x46;
static constexpr uint8_t kCmdAutoWriteRedRam = 0x47;
static constexpr uint8_t kCmdDisplayUpdateCtrl1 = 0x21;
static constexpr uint8_t kCmdDisplayUpdateCtrl2 = 0x22;
static constexpr uint8_t kCmdMasterActivation = 0x20;
static constexpr uint8_t kCmdWriteLut = 0x32;
static constexpr uint8_t kCmdGateVoltage = 0x03;
static constexpr uint8_t kCmdSourceVoltage = 0x04;
static constexpr uint8_t kCmdWriteVcom = 0x2C;
static constexpr uint8_t kCmdDeepSleep = 0x10;

// CTRL1 modes (CMD_DISPLAY_UPDATE_CTRL1)
static constexpr uint8_t kCtrl1Normal = 0x00;     // compare RED vs BW (fast/partial)
static constexpr uint8_t kCtrl1BypassRed = 0x40;  // ignore RED RAM (full refresh)

// Grayscale LUT for fast B&W refresh (from reference lut_grayscale)
static const uint8_t kLutGrayscale[] = {
    // VS waveforms (5 x 10 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 00 b/w
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 01 light gray
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 10 gray
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 11 dark gray
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // VCOM
    // TP/RP timing groups G0-G9 (10 x 5 bytes)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G0
    0x01, 0x01, 0x01, 0x01, 0x00,  // G1
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2
    0x00, 0x00, 0x00, 0x00, 0x00,  // G3
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9
    // Frame rate (5 bytes)
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,
    // Voltages: VGH, VSH1, VSH2, VSL, VCOM  [105..109]
    0x17, 0x41, 0xA8, 0x32, 0x30,
    // Reserved
    0x00, 0x00};

class Esp32Display final : public microreader::IDisplay {
 public:
  // Public frame pixel buffer - owned here, used via DisplayFrame view in main.
  alignas(4) uint8_t frame_pixels[microreader::DisplayFrame::kPixelBytes]{};

  Esp32Display() {
    // Previous frame initialised to white (matches display RAM after init).
    memset(prev_pixels_, 0xFF, sizeof(prev_pixels_));
  }

  void set_rotation(microreader::Rotation r) override {
    rotation_ = r;
  }
  microreader::Rotation rotation() const override {
    return rotation_;
  }

  // Mirrors reference displayBuffer() -----------------------------------------
  void present(const microreader::DisplayFrame& frame, microreader::RefreshMode mode) override {
    ESP_LOGI("display", "present: mode=%s", mode == microreader::RefreshMode::Full ? "full" : "fast");

    if (!initialized_) {
      init();
      initialized_ = true;
    }

    // Reference forces HALF_REFRESH when screen is off (cold start power-on).
    const bool cold_start = !screen_on_;

    set_ram_area(0, 0, microreader::DisplayFrame::kPhysicalWidth, microreader::DisplayFrame::kPhysicalHeight);

    if (cold_start || mode == microreader::RefreshMode::Full) {
      // Full / cold-start: write new frame to both BW and RED RAM.
      write_ram(kCmdWriteRamBw, frame.pixels, microreader::DisplayFrame::kPixelBytes);
      write_ram(kCmdWriteRamRed, frame.pixels, microreader::DisplayFrame::kPixelBytes);
      memcpy(prev_pixels_, frame.pixels, microreader::DisplayFrame::kPixelBytes);
      refresh_display(false, cold_start);  // half=true on first call (WRITE_TEMP + 0xD4)
    } else {
      // Fast: BW = new frame, RED = previous frame so display compares them.
      write_ram(kCmdWriteRamBw, frame.pixels, microreader::DisplayFrame::kPixelBytes);
      write_ram(kCmdWriteRamRed, prev_pixels_, microreader::DisplayFrame::kPixelBytes);
      memcpy(prev_pixels_, frame.pixels, microreader::DisplayFrame::kPixelBytes);
      refresh_display(true, false);
    }
  }

 private:
  microreader::Rotation rotation_ = microreader::Rotation::Deg0;
  bool initialized_ = false;
  bool screen_on_ = false;
  bool custom_lut_active_ = false;
  spi_device_handle_t spi_{};
  uint8_t prev_pixels_[microreader::DisplayFrame::kPixelBytes];

  // ---- Timing ----

  static uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
  }
  static void delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
  }

  // ---- Low-level SPI (manual CS + DC, mirrors reference) ----

  void send_command(uint8_t c) {
    gpio_set_level(kEpdDc, 0);
    gpio_set_level(kEpdCs, 0);
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = c;
    spi_device_polling_transmit(spi_, &t);
    gpio_set_level(kEpdCs, 1);
  }

  void send_data(uint8_t d) {
    gpio_set_level(kEpdDc, 1);
    gpio_set_level(kEpdCs, 0);
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = d;
    spi_device_polling_transmit(spi_, &t);
    gpio_set_level(kEpdCs, 1);
  }

  // Bulk send - split into DMA-safe <=4092-byte chunks.
  void send_data_buf(const uint8_t* buf, size_t len) {
    static constexpr size_t kChunk = 4092;
    gpio_set_level(kEpdDc, 1);
    size_t offset = 0;
    while (offset < len) {
      const size_t chunk = (len - offset < kChunk) ? (len - offset) : kChunk;
      gpio_set_level(kEpdCs, 0);
      spi_transaction_t t{};
      t.length = chunk * 8;
      t.tx_buffer = buf + offset;
      spi_device_polling_transmit(spi_, &t);
      gpio_set_level(kEpdCs, 1);
      offset += chunk;
    }
  }

  void wait_busy() {
    const uint32_t start = now_ms();
    while (gpio_get_level(kEpdBusy) == 1) {
      delay_ms(1);
      if (now_ms() - start > 10000)
        break;
    }
  }

  // ---- Initialisation (mirrors reference initDisplayController) ----

  void init() {
    gpio_config_t out_cfg{};
    out_cfg.pin_bit_mask = (1ULL << kEpdCs) | (1ULL << kEpdDc) | (1ULL << kEpdRst);
    out_cfg.mode = GPIO_MODE_OUTPUT;
    out_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    out_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&out_cfg);

    gpio_config_t in_cfg{};
    in_cfg.pin_bit_mask = (1ULL << kEpdBusy);
    in_cfg.mode = GPIO_MODE_INPUT;
    in_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    in_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    in_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&in_cfg);

    gpio_set_level(kEpdCs, 1);
    gpio_set_level(kEpdDc, 1);

    spi_bus_config_t bus{};
    bus.mosi_io_num = kEpdMosi;
    bus.miso_io_num = -1;
    bus.sclk_io_num = kEpdSclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = 40 * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    spi_bus_add_device(SPI2_HOST, &dev, &spi_);

    // Hardware reset
    gpio_set_level(kEpdRst, 1);
    delay_ms(20);
    gpio_set_level(kEpdRst, 0);
    delay_ms(2);
    gpio_set_level(kEpdRst, 1);
    delay_ms(20);

    // Soft reset
    send_command(kCmdSoftReset);
    wait_busy();

    // Internal temperature sensor
    send_command(kCmdTempSensorCtrl);
    send_data(0x80);

    // Booster soft-start (GDEQ0426T82 specific)
    send_command(kCmdBoosterSoftStart);
    send_data(0xAE);
    send_data(0xC7);
    send_data(0xC3);
    send_data(0xC0);
    send_data(0x40);

    // Driver output: 480 gates, interlaced scan
    send_command(kCmdDriverOutputCtrl);
    send_data((480 - 1) % 256);
    send_data((480 - 1) / 256);
    send_data(0x02);

    // Border waveform
    send_command(kCmdBorderWaveform);
    send_data(0x01);

    // Clear both RAM buffers to white
    set_ram_area(0, 0, microreader::DisplayFrame::kPhysicalWidth, microreader::DisplayFrame::kPhysicalHeight);
    send_command(kCmdAutoWriteBwRam);
    send_data(0xF7);
    wait_busy();
    send_command(kCmdAutoWriteRedRam);
    send_data(0xF7);
    wait_busy();
  }

  // ---- RAM area setup (mirrors reference setRamArea) ----

  void set_ram_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    y = microreader::DisplayFrame::kPhysicalHeight - y - h;  // gates reversed

    send_command(kCmdDataEntryMode);
    send_data(0x01);  // X inc, Y dec

    send_command(kCmdSetRamXRange);
    send_data(x % 256);
    send_data(x / 256);
    send_data((x + w - 1) % 256);
    send_data((x + w - 1) / 256);

    send_command(kCmdSetRamYRange);
    send_data((y + h - 1) % 256);
    send_data((y + h - 1) / 256);
    send_data(y % 256);
    send_data(y / 256);

    send_command(kCmdSetRamXCounter);
    send_data(x % 256);
    send_data(x / 256);

    send_command(kCmdSetRamYCounter);
    send_data((y + h - 1) % 256);
    send_data((y + h - 1) / 256);
  }

  void write_ram(uint8_t ram_cmd, const uint8_t* data, size_t len) {
    send_command(ram_cmd);
    send_data_buf(data, len);
  }

  // ---- LUT loader (mirrors reference setCustomLUT) ----

  void load_lut(const uint8_t* lut) {
    send_command(kCmdWriteLut);
    for (uint16_t i = 0; i < 105; i++)
      send_data(lut[i]);
    send_command(kCmdGateVoltage);
    send_data(lut[105]);
    send_command(kCmdSourceVoltage);
    send_data(lut[106]);
    send_data(lut[107]);
    send_data(lut[108]);
    send_command(kCmdWriteVcom);
    send_data(lut[109]);
    custom_lut_active_ = true;
  }

  void clear_lut() {
    custom_lut_active_ = false;
  }

  // ---- Refresh sequence (mirrors reference refreshDisplay exactly) ----
  //
  // half=true  -> HALF_REFRESH  : CTRL1=0x40, WRITE_TEMP 0x5A, mode bits 0xD4  (cold start power-on)
  // fast=false -> FULL_REFRESH  : CTRL1=0x40 (bypass RED),     mode bits 0x34
  // fast=true  -> FAST_REFRESH  : CTRL1=0x00 (compare),        mode bits 0x1C / 0x0C

  void refresh_display(bool fast, bool half = false) {
    send_command(kCmdDisplayUpdateCtrl1);
    send_data(fast ? kCtrl1Normal : kCtrl1BypassRed);

    uint8_t display_mode = 0x00;

    if (!screen_on_) {
      screen_on_ = true;
      display_mode |= 0xC0;  // CLOCK_ON | ANALOG_ON
    }

    if (half) {
      send_command(kCmdWriteTemp);
      send_data(0x5A);
      display_mode |= 0xD4;
    } else if (!fast) {
      display_mode |= 0x34;
    } else {
      display_mode |= custom_lut_active_ ? 0x0C : 0x1C;
    }

    ESP_LOGI("display", "refresh_display: half=%d fast=%d mode=0x%02X", half, fast, display_mode);

    send_command(kCmdDisplayUpdateCtrl2);
    send_data(display_mode);
    send_command(kCmdMasterActivation);
    wait_busy();
  }
};
