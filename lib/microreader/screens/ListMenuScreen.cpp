#include "ListMenuScreen.h"

#include <cstring>

#include "../display/ui_font_header.h"
#include "../display/ui_font_small.h"

namespace microreader {

void ListMenuScreen::start(DrawBuffer& buf) {
  if (!ui_font_.valid())
    ui_font_.init(kFontData_ui_small_mbf, kFontData_ui_small_mbf_size);
  if (!header_font_.valid())
    header_font_.init(kFontData_ui_header_mbf, kFontData_ui_header_mbf_size);
  chosen_ = nullptr;
  clear_items();
  on_start();
  ensure_visible_();
  draw_all_(buf);
}

void ListMenuScreen::ensure_visible_() {
  if (!ui_font_.valid() || count() == 0)
    return;
  const int line_h = ui_font_.y_advance() + 8;
  const int header_h = 35 + (header_font_.valid() ? header_font_.y_advance() : 0) + 8;
  const int visible = (DrawBuffer::kHeight - header_h) / line_h;
  if (visible <= 0)
    return;
  if (selected_ < scroll_offset_)
    scroll_offset_ = selected_;
  else if (selected_ >= scroll_offset_ + visible)
    scroll_offset_ = selected_ - visible + 1;
}

void ListMenuScreen::draw_all_(DrawBuffer& buf) const {
  const int W = DrawBuffer::kWidth;
  const int H = DrawBuffer::kHeight;
  const int n = count();

  buf.fill(true);

  // Title (header font)
  if (title_ && header_font_.valid()) {
    const size_t title_len = std::strlen(title_);
    const int tw = header_font_.word_width(title_, title_len, FontStyle::Regular);
    buf.draw_text_proportional((W - tw) / 2, 35 + header_font_.baseline(), title_, header_font_, false);
  }

  if (!ui_font_.valid() || n == 0) {
    if (n == 0 && ui_font_.valid()) {
      static const char* kEmpty = "No items";
      const int ew = ui_font_.word_width(kEmpty, std::strlen(kEmpty), FontStyle::Regular);
      buf.draw_text_proportional((W - ew) / 2, H / 2 + ui_font_.baseline(), kEmpty, ui_font_, false);
    }
    return;
  }

  const int line_h = ui_font_.y_advance() + 8;
  const int baseline = ui_font_.baseline();
  const int header_h = 35 + (header_font_.valid() ? header_font_.y_advance() : 0) + 8;
  const int visible = (H - header_h) / line_h;

  const int end = scroll_offset_ + visible < n ? scroll_offset_ + visible : n;
  const int draw_count = end - scroll_offset_;
  const int total_h = line_h * draw_count;
  const int items_y = n <= visible ? (H - total_h) / 2 : header_h;

  for (int i = scroll_offset_; i < end; ++i) {
    const int row = i - scroll_offset_;
    const size_t len = std::strlen(labels_[i]);
    const int iw = ui_font_.word_width(labels_[i], len, FontStyle::Regular);
    const int ix = (W - iw) / 2;
    const int iy = items_y + row * line_h;
    if (i == selected_) {
      const int bar_h = ui_font_.y_advance();
      buf.fill_rect(ix - 4, iy - 1, iw + 8, bar_h, false);
      buf.draw_text_proportional(ix, iy + baseline, labels_[i], ui_font_, true);
    } else {
      buf.draw_text_proportional(ix, iy + baseline, labels_[i], ui_font_, false);
    }
  }

  // Scrollbar on the right edge when items overflow
  if (n > visible) {
    const int sb_x = W - 8;
    const int sb_w = 3;
    const int sb_top = items_y - 1;
    const int sb_bottom = items_y + (draw_count - 1) * line_h + ui_font_.y_advance() - 1;
    const int sb_total_h = sb_bottom - sb_top;
    const int thumb_h = sb_total_h * visible / n < 6 ? 6 : sb_total_h * visible / n;
    const int track = sb_total_h - thumb_h;
    const int max_scroll = n - visible;
    const int thumb_y = sb_top + (max_scroll > 0 ? track * scroll_offset_ / max_scroll : 0);
    buf.fill_rect(sb_x, thumb_y, sb_w, thumb_h, false);
  }
}

bool ListMenuScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (buttons.is_pressed(Button::Button0)) {
    if (!on_back())
      return false;
  }

  const int n = count();
  if (n == 0)
    return true;

  bool moved = false;
  if (buttons.is_pressed(Button::Button3)) {
    selected_ = selected_ > 0 ? selected_ - 1 : n - 1;
    ensure_visible_();
    moved = true;
  }
  if (buttons.is_pressed(Button::Button2)) {
    selected_ = selected_ < n - 1 ? selected_ + 1 : 0;
    ensure_visible_();
    moved = true;
  }

  if (moved) {
    draw_all_(buf);
    buf.refresh();
  }

  if (buttons.is_pressed(Button::Button1) && selected_ < n) {
    if (on_select(selected_)) {
      draw_all_(buf);
      buf.refresh();
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace microreader
