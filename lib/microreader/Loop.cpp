#include "Loop.h"

namespace microreader {

void run_loop(Application& app, DisplayQueue& queue, IRuntime& runtime, IDisplay& display, ILogger& logger) {
  app.start(logger);

  while (runtime.should_continue() && app.running()) {
    const ButtonState buttons = runtime.poll_buttons();  // always pump events
    if (runtime.step_mode() && !runtime.consume_step()) {
      runtime.wait_next_frame();
      continue;
    }
    app.update(buttons, runtime.frame_time_ms(), queue, logger);
    queue.tick();
    display.tick();
    runtime.wait_next_frame();
  }
}

}  // namespace microreader
