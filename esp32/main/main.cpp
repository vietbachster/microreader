#include <string>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "microreader/core/Application.h"
#include "microreader/core/Loop.h"

class Esp32Display final : public microreader::IDisplay {
 public:
  void present(const microreader::DisplayFrame& frame, microreader::RefreshMode mode) override {
    (void)frame;
    (void)mode;
    // TODO: map DisplayFrame to e-ink framebuffer and refresh.
  }
};

class Esp32Logger final : public microreader::ILogger {
 public:
  void log(microreader::LogLevel level, const std::string& message) override {
    switch (level) {
      case microreader::LogLevel::Info:
        ESP_LOGI("microreader2", "%s", message.c_str());
        break;
      case microreader::LogLevel::Warning:
        ESP_LOGW("microreader2", "%s", message.c_str());
        break;
      case microreader::LogLevel::Error:
        ESP_LOGE("microreader2", "%s", message.c_str());
        break;
    }
  }
};

class Esp32InputSource final : public microreader::IInputSource {
 public:
  microreader::ButtonState poll_buttons() override {
    // TODO: map GPIO/ADC button state into this bitmask.
    state_.update(0);
    return state_;
  }

 private:
  microreader::ButtonState state_{};
};

class Esp32Runtime final : public microreader::IRuntime {
 public:
  explicit Esp32Runtime(uint32_t frame_time_ms) : frame_time_ms_(frame_time_ms), input_(nullptr) {}

  void set_input_source(microreader::IInputSource* input) {
    input_ = input;
  }

  bool should_continue() const override {
    return true;
  }

  microreader::ButtonState poll_buttons() override {
    if (!input_) {
      microreader::ButtonState empty{};
      empty.update(0);
      return empty;
    }
    return input_->poll_buttons();
  }

  uint32_t frame_time_ms() const override {
    return frame_time_ms_;
  }

  void wait_next_frame() override {
    vTaskDelay(pdMS_TO_TICKS(frame_time_ms_));
  }

 private:
  uint32_t frame_time_ms_;
  microreader::IInputSource* input_;
};

extern "C" void app_main(void) {
  microreader::Application app;
  Esp32Display display;
  Esp32Logger logger;
  Esp32InputSource input;
  Esp32Runtime runtime(50);
  runtime.set_input_source(&input);
  microreader::run_loop(app, runtime, display, logger);
}
