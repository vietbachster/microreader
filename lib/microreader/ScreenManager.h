#pragma once

#include <cstdio>

#include "HeapLog.h"
#include "display/DrawBuffer.h"
#include "screens/IScreen.h"

namespace microreader {

// Stack-based screen manager. Screens are pushed/popped; only the top screen
// is active (receives start/stop and update calls).
class ScreenManager {
 public:
  static constexpr int kMaxDepth = 8;

  // Push a new screen. Stops the current top, starts the new one.
  // The new screen draws into buf; caller handles the actual refresh.
  void push(IScreen* screen, DrawBuffer& buf) {
    if (depth_ >= kMaxDepth)
      return;
    if (depth_ > 0)
      stack_[depth_ - 1]->stop();
    stack_[depth_++] = screen;
    screen->start(buf);
  }

  // Pop the top screen. Stops it, then restarts the one below.
  void pop(DrawBuffer& buf) {
    if (depth_ == 0)
      return;
    stack_[--depth_]->stop();
    HEAP_LOG("pop: after stop");
    if (depth_ > 0) {
      stack_[depth_ - 1]->start(buf);
      HEAP_LOG("pop: after prev start");
    }
  }

  // Restart the top screen (stop + start).
  void restart_top(DrawBuffer& buf) {
    if (depth_ == 0)
      return;
    stack_[depth_ - 1]->stop();
    stack_[depth_ - 1]->start(buf);
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
