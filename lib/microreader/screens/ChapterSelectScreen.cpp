#include "ChapterSelectScreen.h"

#include <cstring>

namespace microreader {

void ChapterSelectScreen::populate(const TableOfContents& toc, uint16_t current_chapter) {
  entries_.clear();
  entries_.reserve(toc.entries.size());
  initial_selected_ = 0;
  for (const auto& entry : toc.entries) {
    Entry e{};
    size_t label_len = entry.label.size();
    if (label_len > kMaxLabelLen)
      label_len = kMaxLabelLen;
    std::memcpy(e.label, entry.label.c_str(), label_len);
    e.label[label_len] = '\0';
    e.chapter_idx = entry.file_idx;
    e.para_index = entry.para_index;
    // Track the last TOC entry whose chapter_idx <= current_chapter.
    if (entry.file_idx <= current_chapter)
      initial_selected_ = static_cast<int>(entries_.size());
    entries_.push_back(e);
  }
}

void ChapterSelectScreen::on_start() {
  title_ = !entries_.empty() ? "Chapters" : "No chapters";
  for (auto& e : entries_)
    add_item(e.label);
  set_selected(initial_selected_);
}

bool ChapterSelectScreen::on_select(int index) {
  pending_chapter_ = entries_[index].chapter_idx;
  pending_para_index_ = entries_[index].para_index;
  has_pending_ = true;
  return false;
}

}  // namespace microreader
