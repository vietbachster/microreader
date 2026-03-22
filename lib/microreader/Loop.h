#pragma once

#include "microreader/Application.h"
#include "microreader/DisplayQueue.h"
#include "microreader/Log.h"

namespace microreader {

class IRuntime {
 public:
  virtual ~IRuntime() = default;

  virtual bool should_continue() const = 0;
  virtual ButtonState poll_buttons() = 0;
  virtual uint32_t frame_time_ms() const = 0;
  virtual void wait_next_frame() = 0;

  // Optional step-mode support for debugging.
  // step_mode() returns true when the loop should pause between ticks.
  // consume_step() returns true (exactly once per press) when the user
  // has requested one tick to advance.  Default: always run freely.
  virtual bool step_mode() const {
    return false;
  }
  virtual bool consume_step() {
    return false;
  }
};

void run_loop(Application& app, DisplayQueue& queue, IRuntime& runtime, ILogger& logger);
void run_loop_iteration(Application& app, DisplayQueue& queue, IRuntime& runtime, ILogger& logger);

}  // namespace microreader
