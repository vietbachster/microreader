#include "Demo.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "Font.h"

namespace microreader {

void Demo::start(Canvas& canvas, DisplayQueue& queue) {
  // Register all elements with the canvas (z-order: rects, circles, ball on top).
  for (auto& r : rand_rects_)
    canvas.add(&r.rect);
  for (auto& c : rand_circles_)
    canvas.add(&c.circle);
  canvas.add(&ball_);
  for (auto& t : rand_texts_)
    canvas.add(&t.label);

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
  canvas.commit(queue);
  queue.full_refresh();
}

void Demo::update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue) {
  // --- Demo: bouncing black ball on a white background ---
  // Button4 (Up) toggles pause/auto-bounce.
  if (buttons.is_pressed(Button::Up))
    demo_paused_ = !demo_paused_;
  if (buttons.is_pressed(Button::Down))
    queue.display_deep_sleep();
  if (buttons.is_down(Button::Button0))
    queue.clear_screen();
  if (buttons.is_down(Button::Button1))
    queue.clear_screen(false);

  if (demo_paused_) {
    // Manual movement: Button0=left, Button1=right, Button2=up, Button3=down.
    int cx = ball_.cx();
    int cy = ball_.cy();
    if (buttons.is_down(Button::Button2))
      cx += kMoveStep;
    if (buttons.is_down(Button::Button3))
      cx -= kMoveStep;
    cx = std::max(kBallRadius, std::min(cx, DisplayFrame::kPhysicalWidth - kBallRadius - 1));
    cy = std::max(kBallRadius, std::min(cy, DisplayFrame::kPhysicalHeight - kBallRadius - 1));
    ball_.set_position(cx, cy);
    canvas.commit(queue);
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
      const int cx_ = c.radius + std::rand() % (DisplayFrame::kPhysicalWidth - 2 * c.radius);
      const int cy_ = c.radius + std::rand() % (DisplayFrame::kPhysicalHeight - 2 * c.radius);
      c.circle.set_position(cx_, cy_);
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

  canvas.commit(queue);
}

}  // namespace microreader
