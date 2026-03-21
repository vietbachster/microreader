#include "epd.h"
#include "input.h"
#include "logger.h"
#include "microreader/Application.h"
#include "microreader/DisplayQueue.h"
#include "microreader/Loop.h"
#include "runtime.h"

extern "C" void app_main(void) {
  static Esp32Logger logger;
  static Esp32InputSource input;
  static EInkDisplay epd;
  static Esp32Runtime runtime(50);
  static microreader::Application app;
  static microreader::DisplayQueue queue(epd);

  // Wait for the serial monitor to connect before logging anything.
  // vTaskDelay(pdMS_TO_TICKS(2000));

  epd.begin();
  runtime.set_input_source(&input);

  microreader::run_loop(app, queue, runtime, epd, logger);
}
