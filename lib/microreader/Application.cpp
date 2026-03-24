#include "Application.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

#include "Font.h"

namespace microreader {

const char* Application::build_info() const {
  return "microreader";
}

void Application::start(ILogger& logger, DisplayQueue& queue) {
  ticks_ = 0;
  uptime_ms_ = 0;
  buttons_ = ButtonState{};
  dirty_ = true;
  started_ = true;
  running_ = true;
  logger.log(LogLevel::Info, build_info());
#ifdef ESP_PLATFORM
  std::srand(esp_random());
#else
  std::srand(static_cast<unsigned>(std::time(nullptr)));
#endif

  // lut_calibration_.start(queue);
  demo_.start(canvas_, queue);
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, ILogger& logger,
                         IRuntime& runtime) {
  if (!started_)
    start(logger, queue);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    queue.clear_screen(/*white=*/true, RefreshMode::Full);
    running_ = false;
    return;
  }

  // lut_calibration_.update(buttons_, queue, runtime);
  demo_.update(buttons_, canvas_, queue);
}

bool Application::running() const {
  return running_;
}
uint64_t Application::tick_count() const {
  return ticks_;
}
uint32_t Application::uptime_ms() const {
  return uptime_ms_;
}

}  // namespace microreader
