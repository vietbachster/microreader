#pragma once

#include <cstdint>
#include <string>

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
  DisplayFrame frame_;
  std::string status_line_;
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;
  uint32_t last_drawn_second_ = 0;
  bool dirty_ = true;
  bool started_ = false;
  bool running_ = true;
};

}  // namespace microreader
