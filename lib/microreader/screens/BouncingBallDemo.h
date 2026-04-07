#pragma once

#include <array>
#include <cstdlib>

#include "../Input.h"
#include "../display/Canvas.h"
#include "../display/Display.h"
#include "../display/DisplayQueue.h"
#include "IScreen.h"

namespace microreader {

class BouncingBallDemo final : public IScreen {
 public:
  BouncingBallDemo() = default;

  const char* name() const override {
    return "Bouncing Ball";
  }

  void start(Canvas& canvas, DisplayQueue& queue) override;
  void stop() override;
  bool update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& runtime) override;

 private:
  // Bouncing ball.
  static constexpr int kBallRadius = 30;
  static constexpr int kMoveStep = 10;
  CanvasCircle ball_{kBallRadius, kBallRadius, kBallRadius, /*white=*/false};
  int demo_vx_ = 5;
  int demo_vy_ = 5;
  bool demo_paused_ = true;

  // Randomly repositioning rectangles, each with its own countdown.
  static constexpr int kNumRects = 10;
  struct RandRect {
    CanvasRect rect;
    int countdown = 1;
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

  // Randomly repositioning circles, each with its own countdown.
  static constexpr int kNumCircles = 5;
  struct RandCircle {
    CanvasCircle circle;
    int countdown = 1;
    int radius;
  };
  std::array<RandCircle, kNumCircles> rand_circles_{
      {
       {{0, 0, 40}, 1, 40},
       {{0, 0, 25}, 1, 25},
       {{0, 0, 55}, 1, 55},
       {{0, 0, 35}, 1, 35},
       {{0, 0, 50}, 1, 50},
       }
  };

  // Randomly repositioning text labels.
  static constexpr int kNumTexts = 5;
  struct RandText {
    CanvasText label;
    int countdown = 1;
  };
  std::array<RandText, kNumTexts> rand_texts_{
      {
       {{0, 0, "Hello"}, 1},
       {{0, 0, "World"}, 1},
       {{0, 0, "ePaper"}, 1},
       {{0, 0, "micro"}, 1},
       {{0, 0, "reader"}, 1},
       }
  };
};

}  // namespace microreader
