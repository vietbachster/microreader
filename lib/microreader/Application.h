#pragma once

#include <cstdint>

#include "Canvas.h"
#include "Display.h"
#include "DisplayQueue.h"
#include "Input.h"
#include "Log.h"
#include "Runtime.h"
#include "ScreenManager.h"
#include "demos/IScreen.h"
#include "demos/MenuDemo.h"

namespace microreader {

class Application {
 public:
  Application() = default;

  const char* build_info() const;
  void start(ILogger& logger, DisplayQueue& queue);
  void update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, ILogger& logger, IRuntime& runtime);
  bool running() const;
  uint64_t tick_count() const;
  uint32_t uptime_ms() const;

 private:
  ButtonState buttons_{};
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;
  bool started_ = false;
  bool running_ = true;

  Canvas canvas_;
  ScreenManager screen_mgr_;
  MenuDemo menu_;
};

}  // namespace microreader
