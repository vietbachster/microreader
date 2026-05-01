#include "ReaderOptionsScreen.h"

#include <cstring>

#include "../Application.h"

namespace microreader {
constexpr uint16_t ReaderSettings::kHPaddingPresets[];
constexpr uint16_t ReaderSettings::kVPaddingPresets[];
constexpr int16_t ReaderSettings::kSpacingPresets[];
constexpr uint8_t ReaderSettings::kNumSpacingPresets;
constexpr const char* ReaderSettings::kHPaddingNames[];
constexpr const char* ReaderSettings::kVPaddingNames[];
constexpr const char* ReaderSettings::kSpacingNames[];

// ---------------------------------------------------------------------------

void ReaderOptionsScreen::populate(const TableOfContents& toc, uint16_t current_chapter, uint16_t current_para) {
  if (!toc.entries.empty() && app_) {
    app_->chapter_select()->populate(toc, current_chapter, current_para);
    app_->chapter_select()->clear_pending();
  }
}

// Build a "Label: Value" string into a fixed buffer.
static const char* fmt_setting(char* buf, size_t bufsz, const char* label, const char* value) {
  snprintf(buf, bufsz, "%s: %s", label, value);
  return buf;
}

void ReaderOptionsScreen::on_start() {
  title_ = "Options";
  clear_items();
  idx_justify_ = idx_padding_h_ = idx_padding_v_ = idx_line_spacing_ = idx_progress_ = idx_chapters_ = -1;

  char tmp[40];

  // "Chapters" goes at the top so it's easy to reach.
  if (app_ && app_->chapter_select()->has_toc()) {
    idx_chapters_ = count();
    add_item("Chapters");
  }

  // Separator between chapter navigation and text layout settings.
  if (settings_ && app_ && app_->chapter_select()->has_toc())
    add_separator();

  if (settings_) {
    idx_justify_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Justify", settings_->justify ? "On" : "Off"));

    idx_padding_h_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "H-Margin", ReaderSettings::kHPaddingNames[settings_->padding_h_idx]));

    idx_padding_v_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "V-Margin", ReaderSettings::kVPaddingNames[settings_->padding_v_idx]));

    idx_line_spacing_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Line spacing", ReaderSettings::kSpacingNames[settings_->line_spacing_idx]));

    idx_progress_ = count();
    const char* prog_name = settings_->progress_style == ProgressStyle::None         ? "None"
                            : settings_->progress_style == ProgressStyle::Percentage ? "Percent"
                                                                                     : "Bar";
    add_item(fmt_setting(tmp, sizeof(tmp), "Progress", prog_name));
  }
}

void ReaderOptionsScreen::refresh_items_(int restore_selection) {
  on_start();
  set_selected(restore_selection);
}

bool ReaderOptionsScreen::on_select(int index) {
  if (!settings_) {
    if (index == idx_chapters_) {
      app_->replace_screen(ScreenId::ChapterSelect);
      return true;
    }
    return true;
  }

  if (index == idx_justify_) {
    settings_->justify = !settings_->justify;
    refresh_items_(index);
    return true;
  }
  if (index == idx_padding_h_) {
    settings_->padding_h_idx = static_cast<uint8_t>((settings_->padding_h_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return true;
  }
  if (index == idx_padding_v_) {
    settings_->padding_v_idx = static_cast<uint8_t>((settings_->padding_v_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return true;
  }
  if (index == idx_line_spacing_) {
    settings_->line_spacing_idx =
        static_cast<uint8_t>((settings_->line_spacing_idx + 1) % ReaderSettings::kNumSpacingPresets);
    refresh_items_(index);
    return true;
  }
  if (index == idx_progress_) {
    settings_->progress_style = static_cast<ProgressStyle>((static_cast<uint8_t>(settings_->progress_style) + 1) % 3);
    refresh_items_(index);
    return true;
  }
  if (index == idx_chapters_) {
    app_->replace_screen(ScreenId::ChapterSelect);
    return true;
  }
  return true;
}

}  // namespace microreader
