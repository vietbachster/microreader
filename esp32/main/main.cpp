#include "display.h"
#include "input.h"
#include "logger.h"
#include "microreader/core/Application.h"
#include "microreader/core/Loop.h"
#include "runtime.h"

extern "C" void app_main(void) {
  microreader::Application app;
  Esp32Display display;
  Esp32Logger logger;
  Esp32InputSource input;
  Esp32Runtime runtime(50);
  runtime.set_input_source(&input);
  microreader::run_loop(app, runtime, display, logger);
}
