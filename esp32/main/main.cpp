#include <string>

#include "epd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "logger.h"
#include "microreader/core/Input.h"

static constexpr const char* kBtnNames[] = {"Back", "Confirm", "Left", "Right", "Up", "Down", "Power"};
static constexpr microreader::Button kButtons[] = {microreader::Button::Button0, microreader::Button::Button1,
                                                   microreader::Button::Button2, microreader::Button::Button3,
                                                   microreader::Button::Up,      microreader::Button::Down,
                                                   microreader::Button::Power};

extern "C" void app_main(void) {
  static Esp32Logger logger;
  static Esp32InputSource input;
  static EInkDisplay epd;
  epd.begin();

  logger.log(microreader::LogLevel::Info, "debug loop started");

  uint32_t last_tick_ms = 0;

  while (true) {
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);

    if (now - last_tick_ms >= 1000) {
      last_tick_ms = now;
      logger.log(microreader::LogLevel::Info, "tick");
    }

    const microreader::ButtonState state = input.poll_buttons();
    for (int i = 0; i < 7; ++i) {
      if (state.is_pressed(kButtons[i])) {
        logger.log(microreader::LogLevel::Info, std::string("button: ") + kBtnNames[i]);
      }
    }

    if (state.is_pressed(microreader::Button::Button2)) {  // Left -> white
      logger.log(microreader::LogLevel::Info, "clearing screen white");
      epd.clearScreen(0xFF);
      epd.displayBuffer(FAST_REFRESH);
    } else if (state.is_pressed(microreader::Button::Button3)) {  // Right -> black
      logger.log(microreader::LogLevel::Info, "clearing screen black");
      epd.clearScreen(0x00);
      epd.displayBuffer(FAST_REFRESH);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
