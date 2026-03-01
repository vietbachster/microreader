#pragma once

#include "microreader/core/Application.h"
#include "microreader/core/Log.h"

namespace microreader {

class IRuntime {
 public:
  virtual ~IRuntime() = default;

  virtual bool should_continue() const = 0;
  virtual ButtonState poll_buttons() = 0;
  virtual uint32_t frame_time_ms() const = 0;
  virtual void wait_next_frame() = 0;
};

void run_loop(Application& app, IRuntime& runtime, IDisplay& display, ILogger& logger);

}  // namespace microreader
