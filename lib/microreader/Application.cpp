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

  // Randomize all rect positions and commit every primitive so the initial
  // full refresh starts from a fully initialized frame.
  for (auto& r : rand_rects_) {
    const int rx = std::rand() % (DisplayFrame::kPhysicalWidth - r.w);
    const int ry = std::rand() % (DisplayFrame::kPhysicalHeight - r.h);
    r.rect.set_position(rx, ry);
    r.rect.commit(queue);
    r.countdown = 25 + std::rand() % 76;
  }
  square_.commit(queue);
  queue.full_refresh();
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, ILogger& logger) {
  if (!started_)
    start(logger, queue);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    running_ = false;
    return;
  }

  // --- Demo: bouncing black square on a white background ---
  // Button4 (Up) toggles pause/auto-bounce.
  if (buttons_.is_pressed(Button::Up))
    demo_paused_ = !demo_paused_;
  if (buttons_.is_pressed(Button::Down)) {
    queue.clear_screen(clear_white_, RefreshMode::Full);
    clear_white_ = !clear_white_;
  }

  if (demo_paused_) {
    // Manual movement: Button0=left, Button1=right, Button2=up, Button3=down.
    int x = square_.x();
    int y = square_.y();
    if (buttons_.is_down(Button::Button0))
      y += kMoveStep;
    if (buttons_.is_down(Button::Button1))
      y -= kMoveStep;
    if (buttons_.is_down(Button::Button2))
      x += kMoveStep;
    if (buttons_.is_down(Button::Button3))
      x -= kMoveStep;
    x = std::max(0, std::min(x, DisplayFrame::kPhysicalWidth - kSquareSize));
    y = std::max(0, std::min(y, DisplayFrame::kPhysicalHeight - kSquareSize));
    square_.set_position(x, y);
    square_.commit(queue);
    return;
  }

  // Auto-bounce.
  int x = square_.x() + demo_vx_;
  int y = square_.y() + demo_vy_;

  // Bounce off edges.
  const int max_x = DisplayFrame::kPhysicalWidth - kSquareSize;
  const int max_y = DisplayFrame::kPhysicalHeight - kSquareSize;
  if (x <= 0) {
    x = 0;
    demo_vx_ = -demo_vx_;
  }
  if (x >= max_x) {
    x = max_x;
    demo_vx_ = -demo_vx_;
  }
  if (y <= 0) {
    y = 0;
    demo_vy_ = -demo_vy_;
  }
  if (y >= max_y) {
    y = max_y;
    demo_vy_ = -demo_vy_;
  }

  square_.set_position(x, y);
  // square_.set_visible(!square_.visible());
  square_.commit(queue);

  // Each rect has its own countdown; when it hits zero, move it and reset.
  for (auto& r : rand_rects_) {
    if (--r.countdown <= 0) {
      const int rx = std::rand() % (DisplayFrame::kPhysicalWidth - r.w);
      const int ry = std::rand() % (DisplayFrame::kPhysicalHeight - r.h);
      r.rect.set_position(rx, ry);
      r.rect.commit(queue);
      r.countdown = 25 + std::rand() % 76;  // [25, 100]
    }
  }
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
