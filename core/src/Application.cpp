#include "microreader/core/Application.h"

#include <cstdio>
#include <cstring>

namespace microreader {

const char* Application::build_info() const {
  return "microreader2 minimal core";
}

void Application::start(ILogger& logger) {
  ticks_ = 0;
  uptime_ms_ = 0;
  last_drawn_second_ = 0;
  frame_ = DisplayFrame{};
  status_line_.clear();
  dirty_ = true;
  started_ = true;
  running_ = true;
  logger.log(LogLevel::Info, std::string("boot: ") + build_info());
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, ILogger& logger) {
  if (!started_) {
    start(logger);
  }

  if (!running_) {
    return;
  }

  if (buttons.is_pressed(Button::Power)) {
    running_ = false;
    dirty_ = true;
    status_line_ = "power pressed -> sleep";
    logger.log(LogLevel::Info, status_line_);
    return;
  }

  ++ticks_;
  uptime_ms_ += dt_ms;

  // Mark frame dirty on first tick and once per second.
  const uint32_t current_second = uptime_ms_ / 1000;
  if (ticks_ == 1 || current_second > last_drawn_second_) {
    last_drawn_second_ = current_second;
    dirty_ = true;
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "tick=%llu uptime_ms=%lu", static_cast<unsigned long long>(ticks_),
                  static_cast<unsigned long>(uptime_ms_));
    status_line_ = buffer;
    logger.log(LogLevel::Info, status_line_);
  }
}

void Application::draw(IDisplay& display) {
  if (!dirty_) {
    return;
  }

  frame_ = DisplayFrame{};
  std::strncpy(frame_.lines[frame_.line_count].data(), build_info(), DisplayFrame::kMaxLineLength - 1);
  ++frame_.line_count;

  if (!status_line_.empty() && frame_.line_count < DisplayFrame::kMaxLines) {
    std::strncpy(frame_.lines[frame_.line_count].data(), status_line_.c_str(), DisplayFrame::kMaxLineLength - 1);
    ++frame_.line_count;
  }

  display.present(frame_, running_ ? RefreshMode::Fast : RefreshMode::Full);
  dirty_ = false;
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
