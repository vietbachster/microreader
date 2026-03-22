#include "Loop.h"

namespace microreader {

void run_loop_iteration(Application& app, DisplayQueue& queue, IRuntime& runtime, ILogger& logger) {
  const ButtonState buttons = runtime.poll_buttons();
  if (runtime.step_mode() && !runtime.consume_step()) {
    runtime.wait_next_frame();
    return;
  }
  app.update(buttons, runtime.frame_time_ms(), queue, logger);
  queue.tick();
  runtime.wait_next_frame();
}

void run_loop(Application& app, DisplayQueue& queue, IRuntime& runtime, ILogger& logger) {
  while (runtime.should_continue() && app.running())
    run_loop_iteration(app, queue, runtime, logger);
}

}  // namespace microreader
