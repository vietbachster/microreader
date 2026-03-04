#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microreader/Input.h"
#include "microreader/Loop.h"

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
