#include "driver/gpio.h"
#include "epd.h"
#include "esp_sleep.h"
#include "input.h"
#include "logger.h"
#include "microreader/Application.h"
#include "microreader/DisplayQueue.h"
#include "microreader/Loop.h"
#include "runtime.h"
#include "serial_lut.h"

extern "C" void app_main(void) {
  static Esp32Logger logger;
  static Esp32InputSource input;
  static EInkDisplay epd;
  static Esp32Runtime runtime(50);
  static microreader::Application app;
  static microreader::DisplayQueue queue(epd);

  // Wait for the serial monitor to connect before logging anything.
  vTaskDelay(pdMS_TO_TICKS(2000));

  logger.log(microreader::LogLevel::Info, "Booting up...");

  epd.begin();
  runtime.set_input_source(&input);
  serial_lut_start();

  static uint8_t lut_buf[kLutSize];

  app.start(logger, queue);
  while (runtime.should_continue() && app.running()) {
    if (serial_lut_take(lut_buf))
      epd.setCustomLUT(lut_buf);

    microreader::run_loop_iteration(app, queue, runtime, logger);
  }

  logger.log(microreader::LogLevel::Info, "Shutting down, entering deep sleep...");

  // Enter deep sleep; wake on power button press (active LOW, GPIO 3).
  constexpr gpio_num_t kPowerPin = GPIO_NUM_3;
  gpio_set_direction(kPowerPin, GPIO_MODE_INPUT);
  gpio_pullup_en(kPowerPin);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << kPowerPin, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}
