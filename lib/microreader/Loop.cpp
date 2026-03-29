#include "Loop.h"

namespace microreader {

void run_loop_iteration(Application& app, DisplayQueue& queue, IInputSource& input, IRuntime& runtime,
                        ILogger& logger) {
  const ButtonState buttons = input.poll_buttons();
  if (runtime.step_mode() && !runtime.consume_step()) {
    runtime.wait_next_frame();
    return;
  }
  app.update(buttons, runtime.frame_time_ms(), queue, logger, runtime);
  queue.tick();
  runtime.wait_next_frame();
}

void run_loop(Application& app, DisplayQueue& queue, IInputSource& input, IRuntime& runtime, ILogger& logger) {
  while (runtime.should_continue() && app.running())
    run_loop_iteration(app, queue, input, runtime, logger);
}

}  // namespace microreader
