#include "microreader/core/Loop.h"

namespace microreader {

void run_loop(Application& app, IRuntime& runtime, IDisplay& display, ILogger& logger) {
  app.start(logger);

  while (runtime.should_continue() && app.running()) {
    app.update(runtime.poll_buttons(), runtime.frame_time_ms(), logger);
    app.draw(display);
    runtime.wait_next_frame();
  }

  // Render one final frame for sleep/end state if needed.
  app.draw(display);
}

}  // namespace microreader
