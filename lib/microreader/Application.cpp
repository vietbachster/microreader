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

  // Register all elements with the canvas (z-order: rects, circles, ball on top).
  for (auto& r : rand_rects_)
    canvas_.add(&r.rect);
  for (auto& c : rand_circles_)
    canvas_.add(&c.circle);
  canvas_.add(&ball_);
  for (auto& t : rand_texts_)
    canvas_.add(&t.label);

  // Randomize positions.
  for (auto& r : rand_rects_) {
    const int rx = std::rand() % (DisplayFrame::kPhysicalWidth - r.w);
    const int ry = std::rand() % (DisplayFrame::kPhysicalHeight - r.h);
    r.rect.set_position(rx, ry);
    r.countdown = 25 + std::rand() % 76;
  }
  for (auto& c : rand_circles_) {
    const int cx = c.radius + std::rand() % (DisplayFrame::kPhysicalWidth - 2 * c.radius);
    const int cy = c.radius + std::rand() % (DisplayFrame::kPhysicalHeight - 2 * c.radius);
    c.circle.set_position(cx, cy);
    c.countdown = 25 + std::rand() % 76;
  }
  for (auto& t : rand_texts_) {
    const int len = static_cast<int>(std::strlen(t.label.text()));
    const int tw = len * CanvasText::kGlyphW;
    const int tx = std::rand() % std::max(1, DisplayFrame::kPhysicalWidth - tw);
    const int ty = std::rand() % std::max(1, DisplayFrame::kPhysicalHeight - CanvasText::kGlyphH);
    t.label.set_position(tx, ty);
    t.countdown = 25 + std::rand() % 76;
  }
  canvas_.commit(queue);
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

  // --- Demo: bouncing black ball on a white background ---
  // Button4 (Up) toggles pause/auto-bounce.
  if (buttons_.is_pressed(Button::Up))
    demo_paused_ = !demo_paused_;
  if (buttons_.is_pressed(Button::Down)) {
    queue.clear_screen(clear_white_, RefreshMode::Full);
    clear_white_ = !clear_white_;
  }

  if (demo_paused_) {
    // Manual movement: Button0=left, Button1=right, Button2=up, Button3=down.
    int cx = ball_.cx();
    int cy = ball_.cy();
    if (buttons_.is_down(Button::Button0))
      cy += kMoveStep;
    if (buttons_.is_down(Button::Button1))
      cy -= kMoveStep;
    if (buttons_.is_down(Button::Button2))
      cx += kMoveStep;
    if (buttons_.is_down(Button::Button3))
      cx -= kMoveStep;
    cx = std::max(kBallRadius, std::min(cx, DisplayFrame::kPhysicalWidth - kBallRadius - 1));
    cy = std::max(kBallRadius, std::min(cy, DisplayFrame::kPhysicalHeight - kBallRadius - 1));
    ball_.set_position(cx, cy);
    canvas_.commit(queue);
    return;
  }

  // Auto-bounce.
  int cx = ball_.cx() + demo_vx_;
  int cy = ball_.cy() + demo_vy_;

  // Bounce off edges.
  if (cx <= kBallRadius) {
    cx = kBallRadius;
    demo_vx_ = -demo_vx_;
  }
  if (cx >= DisplayFrame::kPhysicalWidth - kBallRadius - 1) {
    cx = DisplayFrame::kPhysicalWidth - kBallRadius - 1;
    demo_vx_ = -demo_vx_;
  }
  if (cy <= kBallRadius) {
    cy = kBallRadius;
    demo_vy_ = -demo_vy_;
  }
  if (cy >= DisplayFrame::kPhysicalHeight - kBallRadius - 1) {
    cy = DisplayFrame::kPhysicalHeight - kBallRadius - 1;
    demo_vy_ = -demo_vy_;
  }

  ball_.set_position(cx, cy);

  // Each rect has its own countdown; when it hits zero, move it and reset.
  for (auto& r : rand_rects_) {
    if (--r.countdown <= 0) {
      const int rx = std::rand() % (DisplayFrame::kPhysicalWidth - r.w);
      const int ry = std::rand() % (DisplayFrame::kPhysicalHeight - r.h);
      r.rect.set_position(rx, ry);
      r.countdown = 25 + std::rand() % 76;  // [25, 100]
    }
  }

  // Each circle has its own countdown; when it hits zero, move it and reset.
  for (auto& c : rand_circles_) {
    if (--c.countdown <= 0) {
      const int cx = c.radius + std::rand() % (DisplayFrame::kPhysicalWidth - 2 * c.radius);
      const int cy = c.radius + std::rand() % (DisplayFrame::kPhysicalHeight - 2 * c.radius);
      c.circle.set_position(cx, cy);
      c.countdown = 25 + std::rand() % 76;  // [25, 100]
    }
  }

  // Each text label has its own countdown; when it hits zero, move it and reset.
  for (auto& t : rand_texts_) {
    if (--t.countdown <= 0) {
      const int len = static_cast<int>(std::strlen(t.label.text()));
      const int tw = len * CanvasText::kGlyphW;
      const int tx = std::rand() % std::max(1, DisplayFrame::kPhysicalWidth - tw);
      const int ty = std::rand() % std::max(1, DisplayFrame::kPhysicalHeight - CanvasText::kGlyphH);
      t.label.set_position(tx, ty);
      t.countdown = 25 + std::rand() % 76;  // [25, 100]
    }
  }

  canvas_.commit(queue);
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
