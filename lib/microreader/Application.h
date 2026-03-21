#pragma once

#include <cstdint>

#include "Canvas.h"
#include "Display.h"
#include "DisplayQueue.h"
#include "Input.h"
#include "Log.h"

namespace microreader {

class Application {
 public:
  Application() = default;

  const char* build_info() const;
  void start(ILogger& logger);
  // Update app logic and submit any display commands to the controller.
  void update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, ILogger& logger);
  bool running() const;
  uint64_t tick_count() const;
  uint32_t uptime_ms() const;

 private:
  ButtonState buttons_{};
  Rotation rotation_ = Rotation::Deg0;
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;
  bool dirty_ = true;
  bool started_ = false;
  bool running_ = true;

  // Demo: bouncing square.
  static constexpr int kSquareSize = 60;
  static constexpr int kMoveStep = 30;
  CanvasRect square_{0, 0, kSquareSize, kSquareSize, /*white=*/false};
  int demo_vx_ = 30;
  int demo_vy_ = 30;
  bool demo_paused_ = true;
};

}  // namespace microreader
