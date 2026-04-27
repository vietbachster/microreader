#pragma once

#include <cstdint>
#include <vector>

#include "ChapterSelectScreen.h"
#include "ListMenuScreen.h"

namespace microreader {

// ---------------------------------------------------------------------------
// ReaderSettings — user-adjustable reader preferences
// Stored in ReaderScreen; mutated inline by ReaderOptionsScreen.
// ---------------------------------------------------------------------------
enum class ProgressStyle : uint8_t {
  None = 0,
  Percentage = 1,
  Bar = 2,
};

struct ReaderSettings {
  bool justify = false;                               // false = left-align (default); true = justify
  uint8_t padding_h_idx = 1;                          // horizontal padding preset index (left & right)
  uint8_t padding_v_idx = 1;                          // vertical top padding preset index
  uint8_t line_spacing_idx = 2;                       // paragraph spacing preset index (2 = Normal)
  ProgressStyle progress_style = ProgressStyle::Bar;  // reading progress indicator style

  static constexpr uint16_t kHPaddingPresets[] = {4, 12, 24, 40};
  static constexpr uint16_t kVPaddingPresets[] = {0, 4, 8, 12};
  // Extra pixels added to every line's height. 0 = Normal (font default).
  // Negative values tighten lines; positive values open them up.
  static constexpr int16_t kSpacingPresets[] = {-8, -4, 0, 4, 8};
  static constexpr const char* kHPaddingNames[] = {"Narrow", "Normal", "Wide", "Wider"};
  static constexpr const char* kVPaddingNames[] = {"Tight", "Normal", "Loose", "Looser"};
  static constexpr const char* kSpacingNames[] = {"Tighter", "Tight", "Normal", "Loose", "Looser"};
  static constexpr uint8_t kNumPresets = 4;
  static constexpr uint8_t kNumSpacingPresets = 5;

  uint16_t h_padding() const {
    return kHPaddingPresets[padding_h_idx];
  }
  uint16_t v_padding() const {
    return kVPaddingPresets[padding_v_idx];
  }
  int16_t extra_line_spacing() const {
    return kSpacingPresets[line_spacing_idx];
  }
  // Bottom padding reserved for the progress indicator.
  uint16_t progress_bottom() const {
    switch (progress_style) {
      case ProgressStyle::Percentage:
        return 16;
      case ProgressStyle::Bar:
        return 8;
      default:
        return 6;
    }
  }
};

// In-reader options menu — shown when the user presses Button1 while reading.
// Populated by ReaderScreen before being pushed so it reflects the current
// reading context (TOC availability, page links, etc.).
//
// Currently supports:
//   - Justify on/off, H-Margin, V-Margin, Line spacing (inline cycling)
//   - "Chapters" (TOC navigation) → replaces this screen with ChapterSelectScreen
//
// Usage:
//   reader_options_.set_settings(&reader_settings_);
//   reader_options_.populate(mrb_.toc(), chapter_idx, page_pos_.paragraph);
//   nav_chosen_ = &reader_options_;
class ReaderOptionsScreen final : public ListMenuScreen {
 public:
  ReaderOptionsScreen() = default;

  const char* name() const override {
    return "Options";
  }

  // Set the settings object to read/write (must outlive this screen).
  void set_settings(ReaderSettings* s) {
    settings_ = s;
  }

  // Populate before pushing. Pass toc (may be empty — "Chapters" hidden when empty).
  void populate(const TableOfContents& toc, uint16_t current_chapter, uint16_t current_para);

  // Access the chapter select screen to read pending state after it returns.
  ChapterSelectScreen& chapter_select() {
    return chapter_select_;
  }

  // Returns the screen to replace this one with (pop Options, push it).
  // Set when user picks "Chapters".
  IScreen* replace_with() const override {
    return replace_;
  }

 protected:
  void on_start() override;
  bool on_select(int index) override;

 private:
  ChapterSelectScreen chapter_select_;
  ReaderSettings* settings_ = nullptr;
  IScreen* replace_ = nullptr;

  // Item indices (-1 = not shown).
  int idx_justify_ = -1;
  int idx_padding_h_ = -1;
  int idx_padding_v_ = -1;
  int idx_line_spacing_ = -1;
  int idx_progress_ = -1;
  int idx_chapters_ = -1;

  // Re-populate item labels after an inline setting change, restoring selection.
  void refresh_items_(int restore_selection);
};

}  // namespace microreader
