#include "driver/gpio.h"
#include "epd.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "input.h"
#include "logger.h"
#include "microreader/Application.h"
#include "microreader/DisplayQueue.h"
#include "microreader/Loop.h"
#include "runtime.h"
#include "serial_lut.h"

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
bool g_lut_target_settle = false;

extern "C" void app_main(void) {
  verify_ota();

  static Esp32Logger logger;
  static Esp32InputSource input;
  static EInkDisplay epd;
  static Esp32Runtime runtime(50);
  static microreader::Application app;
  static microreader::DisplayQueue queue(epd);

  // Wait for the serial monitor to connect before logging anything.
  // vTaskDelay(pdMS_TO_TICKS(2000));

  logger.log(microreader::LogLevel::Info, "Booting up...");

  epd.begin();
  serial_lut_start();

  static uint8_t lut_buf[kLutSize];

  app.start(logger, queue);

  // Discard the power-button press that woke us from deep sleep.
  input.clear_button(microreader::Button::Power);

  while (runtime.should_continue() && app.running()) {
    if (serial_lut_take(lut_buf)) {
      if (g_lut_target_settle)
        epd.setCustomSettleLUT(lut_buf);
      else
        epd.setCustomLUT(lut_buf);
    }

    microreader::run_loop_iteration(app, queue, input, runtime, logger);
  }

  logger.log(microreader::LogLevel::Info, "Shutting down, entering deep sleep...");

  // Enter deep sleep; wake on power button press (active LOW, GPIO 3).
  constexpr gpio_num_t kPowerPin = GPIO_NUM_3;
  gpio_set_direction(kPowerPin, GPIO_MODE_INPUT);
  gpio_pullup_en(kPowerPin);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << kPowerPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}
