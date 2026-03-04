#include <iostream>

#include "display.h"
#include "logger.h"
#include "microreader/Application.h"
#include "microreader/Loop.h"
#include "runtime.h"

int main() {
  try {
    DesktopLogger logger;
    DesktopRuntime runtime(50);
    DesktopEmulatorDisplay display(runtime);
    microreader::Application app;
    microreader::run_loop(app, runtime, display, logger);
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
