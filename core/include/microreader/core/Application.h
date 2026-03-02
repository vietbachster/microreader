#pragma once

#include <cstdint>

#include "microreader/core/Display.h"
#include "microreader/core/Input.h"
#include "microreader/core/Log.h"

namespace microreader {

class Application {
 public:
  Application() = default;

  const char* build_info() const;
  void start(ILogger& logger);
  void update(const ButtonState& buttons, uint32_t dt_ms, ILogger& logger);
  void draw(IDisplay& display);
  bool running() const;
  uint64_t tick_count() const;
  uint32_t uptime_ms() const;

 private:
  // Pixel buffer owned here; DisplayFrame is a non-owning view into it.
  alignas(4) uint8_t frame_pixels_[DisplayFrame::kPixelBytes]{};
  DisplayFrame frame_{frame_pixels_};
  ButtonState buttons_{};
  Rotation rotation_ = Rotation::Deg0;
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;
  bool dirty_ = true;
  bool started_ = false;
  bool running_ = true;
};

}  // namespace microreader
