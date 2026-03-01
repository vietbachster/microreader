#include "microreader/core/Application.h"

#include "microreader/core/Font.h"

namespace microreader {

const char* Application::build_info() const {
  return "microreader";
}

void Application::start(ILogger& logger) {
  ticks_ = 0;
  uptime_ms_ = 0;
  frame_ = DisplayFrame{};
  buttons_ = ButtonState{};
  dirty_ = true;
  started_ = true;
  running_ = true;
  logger.log(LogLevel::Info, build_info());
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, ILogger& logger) {
  if (!started_) {
    start(logger);
  }

  if (!running_) {
    return;
  }

  ++ticks_;
  uptime_ms_ += dt_ms;

  // Redraw whenever the held button set changes.
  if (buttons.current != buttons_.current) {
    dirty_ = true;
  }
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    running_ = false;
    dirty_ = true;
    logger.log(LogLevel::Info, "power pressed -> sleep");
    return;
  }

  // Cycle rotation on Up/Down press.
  if (buttons_.is_pressed(Button::Up) || buttons_.is_pressed(Button::Down)) {
    const int step = buttons_.is_pressed(Button::Up) ? 1 : -1;
    const int deg = (static_cast<int>(rotation_) + step * 90 + 180) % 180;
    rotation_ = static_cast<Rotation>(deg);
    dirty_ = true;
  }
}

void Application::draw(IDisplay& display) {
  if (!dirty_) {
    return;
  }

  frame_.fill(false);
  draw_text(frame_, 8, 8, build_info());

  // Apply rotation whenever it differs from the display's current setting.
  if (display.rotation() != rotation_) {
    display.set_rotation(rotation_);
  }

  // Build a space-separated list of currently held button names.
  char held[64] = {};
  char* p = held;
  auto append = [&](const char* name) {
    if (p != held) {
      *p++ = ' ';
    }
    while (*name)
      *p++ = *name++;
  };
  if (buttons_.is_down(Button::Up))
    append("Up");
  if (buttons_.is_down(Button::Down))
    append("Down");
  if (buttons_.is_down(Button::Button0))
    append("B0");
  if (buttons_.is_down(Button::Button1))
    append("B1");
  if (buttons_.is_down(Button::Button2))
    append("B2");
  if (buttons_.is_down(Button::Button3))
    append("B3");
  if (buttons_.is_down(Button::Power))
    append("Power");
  if (p != held) {
    draw_text(frame_, 8, 24, held);
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
