#include "TextShowcaseDemo.h"

#include <cstring>

namespace microreader {

void TextShowcaseDemo::layout_(Canvas& canvas, DisplayQueue& queue) {
  const int W = queue.width();
  const int H = queue.height();
  queue.submit(0, 0, W, H, true);

  const int title_x = (W - title_.text_width()) / 2;
  title_.set_position(title_x, 8);
  canvas.add(&title_);

  // Sample lines at the current scale.
  static const char* samples[] = {
      "ABCDEFGHIJKLM", "nopqrstuvwxyz", "0123456789!@#", "The quick brown", "fox jumps over", "the lazy dog.",
  };

  line_count_ = 0;
  const int glyph_h = CanvasText::kGlyphH * scale_;
  const int glyph_w = CanvasText::kGlyphW * scale_;
  const int spacing = glyph_h + 6;
  const int start_y = 8 + CanvasText::kGlyphH * 2 + 16;

  for (int i = 0; i < kMaxLines; ++i) {
    const int y = start_y + i * spacing;
    if (y + glyph_h > H)
      break;
    const int len = static_cast<int>(std::strlen(samples[i]));
    const int x = (W - len * glyph_w) / 2;
    lines_[i] = CanvasText(x, y, samples[i], false, scale_);
    canvas.add(&lines_[i]);
    ++line_count_;
  }

  canvas.commit(queue);
}

void TextShowcaseDemo::start(Canvas& canvas, DisplayQueue& queue) {
  layout_(canvas, queue);
}

void TextShowcaseDemo::stop() {}

bool TextShowcaseDemo::update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime&) {
  if (buttons.is_pressed(Button::Button0))
    return false;

  if (buttons.is_pressed(Button::Up)) {
    scale_ = scale_ < 4 ? scale_ + 1 : 1;
    stop();
    canvas.clear();
    layout_(canvas, queue);
  }

  return true;
}

}  // namespace microreader
