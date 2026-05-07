#pragma once

#include <cstdint>
#include <vector>

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

enum class AlignOverride : uint8_t {
  Book = 0,
  Left,
  Center,
  Right,
  Justify,
};

enum class SpacingOverride : uint8_t {
  Book = 0,
  Spacing_0_8x,
  Spacing_0_9x,
  Spacing_1_0x,
  Spacing_1_1x,
  Spacing_1_2x,
};

struct ReaderSettings {
  AlignOverride align_override = AlignOverride::Book;
  SpacingOverride spacing_override = SpacingOverride::Spacing_1_0x;
  uint8_t padding_h_idx = 1;                          // horizontal padding preset index (left & right)
  uint8_t padding_v_idx = 1;                          // vertical top padding preset index
  uint8_t font_size_idx = 1;                          // base font size preset index (1 = Normal/24px)
  ProgressStyle progress_style = ProgressStyle::Bar;  // reading progress indicator style
  bool override_publisher_fonts = false;              // ignore publisher's font sizes

  static constexpr uint16_t kHPaddingPresets[] = {4, 12, 24, 40};
  static constexpr uint16_t kVPaddingPresets[] = {0, 4, 12, 20};

  static constexpr uint16_t kSpacingPercents[] = {0, 80, 90, 100, 110, 120};  // Index matches SpacingOverride

  static constexpr const char* kHPaddingNames[] = {"Narrow", "Normal", "Wide", "Wider"};
  static constexpr const char* kVPaddingNames[] = {"Tight", "Normal", "Loose", "Looser"};
  static constexpr const char* kAlignNames[] = {"Book", "Left", "Center", "Right", "Justify"};
  static constexpr const char* kSpacingNames[] = {"Book", "0.8x", "0.9x", "1.0x", "1.1x", "1.2x"};
  static constexpr const char* kFontSizeNames[] = {"20", "24", "26", "28", "30", "32", "34", "36"};

  static constexpr uint8_t kNumPresets = 4;
  static constexpr uint8_t kNumAlignPresets = 5;
  static constexpr uint8_t kNumSpacingPresets = 6;
  static constexpr uint8_t kNumFontSizePresets = 8;

  uint16_t h_padding() const {
    return kHPaddingPresets[padding_h_idx];
  }
  uint16_t v_padding() const {
    return kVPaddingPresets[padding_v_idx];
  }
  uint16_t line_height_multiplier_percent() const {
    return kSpacingPercents[static_cast<uint8_t>(spacing_override)];
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
//   app_->reader_options()->set_settings(&reader_settings_);
//   app_->reader_options()->populate(mrb_.toc(), chapter_idx, page_pos_.paragraph);
//   app_->push_screen(ScreenId::ReaderOptions);
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
  void populate(const TableOfContents& toc, uint16_t current_chapter, uint16_t current_para,
                const std::string& fallback_title, int progress_pct);

 protected:
  void on_start() override;
  void on_select(int index) override;

 private:
  ReaderSettings* settings_ = nullptr;

  // Item indices (-1 = not shown).
  int idx_justify_ = -1;
  int idx_padding_h_ = -1;
  int idx_padding_v_ = -1;
  int idx_line_spacing_ = -1;
  int idx_font_size_ = -1;
  int idx_progress_ = -1;
  int idx_pub_fonts_ = -1;
  int idx_chapters_ = -1;

  // Re-populate item labels after an inline setting change, restoring selection.
  void refresh_items_(int restore_selection);

  std::string chapter_title_;
  int progress_pct_ = 0;
};

}  // namespace microreader
