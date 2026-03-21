#pragma once

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microreader/Input.h"
#include "microreader/Loop.h"

class Esp32Runtime final : public microreader::IRuntime {
 public:
  explicit Esp32Runtime(uint32_t frame_time_ms) : frame_time_ms_(frame_time_ms), input_(nullptr), frame_start_ms_(0) {}

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
    const uint32_t now = millis();
    if (frame_start_ms_ != 0) {
      const uint32_t elapsed = now - frame_start_ms_;
      if (elapsed < frame_time_ms_)
        vTaskDelay(pdMS_TO_TICKS(frame_time_ms_ - elapsed));
    }
    frame_start_ms_ = millis();
  }

 private:
  static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
  }

  uint32_t frame_time_ms_;
  microreader::IInputSource* input_;
  uint32_t frame_start_ms_;
};
