#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>

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
  void start(ILogger& logger, DisplayQueue& queue);
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
  static constexpr int kMoveStep = 10;
  CanvasRect square_{0, 0, kSquareSize, kSquareSize, /*white=*/false};
  int demo_vx_ = 10;
  int demo_vy_ = 10;
  bool demo_paused_ = true;

  // Randomly repositioning rectangles, each with its own countdown.
  static constexpr int kNumRects = 10;
  struct RandRect {
    CanvasRect rect;
    int countdown = 1;  // ticks until next move (1 = move on first update)
    int w, h;
  };
  std::array<RandRect, kNumRects> rand_rects_{
      {
       {{0, 0, 80, 80}, 1, 80, 80},
       {{0, 0, 60, 60}, 1, 60, 60},
       {{0, 0, 100, 100}, 1, 100, 100},
       {{0, 0, 50, 50}, 1, 50, 50},
       {{0, 0, 70, 40}, 1, 70, 40},
       {{0, 0, 90, 90}, 1, 90, 90},
       {{0, 0, 40, 70}, 1, 40, 70},
       {{0, 0, 55, 55}, 1, 55, 55},
       {{0, 0, 120, 45}, 1, 120, 45},
       {{0, 0, 45, 120}, 1, 45, 120},
       }
  };
};

}  // namespace microreader
