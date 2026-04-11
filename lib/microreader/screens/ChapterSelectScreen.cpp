#include "ChapterSelectScreen.h"

#include <cstring>

namespace microreader {

void ChapterSelectScreen::populate(const TableOfContents& toc) {
  count_ = 0;
  selected_ = 0;
  scroll_offset_ = 0;
  for (const auto& entry : toc.entries) {
    if (count_ >= kMaxItems)
      break;
    auto& e = entries_[count_];
    size_t label_len = entry.label.size();
    if (label_len > kMaxLabelLen)
      label_len = kMaxLabelLen;
    std::memcpy(e.label, entry.label.c_str(), label_len);
    e.label[label_len] = '\0';
    e.chapter_idx = entry.file_idx;
    e.depth = entry.depth;
    e.para_index = entry.para_index;
    ++count_;
  }
}

void ChapterSelectScreen::ensure_visible_() {
  if (selected_ < scroll_offset_)
    scroll_offset_ = selected_;
  else if (selected_ >= scroll_offset_ + kVisible)
    scroll_offset_ = selected_ - kVisible + 1;
}

void ChapterSelectScreen::draw_all_(DrawBuffer& buf) const {
  buf.fill(true);
  const char* title = count_ > 0 ? "Select Chapter:" : "No chapters";
  buf.draw_text(kPadding, kPadding, title, true, kScale);

  // Scroll indicator: "23/45" style in top-right
  if (count_ > kVisible) {
    char scroll_info[12];
    std::snprintf(scroll_info, sizeof(scroll_info), "%d/%d", selected_ + 1, count_);
    int info_x = DrawBuffer::kWidth - static_cast<int>(std::strlen(scroll_info)) * kGlyphW - kPadding;
    buf.draw_text(info_x, kPadding, scroll_info, true, kScale);
  }

  const int end = scroll_offset_ + kVisible < count_ ? scroll_offset_ + kVisible : count_;
  for (int i = scroll_offset_; i < end; ++i) {
    int row = i - scroll_offset_;
    int x = kPadding + entries_[i].depth * kIndentW;
    buf.draw_text(x, kListY + row * kLineHeight, entries_[i].label, i != selected_, kScale);
  }
}

void ChapterSelectScreen::start(DrawBuffer& buf) {
  // selected_ / scroll_offset_ / count_ are preserved from populate() —
  // re-entry after being stopped restores the same scroll position.
  draw_all_(buf);
}

void ChapterSelectScreen::stop() {}

bool ChapterSelectScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (buttons.is_pressed(Button::Button0))
    return false;  // cancel — has_pending_ stays false

  if (count_ == 0)
    return true;

  bool moved = false;
  if (buttons.is_pressed(Button::Button3)) {  // up
    if (selected_ > 0) {
      --selected_;
      ensure_visible_();
      moved = true;
    }
  }
  if (buttons.is_pressed(Button::Button2)) {  // down
    if (selected_ < count_ - 1) {
      ++selected_;
      ensure_visible_();
      moved = true;
    }
  }

  if (moved) {
    draw_all_(buf);
    buf.refresh();
  }

  if (buttons.is_pressed(Button::Button1)) {
    pending_chapter_ = entries_[selected_].chapter_idx;
    pending_para_index_ = entries_[selected_].para_index;
    has_pending_ = true;
    return false;
  }

  return true;
}

}  // namespace microreader
