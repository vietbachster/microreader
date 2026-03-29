#pragma once

#include <cstdint>

namespace microreader {

enum class Button : uint8_t { Button0 = 0, Button1 = 1, Button2 = 2, Button3 = 3, Up = 4, Down = 5, Power = 6 };

struct ButtonState {
  uint8_t current = 0;        // Instantaneous state at last sample
  uint8_t pressed_latch = 0;  // Accumulated rising edges since last poll

  bool is_down(Button button) const {
    const uint8_t mask = static_cast<uint8_t>(1u << static_cast<uint8_t>(button));
    return (current & mask) != 0;
  }

  bool is_pressed(Button button) const {
    const uint8_t mask = static_cast<uint8_t>(1u << static_cast<uint8_t>(button));
    return (pressed_latch & mask) != 0;
  }
};

class IInputSource {
 public:
  virtual ~IInputSource() = default;
  virtual ButtonState poll_buttons() = 0;
};

}  // namespace microreader
