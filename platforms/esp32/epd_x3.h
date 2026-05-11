#pragma once

// EInkDisplay driver for ESP-IDF (UC8276, Xteink X3).
// Ported from papyrix-reader-main with 5 LUT sets, X3LutSet caching,
// Y-mirrored frame writes, bit-inversion for full-sync, and correct
// UC8276 deep-sleep sequence (CMD_POWER_OFF → CMD_DEEP_SLEEP + 0xA5).

#include <cstdint>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microreader/display/DrawBuffer.h"

// ---- Pin assignments (shared with X4) ----
#define EPD_SCLK GPIO_NUM_8
#define EPD_MOSI GPIO_NUM_10
#define EPD_CS   GPIO_NUM_21
#define EPD_DC   GPIO_NUM_4
#define EPD_RST  GPIO_NUM_5
#define EPD_BUSY GPIO_NUM_6

// ---- UC8276 command set ----
static constexpr uint8_t X3_CMD_PANEL_SETTING   = 0x00;
static constexpr uint8_t X3_CMD_POWER_SETTING   = 0x01;
static constexpr uint8_t X3_CMD_POWER_OFF       = 0x02;
static constexpr uint8_t X3_CMD_POWER_ON        = 0x04;
static constexpr uint8_t X3_CMD_BOOSTER         = 0x06;
static constexpr uint8_t X3_CMD_DEEP_SLEEP      = 0x07;
static constexpr uint8_t X3_CMD_DATA_OLD        = 0x10;
static constexpr uint8_t X3_CMD_DISPLAY_REFRESH = 0x12;
static constexpr uint8_t X3_CMD_DATA_NEW        = 0x13;
static constexpr uint8_t X3_CMD_PLL             = 0x30;
static constexpr uint8_t X3_CMD_LUT_VCOM        = 0x20;
static constexpr uint8_t X3_CMD_LUT_WW          = 0x21;
static constexpr uint8_t X3_CMD_LUT_BW          = 0x22;
static constexpr uint8_t X3_CMD_LUT_WB          = 0x23;
static constexpr uint8_t X3_CMD_LUT_BB          = 0x24;
static constexpr uint8_t X3_CMD_VCOM_INTERVAL   = 0x50;
static constexpr uint8_t X3_CMD_RESOLUTION      = 0x61;
static constexpr uint8_t X3_CMD_FLASH_MODE      = 0x65;
static constexpr uint8_t X3_CMD_VCM_DC          = 0x82;
static constexpr uint8_t X3_CMD_GATE_VOLTAGE    = 0x03;
static constexpr uint8_t X3_CMD_PARTIAL_IN      = 0x91;
static constexpr uint8_t X3_CMD_PARTIAL_WINDOW  = 0x90;
static constexpr uint8_t X3_CMD_PARTIAL_OUT     = 0x92;
static constexpr uint8_t X3_CMD_OTP_SELECTION   = 0xE1;

// clang-format off
// ---- LUT tables: 5 sets × 5 registers × 42 bytes ----
// All data verified byte-exact against papyrix-reader-main and 3pyrix.

