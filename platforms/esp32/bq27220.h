#pragma once

// BQ27220 fuel gauge driver for ESP-IDF (Xteink X3).
// I²C address 0x55, SDA=GPIO20, SCL=GPIO0, 400 kHz.
//
// The bus is initialised and deleted on every call so that GPIO20/GPIO0
// are returned to floating-input state between reads.  This mirrors the
// papyrix-reader-main BatteryMonitor pattern and keeps the pins free for
// any other code that samples them (e.g. USB detect check).

#include <cstdint>
#include <optional>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr gpio_num_t kBq27220Sda = GPIO_NUM_20;
static constexpr gpio_num_t kBq27220Scl = GPIO_NUM_0;
static constexpr uint8_t kBq27220Addr = 0x55;

static constexpr uint8_t kBq27220RegSoc = 0x2C;      // 16-bit LE, %
static constexpr uint8_t kBq27220RegCurrent = 0x0C;  // 16-bit LE, signed mA (>0 = charging)

class Bq27220 {
 public:
  // Returns state-of-charge [0–100].
  // On I²C failure: returns the last good reading if one exists, otherwise 100
  // as a safe pre-boot default (a fully-discharged battery wouldn't have booted
  // the device, so 100 is safer than 0 for seeding the UI on a transient glitch).
  std::optional<uint8_t> read_soc() const {
    uint16_t val = 0;
    const bool ok = read_reg16_(kBq27220RegSoc, &val);
    if (!ok || val > 100) {
      return have_reading_ ? std::optional<uint8_t>(last_good_soc_)
                           : std::optional<uint8_t>(100);
    }
    last_good_soc_ = static_cast<uint8_t>(val);
    have_reading_ = true;
    return last_good_soc_;
  }

  // Returns true when USB is supplying charge current.
  // Two attempts with 2 ms settle so a single bus glitch doesn't flip the result.
  bool is_charging() const {
    for (int attempt = 0; attempt < 2; ++attempt) {
      uint16_t raw = 0;
      if (read_reg16_(kBq27220RegCurrent, &raw)) {
        return static_cast<int16_t>(raw) > 0;
      }
      if (attempt == 0)
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return false;
  }

 private:
  mutable bool have_reading_ = false;
  mutable uint8_t last_good_soc_ = 0;

  // Read a 16-bit little-endian register. Installs and deletes the I²C driver
  // around each call so the pins are released immediately after use.
  bool read_reg16_(uint8_t reg, uint16_t* out) const {
    i2c_config_t conf{};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = kBq27220Sda;
    conf.scl_io_num = kBq27220Scl;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;
    i2c_param_config(I2C_NUM_0, &conf);

    if (i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK)
      return false;

    bool ok = false;
    uint8_t lo = 0, hi = 0;

    // Write: START + addr_W + reg + STOP
    {
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, static_cast<uint8_t>((kBq27220Addr << 1) | I2C_MASTER_WRITE), true);
      i2c_master_write_byte(cmd, reg, true);
      i2c_master_stop(cmd);
      const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(6));
      i2c_cmd_link_delete(cmd);
      if (err != ESP_OK)
        goto done;
    }

    // Read: START + addr_R + lo(ACK) + hi(NACK) + STOP
    {
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, static_cast<uint8_t>((kBq27220Addr << 1) | I2C_MASTER_READ), true);
      i2c_master_read_byte(cmd, &lo, I2C_MASTER_ACK);
      i2c_master_read_byte(cmd, &hi, I2C_MASTER_NACK);
      i2c_master_stop(cmd);
      const esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(6));
      i2c_cmd_link_delete(cmd);
      if (err == ESP_OK) {
        *out = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
        ok = true;
      }
    }

  done:
    i2c_driver_delete(I2C_NUM_0);
    // Return pins to floating-input so GPIO20 and GPIO0 are not held by the
    // I²C peripheral between reads (allows other code to sample them freely).
    gpio_reset_pin(kBq27220Sda);
    gpio_reset_pin(kBq27220Scl);
    return ok;
  }
};
