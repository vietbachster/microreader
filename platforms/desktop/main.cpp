#include <iostream>

#include "display.h"
#include "logger.h"
#include "microreader/Application.h"
#include "microreader/DisplayQueue.h"
#include "microreader/Loop.h"
#include "runtime.h"

int main() {
  try {
    DesktopLogger logger;
    DesktopRuntime runtime(30);
    DesktopEmulatorDisplay display(runtime);
    microreader::Application app;
    microreader::DisplayQueue queue(display);
    runtime.set_transition_toggle(&display.show_transitions);
    display.set_phases_source(&queue.phases);
    microreader::run_loop(app, queue, runtime, display, logger);
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
