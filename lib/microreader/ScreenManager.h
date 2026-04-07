#pragma once

#include <cstdio>

#include "display/Canvas.h"
#include "display/DisplayQueue.h"
#include "screens/IScreen.h"

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
    std::printf("[SM] push '%s' (depth %d -> %d)\n", screen->name(), depth_, depth_ + 1);
    if (depth_ > 0) {
      std::printf("[SM]   stopping '%s'\n", stack_[depth_ - 1]->name());
      stack_[depth_ - 1]->stop();
      canvas.clear();
      std::printf("[SM]   flushing queue\n");
      queue.flush();
    }
    stack_[depth_++] = screen;
    std::printf("[SM]   starting '%s'\n", screen->name());
    screen->start(canvas, queue);
    // if (depth_ > 1) {
    //   std::printf("[SM]   partial_refresh for transition\n");
    //   queue.partial_refresh();
    //   queue.cancel_settle();
    // }
    std::printf("[SM] push done\n");
  }

  // Pop the top screen. Stops it, then restarts the one below.
  void pop(Canvas& canvas, DisplayQueue& queue) {
    if (depth_ == 0)
      return;
    std::printf("[SM] pop '%s' (depth %d -> %d)\n", stack_[depth_ - 1]->name(), depth_, depth_ - 1);
    stack_[--depth_]->stop();
    canvas.clear();
    std::printf("[SM]   flushing queue\n");
    queue.flush();
    if (depth_ > 0) {
      std::printf("[SM]   starting '%s'\n", stack_[depth_ - 1]->name());
      stack_[depth_ - 1]->start(canvas, queue);
      // std::printf("[SM]   partial_refresh for transition\n");
      // queue.partial_refresh();
      // queue.cancel_settle();
    }
    std::printf("[SM] pop done\n");
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
