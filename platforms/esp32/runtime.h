#pragma once

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microreader/Input.h"
#include "microreader/Runtime.h"

class Esp32Runtime final : public microreader::IRuntime {
 public:
  explicit Esp32Runtime(uint32_t frame_time_ms) : frame_time_ms_(frame_time_ms), frame_start_ms_(0) {}

  bool should_continue() const override {
    return true;
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
      else
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    frame_start_ms_ = millis();
  }

  void yield() override {
    vTaskDelay(1);  // 1 tick (10ms at 100Hz); pdMS_TO_TICKS(1) rounds to 0
  }

 private:
  static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
  }

  uint32_t frame_time_ms_;
  uint32_t frame_start_ms_;
};
