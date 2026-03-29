#pragma once

#include "Canvas.h"
#include "DisplayQueue.h"
#include "demos/IScreen.h"

namespace microreader {

// Stack-based screen manager. Screens are pushed/popped; only the top screen
// is active (receives start/stop and update calls).
class ScreenManager {
 public:
  static constexpr int kMaxDepth = 8;

  // Push a new screen. Stops the current top, starts the new one.
  void push(IScreen* screen, Canvas& canvas, DisplayQueue& queue) {
    if (depth_ >= kMaxDepth)
      return;
    if (depth_ > 0) {
      stack_[depth_ - 1]->stop();
      canvas.clear();
      queue.flush();
    }
    stack_[depth_++] = screen;
    screen->start(canvas, queue);
    if (depth_ > 1)
      queue.partial_refresh();
  }

  // Pop the top screen. Stops it, then restarts the one below.
  void pop(Canvas& canvas, DisplayQueue& queue) {
    if (depth_ == 0)
      return;
    stack_[--depth_]->stop();
    canvas.clear();
    queue.flush();
    if (depth_ > 0) {
      stack_[depth_ - 1]->start(canvas, queue);
      queue.partial_refresh();
    }
  }

  // Restart the top screen (stop + clear + start).
  void restart_top(Canvas& canvas, DisplayQueue& queue) {
    if (depth_ == 0)
      return;
    stack_[depth_ - 1]->stop();
    canvas.clear();
    stack_[depth_ - 1]->start(canvas, queue);
  }

  IScreen* top() const {
    return depth_ > 0 ? stack_[depth_ - 1] : nullptr;
  }
  int depth() const {
    return depth_;
  }
  bool empty() const {
    return depth_ == 0;
  }

 private:
  IScreen* stack_[kMaxDepth] = {};
  int depth_ = 0;
};

}  // namespace microreader
