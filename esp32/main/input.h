#pragma once

#include "microreader/core/Input.h"

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
