#include "BouncingBallDemo.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "../display/Font.h"

namespace microreader {

void BouncingBallDemo::start(Canvas& canvas, DisplayQueue& queue) {
  const int W = queue.width();
  const int H = queue.height();
  queue.submit(0, 0, W, H, /*white=*/true);
  for (auto& r : rand_rects_)
    canvas.add(&r.rect);
  for (auto& c : rand_circles_)
    canvas.add(&c.circle);
  canvas.add(&ball_);
  for (auto& t : rand_texts_)
    canvas.add(&t.label);

  for (auto& r : rand_rects_) {
    const int rx = std::rand() % (W - r.w);
    const int ry = std::rand() % (H - r.h);
    r.rect.set_position(rx, ry);
    r.countdown = 25 + std::rand() % 76;
  }
  for (auto& c : rand_circles_) {
    const int cx = c.radius + std::rand() % (W - 2 * c.radius);
    const int cy = c.radius + std::rand() % (H - 2 * c.radius);
    c.circle.set_position(cx, cy);
    c.countdown = 25 + std::rand() % 76;
  }
  for (auto& t : rand_texts_) {
    const int len = static_cast<int>(std::strlen(t.label.text()));
    const int tw = len * CanvasText::kGlyphW;
    const int tx = std::rand() % std::max(1, W - tw);
    const int ty = std::rand() % std::max(1, H - CanvasText::kGlyphH);
    t.label.set_position(tx, ty);
    t.countdown = 25 + std::rand() % 76;
  }
  canvas.commit(queue);
}

void BouncingBallDemo::stop() {}

bool BouncingBallDemo::update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& /*runtime*/) {
  // Button0 = back to menu.
  if (buttons.is_pressed(Button::Button0))
    return false;

  // Up toggles pause/auto-bounce.
  if (buttons.is_pressed(Button::Up))
    demo_paused_ = !demo_paused_;

  // Auto-bounce.
  int cx = ball_.cx() + demo_vx_;
  int cy = ball_.cy() + demo_vy_;
  const int W = queue.width();
  const int H = queue.height();

  if (cx <= kBallRadius) {
    cx = kBallRadius;
    demo_vx_ = -demo_vx_;
  }
  if (cx >= W - kBallRadius - 1) {
    cx = W - kBallRadius - 1;
    demo_vx_ = -demo_vx_;
  }
  if (cy <= kBallRadius) {
    cy = kBallRadius;
    demo_vy_ = -demo_vy_;
  }
  if (cy >= H - kBallRadius - 1) {
    cy = H - kBallRadius - 1;
    demo_vy_ = -demo_vy_;
  }

  ball_.set_position(cx, cy);

  for (auto& r : rand_rects_) {
    if (--r.countdown <= 0) {
      const int rx = std::rand() % (W - r.w);
      const int ry = std::rand() % (H - r.h);
      r.rect.set_position(rx, ry);
      r.countdown = 25 + std::rand() % 76;
    }
  }

  for (auto& c : rand_circles_) {
    if (--c.countdown <= 0) {
      const int cx_ = c.radius + std::rand() % (W - 2 * c.radius);
      const int cy_ = c.radius + std::rand() % (H - 2 * c.radius);
      c.circle.set_position(cx_, cy_);
      c.countdown = 25 + std::rand() % 76;
    }
  }

  for (auto& t : rand_texts_) {
    if (--t.countdown <= 0) {
      const int len = static_cast<int>(std::strlen(t.label.text()));
      const int tw = len * CanvasText::kGlyphW;
      const int tx = std::rand() % std::max(1, W - tw);
      const int ty = std::rand() % std::max(1, H - CanvasText::kGlyphH);
      t.label.set_position(tx, ty);
      t.countdown = 25 + std::rand() % 76;
    }
  }

  canvas.commit(queue);
  return true;
}

}  // namespace microreader
