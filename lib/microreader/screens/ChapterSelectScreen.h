#pragma once

#include <cstring>

#include "../Input.h"
#include "../content/ContentModel.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"

namespace microreader {

// Chapter selection screen — lists TOC entries from an MRB file.
// Button3/Button2 = navigate up/down, Button1 = jump to chapter, Button0 = cancel.
class ChapterSelectScreen final : public IScreen {
 public:
  static constexpr int kMaxItems = 200;

  ChapterSelectScreen() = default;

  // Populate the list from a TableOfContents. Call before pushing this screen.
  void populate(const TableOfContents& toc);

  const char* name() const override {
    return "Chapters";
  }

  // Returns true if the user selected a chapter (vs. pressing back).
  bool has_pending() const {
    return has_pending_;
  }
  // The chapter index to jump to (valid only when has_pending() == true).
  uint16_t pending_chapter() const {
    return pending_chapter_;
  }
  // The paragraph index within that chapter (0 if no specific anchor).
  uint16_t pending_para_index() const {
    return pending_para_index_;
  }
  void clear_pending() {
    has_pending_ = false;
  }

  void start(DrawBuffer& buf) override;
  void stop() override;
  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 private:
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8 * kScale;
  static constexpr int kGlyphH = 8 * kScale;
  static constexpr int kLineHeight = kGlyphH + 6;
  static constexpr int kPadding = 12;
  static constexpr int kIndentW = kGlyphW * 2;  // pixels per depth level
  static constexpr int kListY = kPadding + kLineHeight + 4;
  static constexpr int kVisible = (DrawBuffer::kHeight - kListY) / kLineHeight;
  static constexpr int kMaxLabelLen = 28;

  struct Entry {
    char label[kMaxLabelLen + 1];
    uint16_t chapter_idx;
    uint8_t depth;
    uint16_t para_index;
  };
  Entry entries_[kMaxItems] = {};
  int count_ = 0;
  int selected_ = 0;
  int scroll_offset_ = 0;  // index of first visible row

  uint16_t pending_chapter_ = 0;
  uint16_t pending_para_index_ = 0;
  bool has_pending_ = false;

  void draw_all_(DrawBuffer& buf) const;
  void ensure_visible_();
};

}  // namespace microreader
