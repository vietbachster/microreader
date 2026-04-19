#include "GrayscaleDemo.h"

#include <cstring>

namespace microreader {

void GrayscaleDemo::draw_bw_(DrawBuffer& buf) const {
  static constexpr int W = DrawBuffer::kWidth;

  buf.fill(true);

  const int x = (W - kRectW) / 2;
  const int total_h = kNumRects * kRectH + (kNumRects - 1) * kSpacing;
  const int base_y = (DrawBuffer::kHeight - total_h) / 2 + offset_y_;

  // Labels for the 5 levels.
  const char* labels[] = {"White", "Light", "Gray", "Dark", "Black"};

  // Draw rects as black EXCEPT White (0) and Light (1) — those start white.
  // LUT 00 = no change, so white rects stay white, black rects stay black.
  for (int i = 0; i < kNumRects; ++i) {
    int y = base_y + i * (kRectH + kSpacing);
    if (i >= 2)  // skip White and Light in BW pass
      buf.fill_rect(x, y, kRectW, kRectH, false);
    buf.draw_text(x + kRectW + 12, y + kRectH / 2 - 4, labels[i], true, 1);
  }

  buf.draw_text(12, 12, "Btn1: Toggle Gray  Up/Dn: Move", true, 1);
}

void GrayscaleDemo::apply_grayscale_(DrawBuffer& buf) const {
  static constexpr int W = DrawBuffer::kWidth;

  const int x = (W - kRectW) / 2;
  const int total_h = kNumRects * kRectH + (kNumRects - 1) * kSpacing;
  const int base_y = (DrawBuffer::kHeight - total_h) / 2 + offset_y_;

  // Grayscale encoding: LUT entry = {BW_RAM bit, RED_RAM bit}
  //   00 = no change (stays as BW pass left it)
  //   01 = light gray
  //   10 = gray
  //   11 = dark gray
  //
  // Rect 0 (White):      not in BW, LSB=0 MSB=0 → no change → stays white
  // Rect 1 (Light gray): not in BW, LSB=1 MSB=0 → 01 → light gray (from white)
  // Rect 2 (Gray):        in BW,    LSB=0 MSB=1 → 10 → gray (from black)
  // Rect 3 (Dark gray):   in BW,    LSB=1 MSB=1 → 11 → dark gray (from black)
  // Rect 4 (Black):       in BW,    LSB=0 MSB=0 → no change → stays black

  auto rect_y = [&](int i) { return base_y + i * (kRectH + kSpacing); };

  // LSB plane → BW RAM
  buf.fill(false);                                    // clear to 0x00
  buf.fill_rect(x, rect_y(1), kRectW, kRectH, true);  // Light gray: LSB=1
  buf.fill_rect(x, rect_y(3), kRectW, kRectH, true);  // Dark gray:  LSB=1
  buf.write_ram_bw();

  // MSB plane → RED RAM
  buf.fill(false);                                    // clear to 0x00
  buf.fill_rect(x, rect_y(2), kRectW, kRectH, true);  // Gray:       MSB=1
  buf.fill_rect(x, rect_y(3), kRectW, kRectH, true);  // Dark gray:  MSB=1
  buf.write_ram_red();

  buf.grayscale_refresh();
}

void GrayscaleDemo::start(DrawBuffer& buf) {
  offset_y_ = 0;
  draw_bw_(buf);
}

void GrayscaleDemo::stop() {}

bool GrayscaleDemo::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (buttons.is_pressed(Button::Button0))
    return false;

  // Access display's grayscale state via DrawBuffer
  bool is_gray = buf.display().in_grayscale_mode();
  if (buttons.is_pressed(Button::Button1)) {
    if (is_gray) {
      // draw_bw_(buf);
      // buf.refresh();
      buf.revert_grayscale();
    } else {
      apply_grayscale_(buf);
    }
    return true;
  }

  bool moved = false;
  if (buttons.is_pressed(Button::Button2)) {
    offset_y_ += 20;
    moved = true;
  }
  if (buttons.is_pressed(Button::Button3)) {
    offset_y_ -= 20;
    moved = true;
  }

  if (buttons.is_pressed(Button::Up)) {
    draw_bw_(buf);
    buf.full_refresh();
  }

  if (moved) {
    draw_bw_(buf);
    buf.refresh();  // partial BW refresh (auto-reverts grayscale if active)
  }

  return true;
}

}  // namespace microreader