// Full sync (~472ms) — 26 frame groups, high-quality waveform
static const uint8_t kX3LutVcomFull[] = {
    0x00,0x06,0x02,0x06,0x06,0x01, 0x00,0x05,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWwFull[] = {
    0x20,0x06,0x02,0x06,0x06,0x01, 0x00,0x05,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBwFull[] = {
    0xAA,0x06,0x02,0x06,0x06,0x01, 0x80,0x05,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWbFull[] = {
    0x55,0x06,0x02,0x06,0x06,0x01, 0x40,0x05,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBbFull[] = {
    0x10,0x06,0x02,0x06,0x06,0x01, 0x00,0x05,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};

// Turbo differential (~382ms) — same VS, reduced timing (TP=4 instead of 6/5)
static const uint8_t kX3LutVcomTurbo[] = {
    0x00,0x04,0x02,0x04,0x04,0x01, 0x00,0x04,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWwTurbo[] = {
    0x20,0x04,0x02,0x04,0x04,0x01, 0x00,0x04,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBwTurbo[] = {
    0xAA,0x04,0x02,0x04,0x04,0x01, 0x80,0x04,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWbTurbo[] = {
    0x55,0x04,0x02,0x04,0x04,0x01, 0x40,0x04,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBbTurbo[] = {
    0x10,0x04,0x02,0x04,0x04,0x01, 0x00,0x04,0x01,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};

// Image write (~908ms) — 3-phase complex waveform for first-paint quality
static const uint8_t kX3LutVcomImg[] = {
    0x00,0x08,0x0B,0x02,0x03,0x01, 0x00,0x0C,0x02,0x07,0x02,0x01,
    0x00,0x01,0x00,0x02,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWwImg[] = {
    0xA8,0x08,0x0B,0x02,0x03,0x01, 0x44,0x0C,0x02,0x07,0x02,0x01,
    0x04,0x01,0x00,0x02,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBwImg[] = {
    0x80,0x08,0x0B,0x02,0x03,0x01, 0x62,0x0C,0x02,0x07,0x02,0x01,
    0x00,0x01,0x00,0x02,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWbImg[] = {
    0x88,0x08,0x0B,0x02,0x03,0x01, 0x60,0x0C,0x02,0x07,0x02,0x01,
    0x00,0x01,0x00,0x02,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBbImg[] = {
    0x00,0x08,0x0B,0x02,0x03,0x01, 0x4A,0x0C,0x02,0x07,0x02,0x01,
    0x88,0x01,0x00,0x02,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};

// Grayscale (~127ms) — single-phase, per-transition voltage tuning
static const uint8_t kX3LutVcomGray[] = {
    0x00,0x03,0x02,0x01,0x01,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWwGray[] = {
    0x20,0x03,0x02,0x01,0x01,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBwGray[] = {
    0x80,0x03,0x02,0x01,0x01,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutWbGray[] = {
    0x00,0x03,0x02,0x01,0x01,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBbGray[] = {
    0x00,0x03,0x02,0x01,0x01,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};

// Fast partial-style AA LUT (for partial window updates)
static const uint8_t kX3LutVcomFast[] = {
    0x00,0x18,0x18,0x01,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
// W→W and B→B: no drive (all-zero VS) so unchanged pixels in the partial
// window stay quiet. Without this the entire window "pulses" each refresh
// because the LUTs were kicking stable pixels with ±V for 48 frames.
static const uint8_t kX3LutWwFast[] = {
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
// B→W: pull stable-black pixel to white with one VSH phase.
static const uint8_t kX3LutBwFast[] = {
    0x20,0x18,0x18,0x01,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
// W→B: pull stable-white pixel to black with one VSL phase.
static const uint8_t kX3LutWbFast[] = {
    0x10,0x18,0x18,0x01,0x00,0x01, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t kX3LutBbFast[] = {
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00};
// clang-format on

class EInkDisplay final : public microreader::IDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = microreader::DisplayFrame::kPhysicalWidth;    // 792
  static constexpr uint16_t DISPLAY_HEIGHT = microreader::DisplayFrame::kPhysicalHeight;  // 528
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = microreader::DisplayFrame::kStride;     // 99
  static constexpr uint32_t BUFFER_SIZE = microreader::DisplayFrame::kPixelBytes;         // 52272

  EInkDisplay() = default;

  void begin() {
    gpio_config_t out_cfg{};
    out_cfg.pin_bit_mask = (1ULL << EPD_CS) | (1ULL << EPD_DC) | (1ULL << EPD_RST);
    out_cfg.mode = GPIO_MODE_OUTPUT;
    out_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    out_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&out_cfg);

    gpio_config_t in_cfg{};
    in_cfg.pin_bit_mask = (1ULL << EPD_BUSY);
    in_cfg.mode = GPIO_MODE_INPUT;
    in_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    in_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    in_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&in_cfg);

    gpio_set_level(EPD_CS, 1);
    gpio_set_level(EPD_DC, 1);

    spi_bus_config_t bus{};
    bus.mosi_io_num = EPD_MOSI;
    bus.miso_io_num = GPIO_NUM_7;  // shared with SD card
    bus.sclk_io_num = EPD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = 10 * 1000 * 1000;  // 10 MHz max for UC8276
    dev.mode = 0;
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    spi_bus_add_device(SPI2_HOST, &dev, &spi_);

    resetDisplay();
    initDisplayController();

    ESP_LOGI("epd_x3", "ready (UC8276, 792x528, 10MHz SPI)");
  }

  // No-ops: serial LUT upload is an X4 debug feature only.
  void set_grayscale_lut(const uint8_t* /*lut*/) {}
  void set_grayscale_revert_lut(const uint8_t* /*lut*/) {}

  // ---- IDisplay ----

  void full_refresh(const uint8_t* pixels, microreader::RefreshMode mode, bool turnOffScreen = false) override {
    // On X3 we ignore RefreshMode half/full distinction — the state machine
    // picks the appropriate waveform based on sync history.
    const bool forceFullSync = (mode == microreader::RefreshMode::Full);
    if (forceFullSync) {
      forceFullSyncNext_ = true;
    }
    displayBuffer(pixels, /*fastMode=*/false, turnOffScreen);
  }

  void partial_refresh(const uint8_t* new_pixels, const uint8_t* /*prev_pixels*/) override {
    displayBuffer(new_pixels, /*fastMode=*/true, /*turnOffScreen=*/false);
  }

  // write_ram_bw: sends `data` to OLD RAM (0x10), Y-mirrored.
  // On X3 this primes the LSB plane for grayscale.
  void write_ram_bw(const uint8_t* data) override {
    prepareForOp_(/*nextOpIsPartial=*/false);
    sendCommand(X3_CMD_DATA_OLD);
    sendMirroredPlane(data, false);
    lsbValid_ = true;
  }

  // write_ram_red: sends `data` to NEW RAM (0x13), Y-mirrored.
  // Only called after write_ram_bw (LSB must precede MSB on X3).
  void write_ram_red(const uint8_t* data) override {
    if (!lsbValid_)
      return;
    prepareForOp_(/*nextOpIsPartial=*/false);
    sendCommand(X3_CMD_DATA_NEW);
    sendMirroredPlane(data, false);
  }

  // grayscale_refresh: trigger a grayscale display update using GRAY LUTs.
  // Assumes 0x10 (LSB) and 0x13 (MSB) already contain valid plane data.
  void grayscale_refresh(bool turnOffScreen = false) override {
    if (!lsbValid_)
      return;
    prepareForOp_(/*nextOpIsPartial=*/false);

    sendLuts5x42(kX3LutVcomGray, kX3LutWwGray, kX3LutBwGray, kX3LutWbGray, kX3LutBbGray);
    loadedLuts_ = X3LutSet::GRAY;

    const uint8_t vcomArg[2] = {0x29, 0x07};
    sendCommand(X3_CMD_VCOM_INTERVAL);
    sendData(vcomArg, 2);

    powerOnIfNeeded();
    sendCommand(X3_CMD_DISPLAY_REFRESH);
    waitForRefresh("gray");

    if (turnOffScreen) {
      sendCommand(X3_CMD_POWER_OFF);
      waitForRefresh("gray_poweroff");
      isScreenOn_ = false;
      loadedLuts_ = X3LutSet::NONE;
    }

    // RED RAM is stale after grayscale; force a full resync next page turn.
    redRamSynced_ = false;
    loadedLuts_ = X3LutSet::NONE;
    lsbValid_ = false;
    //inGrayscaleMode_ = true;
  }

  // revert_grayscale: restore prev_pixels into both RAMs so the next
  // differential update has a coherent baseline.
  void revert_grayscale(const uint8_t* prev_pixels) override {
    if (!prev_pixels)
      return;
    prepareForOp_(/*nextOpIsPartial=*/false);
    sendCommand(X3_CMD_DATA_NEW);
    sendMirroredPlane(prev_pixels, false);
    sendCommand(X3_CMD_DATA_OLD);
    sendMirroredPlane(prev_pixels, false);
    redRamSynced_ = true;
	forceFullSyncNext_ = false;
    //inGrayscaleMode_ = false;
    //forceFullSyncNext_ = true;
    forcedConditionPassesNext_ = 0;
  }

  // partial_refresh_region: windowed update using the UC8276 partial window
  // commands and FAST LUTs. Returns immediately (non-blocking); next call
  // drains BUSY in prepareForOp_() before issuing new commands.
  //
  // PARTIAL_OUT must NOT be sent before DISPLAY_REFRESH — that would make the
  // refresh full-screen and cause the entire panel to flash with FAST LUT
  // every time. We keep partial mode active across the refresh and let the
  // next op's prologue exit it.
  void partial_refresh_region(int phys_x, int phys_y, int phys_w, int phys_h, const uint8_t* new_buf,
                              int stride_bytes) override {
    prepareForOp_(/*nextOpIsPartial=*/true);

    // Coordinates in controller space (Y-mirrored from physical).
    const uint16_t x_start = static_cast<uint16_t>(phys_x);
    const uint16_t x_end = static_cast<uint16_t>(phys_x + phys_w - 1);
    // Controller Y: bottom of physical window (highest phys row) maps to lowest controller Y.
    const uint16_t ctrl_y_top = static_cast<uint16_t>(DISPLAY_HEIGHT - phys_y - phys_h);
    const uint16_t ctrl_y_bot = static_cast<uint16_t>(DISPLAY_HEIGHT - phys_y - 1);

    const uint8_t win[9] = {
        static_cast<uint8_t>(x_start >> 8), static_cast<uint8_t>(x_start & 0xFF),
        static_cast<uint8_t>(x_end >> 8),   static_cast<uint8_t>(x_end & 0xFF),
        static_cast<uint8_t>(ctrl_y_top >> 8), static_cast<uint8_t>(ctrl_y_top & 0xFF),
        static_cast<uint8_t>(ctrl_y_bot >> 8), static_cast<uint8_t>(ctrl_y_bot & 0xFF),
        0x01,
    };

    // Load FAST LUTs once (cached via loadedLuts_).
    if (loadedLuts_ != X3LutSet::FAST) {
      sendLuts5x42(kX3LutVcomFast, kX3LutWwFast, kX3LutBwFast, kX3LutWbFast, kX3LutBbFast);
      loadedLuts_ = X3LutSet::FAST;
    }
    const uint8_t vcomArg[2] = {0x29, 0x07};
    sendCommand(X3_CMD_VCOM_INTERVAL);
    sendData(vcomArg, 2);

    if (!inPartialMode_) {
      sendCommand(X3_CMD_PARTIAL_IN);
      inPartialMode_ = true;
    }
    sendCommand(X3_CMD_PARTIAL_WINDOW);
    sendData(win, 9);

    // Write new data to 0x13 for the window region. Rows must be sent in
    // bottom-to-top physical order (= controller scan order).
    sendCommand(X3_CMD_DATA_NEW);
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    for (int row = phys_h - 1; row >= 0; --row) {
      const uint8_t* src = new_buf + static_cast<uint32_t>(row) * stride_bytes;
      spiWriteBytes_(src, static_cast<uint32_t>(stride_bytes));
    }
    gpio_set_level(EPD_CS, 1);

    powerOnIfNeeded();
    sendCommand(X3_CMD_DISPLAY_REFRESH);
    // Non-blocking. Partial mode stays active until the next non-partial op
    // calls prepareForOp_(false), which drains BUSY then sends PARTIAL_OUT.
  }

  void deep_sleep() override {
    prepareForOp_(/*nextOpIsPartial=*/false);
    if (isScreenOn_) {
      sendCommand(X3_CMD_POWER_OFF);
      waitForRefresh("deep_sleep_poweroff");
      isScreenOn_ = false;
    }
    sendCommand(X3_CMD_DEEP_SLEEP);
    sendData(0xA5);
    loadedLuts_ = X3LutSet::NONE;
  }

  // BUSY on UC8276 is active-LOW during refresh (HIGH = idle).
  bool is_busy() const override {
    return gpio_get_level(EPD_BUSY) == 0;
  }

  bool in_grayscale_mode() const override {
	// X3 grayscale state is managed via redRamSynced_; no explicit mode flag needed.
    return false;
    //return inGrayscaleMode_;
  }

 private:
  enum class X3LutSet : uint8_t { NONE, FULL, TURBO, IMG, GRAY, FAST };

  spi_device_handle_t spi_ = nullptr;
  bool isScreenOn_ = false;
  bool redRamSynced_ = false;
  bool lsbValid_ = false;
  // True after a partial-window refresh was issued; PARTIAL_OUT is deferred
  // to the next op's prologue so the in-flight refresh stays partial.
  bool inPartialMode_ = false;
  // Number of forced full-sync passes after a hardware reset (e.g. wake from
  // deep sleep). Must be 1: the first user-visible render then runs an IMG
  // sync (3-phase, ~908ms — already a thorough cleanup) plus the FULL-LUT
  // settle pass (triggered when remaining == 1), and the next user action
  // (e.g. cursor move) goes through the fast path with no black flash.
  //
  // Setting this to 2 deferred the second bootstrap sync to the user's first
  // input, producing a visible IMG-LUT flash on the first cursor move after
  // wake — see git history for the symptom.
  uint8_t initialFullSyncsRemaining_ = 1;
  bool forceFullSyncNext_ = false;
  uint8_t forcedConditionPassesNext_ = 0;
  X3LutSet loadedLuts_ = X3LutSet::NONE;

  static uint32_t millis() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
  }
  static void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
  }

  void sendCommand(uint8_t cmd) {
    gpio_set_level(EPD_DC, 0);
    gpio_set_level(EPD_CS, 0);
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;
    spi_device_polling_transmit(spi_, &t);
    gpio_set_level(EPD_CS, 1);
  }

  void sendData(uint8_t d) {
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = d;
    spi_device_polling_transmit(spi_, &t);
    gpio_set_level(EPD_CS, 1);
  }

  void sendData(const uint8_t* data, uint32_t length) {
    static constexpr size_t kChunk = 4092;
    gpio_set_level(EPD_DC, 1);
    size_t off = 0;
    while (off < length) {
      const size_t chunk = (length - off < kChunk) ? (length - off) : kChunk;
      gpio_set_level(EPD_CS, 0);
      spi_transaction_t t{};
      t.length = chunk * 8;
      t.tx_buffer = data + off;
      spi_device_polling_transmit(spi_, &t);
      gpio_set_level(EPD_CS, 1);
      off += chunk;
    }
  }

  // Raw SPI byte write within an existing CS-low / DC-high window.
  // Used by sendMirroredPlane to stream rows without redundant CS toggling.
  void spiWriteBytes_(const uint8_t* data, uint32_t length) {
    static constexpr size_t kChunk = 4092;
    size_t off = 0;
    while (off < length) {
      const size_t chunk = (length - off < kChunk) ? (length - off) : kChunk;
      spi_transaction_t t{};
      t.length = chunk * 8;
      t.tx_buffer = data + off;
      spi_device_polling_transmit(spi_, &t);
      off += chunk;
    }
  }

  // Two-phase BUSY wait for UC8276:
  //   Phase 1: wait for BUSY to go LOW  (controller starts refresh, up to 1s)
  //   Phase 2: wait for BUSY to go HIGH (refresh complete, up to 30s)
  // If BUSY never went LOW, return immediately (already done / spurious call).
  void waitForRefresh(const char* comment = nullptr) {
    const uint32_t t0 = millis();
    bool sawLow = false;

    while (gpio_get_level(EPD_BUSY) == 1) {
      vTaskDelay(pdMS_TO_TICKS(1));
      if (millis() - t0 > 1000)
        break;
    }
    if (gpio_get_level(EPD_BUSY) == 0) {
      sawLow = true;
      while (gpio_get_level(EPD_BUSY) == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (millis() - t0 > 30000)
          break;
      }
    }
    if (!sawLow)
      return;

    if (comment) {
      ESP_LOGI("epd_x3", "waitForRefresh(%s): %lu ms", comment, millis() - t0);
    }
  }

  // Single-phase wait: returns immediately if not busy, else blocks until the
  // controller releases BUSY. Used at the start of each op to drain any
  // refresh issued non-blocking by a previous call (e.g. partial_refresh_region).
  void waitWhileBusy_() {
    const uint32_t t0 = millis();
    while (gpio_get_level(EPD_BUSY) == 0) {  // BUSY active-low on UC8276
      vTaskDelay(pdMS_TO_TICKS(1));
      if (millis() - t0 > 30000)
        break;
    }
  }

  // Prologue for any controller op. Drains the previous (possibly async)
  // refresh, then exits partial-window mode if the previous op left it
  // active and the next op needs full-screen mode.
  void prepareForOp_(bool nextOpIsPartial) {
    waitWhileBusy_();
    if (inPartialMode_ && !nextOpIsPartial) {
      sendCommand(X3_CMD_PARTIAL_OUT);
      inPartialMode_ = false;
    }
  }

  void resetDisplay() {
    gpio_set_level(EPD_RST, 1);
    delay(20);
    gpio_set_level(EPD_RST, 0);
    delay(2);
    gpio_set_level(EPD_RST, 1);
    delay(20);
    delay(50);  // X3 UC8276 needs extra settle time after reset
  }

  void initDisplayController() {
    // Sequence from papyrix-reader-main and 3pyrix (byte-verified).
    sendCommand(X3_CMD_PANEL_SETTING);  // 0x00
    sendData(0x3F);
    sendData(0x08);

    sendCommand(X3_CMD_RESOLUTION);  // 0x61: 792×600 controller RAM (display uses 528 rows)
    sendData(0x03);
    sendData(0x18);
    sendData(0x02);
    sendData(0x58);

    sendCommand(X3_CMD_FLASH_MODE);  // 0x65
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);

    sendCommand(X3_CMD_GATE_VOLTAGE);  // 0x03
    sendData(0x1D);

    sendCommand(X3_CMD_POWER_SETTING);  // 0x01
    sendData(0x07);
    sendData(0x17);
    sendData(0x3F);
    sendData(0x3F);
    sendData(0x17);

    sendCommand(X3_CMD_VCM_DC);  // 0x82
    sendData(0x1D);

    sendCommand(X3_CMD_BOOSTER);  // 0x06
    sendData(0x25);
    sendData(0x25);
    sendData(0x3C);
    sendData(0x37);

    sendCommand(X3_CMD_PLL);  // 0x30
    sendData(0x09);

    sendCommand(X3_CMD_OTP_SELECTION);  // 0xE1
    sendData(0x02);

    // Pre-load FULL LUTs so the controller is ready for the first displayBuffer call.
    sendLuts5x42(kX3LutVcomFull, kX3LutWwFull, kX3LutBwFull, kX3LutWbFull, kX3LutBbFull);
    loadedLuts_ = X3LutSet::FULL;

    isScreenOn_ = false;
  }

  // Send all 5 LUT registers in one shot (5 × 42 bytes = 210 bytes total).
  // Cached via loadedLuts_ so repeated calls with the same set are skipped.
  void sendLuts5x42(const uint8_t* vcom, const uint8_t* ww, const uint8_t* bw,
                    const uint8_t* wb, const uint8_t* bb) {
    sendCommand(X3_CMD_LUT_VCOM);
    sendData(vcom, 42);
    sendCommand(X3_CMD_LUT_WW);
    sendData(ww, 42);
    sendCommand(X3_CMD_LUT_BW);
    sendData(bw, 42);
    sendCommand(X3_CMD_LUT_WB);
    sendData(wb, 42);
    sendCommand(X3_CMD_LUT_BB);
    sendData(bb, 42);
  }

  void powerOnIfNeeded() {
    if (!isScreenOn_) {
      sendCommand(X3_CMD_POWER_ON);
      waitForRefresh("power_on");
      isScreenOn_ = true;
    }
  }

  // Send a full frame to the controller in Y-mirrored order (bottom physical
  // row first = controller row 0). When invertBits=true, each byte is inverted
  // (used to write full-sync inverted data to both RAMs).
  void sendMirroredPlane(const uint8_t* plane, bool invertBits) {
    // Row buffer sized for one physical row (99 bytes for 792px).
    alignas(4) uint8_t row[DISPLAY_WIDTH_BYTES];

    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);

    for (uint16_t y = 0; y < DISPLAY_HEIGHT; ++y) {
      const uint16_t src_y = static_cast<uint16_t>(DISPLAY_HEIGHT - 1 - y);
      const uint8_t* src = plane + static_cast<uint32_t>(src_y) * DISPLAY_WIDTH_BYTES;
      if (invertBits) {
        for (uint16_t x = 0; x < DISPLAY_WIDTH_BYTES; ++x)
          row[x] = static_cast<uint8_t>(~src[x]);
        spiWriteBytes_(row, DISPLAY_WIDTH_BYTES);
      } else {
        spiWriteBytes_(src, DISPLAY_WIDTH_BYTES);
      }
    }

    gpio_set_level(EPD_CS, 1);
  }

  // Core X3 display update state machine.
  // fastMode=false → full resync (IMG LUT + inverted writes to both RAMs).
  // fastMode=true  → differential update (TURBO LUT + write to 0x13 only).
  // Decision also forced to full-sync when RAMs are not yet synced or we're
  // still in the initial 2-sync bootstrap.
  void displayBuffer(const uint8_t* frameBuffer, bool fastMode, bool turnOffScreen) {
    // Drain any in-flight async refresh (e.g. show_loading) and exit partial
    // mode if it's still active. Without this, sending LUTs/data while the
    // panel is BUSY produces dropped commands and visible stalls — most
    // visibly when a full page render fires right after the last loading-box
    // refresh during EPUB conversion.
    prepareForOp_(/*nextOpIsPartial=*/false);

    lsbValid_ = false;

    const bool forcedFullSync = forceFullSyncNext_;
    const bool doFullSync = !fastMode || !redRamSynced_ || initialFullSyncsRemaining_ > 0 || forcedFullSync;

    ESP_LOGD("epd_x3", "displayBuffer: %s", doFullSync ? "FULL" : "FAST");

    if (doFullSync) {
      if (loadedLuts_ != X3LutSet::IMG) {
        sendLuts5x42(kX3LutVcomImg, kX3LutWwImg, kX3LutBwImg, kX3LutWbImg, kX3LutBbImg);
        loadedLuts_ = X3LutSet::IMG;
      }
      sendCommand(X3_CMD_DATA_NEW);
      sendMirroredPlane(frameBuffer, true);
      sendCommand(X3_CMD_DATA_OLD);
      sendMirroredPlane(frameBuffer, true);

      const uint8_t vcomFull[2] = {0xA9, 0x07};
      sendCommand(X3_CMD_VCOM_INTERVAL);
      sendData(vcomFull, 2);
    } else {
      if (loadedLuts_ != X3LutSet::TURBO) {
        sendLuts5x42(kX3LutVcomTurbo, kX3LutWwTurbo, kX3LutBwTurbo, kX3LutWbTurbo, kX3LutBbTurbo);
        loadedLuts_ = X3LutSet::TURBO;
      }
      sendCommand(X3_CMD_DATA_NEW);
      sendMirroredPlane(frameBuffer, false);

      const uint8_t vcomFast[2] = {0x29, 0x07};
      sendCommand(X3_CMD_VCOM_INTERVAL);
      sendData(vcomFast, 2);
    }

    powerOnIfNeeded();
    sendCommand(X3_CMD_DISPLAY_REFRESH);
    waitForRefresh(doFullSync ? "full" : "fast");

    if (turnOffScreen) {
      sendCommand(X3_CMD_POWER_OFF);
      waitForRefresh("poweroff");
      isScreenOn_ = false;
      loadedLuts_ = X3LutSet::NONE;
    }

    // Extra settle delay after non-fast refresh (helps early page-turn quality).
    if (!fastMode)
      delay(200);

    // Post-condition settle pass: one extra FULL LUT partial sweep on the first
    // bootstrap sync, or as many passes as requested by forceFullSyncNext_.
    uint8_t postPasses = 0;
    if (doFullSync) {
      if (forcedFullSync)
        postPasses = forcedConditionPassesNext_;
      else if (initialFullSyncsRemaining_ == 1)
        postPasses = 1;
    }

    if (postPasses > 0) {
      const uint16_t xS = 0, xE = static_cast<uint16_t>(DISPLAY_WIDTH - 1);
      const uint16_t yS = 0, yE = static_cast<uint16_t>(DISPLAY_HEIGHT - 1);
      const uint8_t win[9] = {
          static_cast<uint8_t>(xS >> 8), static_cast<uint8_t>(xS & 0xFF),
          static_cast<uint8_t>(xE >> 8), static_cast<uint8_t>(xE & 0xFF),
          static_cast<uint8_t>(yS >> 8), static_cast<uint8_t>(yS & 0xFF),
          static_cast<uint8_t>(yE >> 8), static_cast<uint8_t>(yE & 0xFF),
          0x01,
      };
      if (loadedLuts_ != X3LutSet::FULL) {
        sendLuts5x42(kX3LutVcomFull, kX3LutWwFull, kX3LutBwFull, kX3LutWbFull, kX3LutBbFull);
        loadedLuts_ = X3LutSet::FULL;
      }
      const uint8_t vcomSettle[2] = {0x29, 0x07};
      sendCommand(X3_CMD_VCOM_INTERVAL);
      sendData(vcomSettle, 2);

      for (uint8_t i = 0; i < postPasses; ++i) {
        sendCommand(X3_CMD_PARTIAL_IN);
        sendCommand(X3_CMD_PARTIAL_WINDOW);
        sendData(win, 9);
        sendCommand(X3_CMD_DATA_NEW);
        sendMirroredPlane(frameBuffer, false);
        sendCommand(X3_CMD_PARTIAL_OUT);
        powerOnIfNeeded();
        sendCommand(X3_CMD_DISPLAY_REFRESH);
        waitForRefresh("settle");
      }
    }

    // Sync OLD RAM (0x10) with the current (non-inverted) frame so the next
    // differential update has the correct previous-frame reference.
    sendCommand(X3_CMD_DATA_OLD);
    sendMirroredPlane(frameBuffer, false);
    redRamSynced_ = true;

    if (doFullSync && initialFullSyncsRemaining_ > 0)
      --initialFullSyncsRemaining_;
    //inGrayscaleMode_ = false;
    forceFullSyncNext_ = false;
    forcedConditionPassesNext_ = 0;
  }
};
