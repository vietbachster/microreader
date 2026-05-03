#include "ReaderOptionsScreen.h"

#include <cstring>

#include "../Application.h"

namespace microreader {
constexpr uint16_t ReaderSettings::kHPaddingPresets[];
constexpr uint16_t ReaderSettings::kVPaddingPresets[];
constexpr uint16_t ReaderSettings::kSpacingPercents[];
constexpr uint8_t ReaderSettings::kNumAlignPresets;
constexpr uint8_t ReaderSettings::kNumSpacingPresets;
constexpr uint8_t ReaderSettings::kNumFontSizePresets;
constexpr const char* ReaderSettings::kHPaddingNames[];
constexpr const char* ReaderSettings::kVPaddingNames[];
constexpr const char* ReaderSettings::kAlignNames[];
constexpr const char* ReaderSettings::kSpacingNames[];
constexpr const char* ReaderSettings::kFontSizeNames[];

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
    add_item(fmt_setting(tmp, sizeof(tmp), "Align",
                         ReaderSettings::kAlignNames[static_cast<uint8_t>(settings_->align_override)]));

    idx_font_size_ = count();
    if (app_ && app_->font_manager() && app_->font_manager()->valid()) {
      auto* fonts = app_->font_manager()->font_set();
      int sz = 0;
      if (fonts && settings_->font_size_idx < fonts->num_fonts()) {
        auto* f = fonts->get_font(settings_->font_size_idx);
        if (f) {
          sz = f->nominal_size();
          if (sz == 0)
            sz = f->y_advance();
        }
      }
      char val[16];
      if (sz > 0)
        snprintf(val, sizeof(val), "%d", sz);
      else
        snprintf(val, sizeof(val), "Unknown");
      add_item(fmt_setting(tmp, sizeof(tmp), "Size", val));
    } else {
      add_item(fmt_setting(tmp, sizeof(tmp), "Size", ReaderSettings::kFontSizeNames[settings_->font_size_idx]));
    }

    idx_padding_h_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "H-Margin", ReaderSettings::kHPaddingNames[settings_->padding_h_idx]));

    idx_padding_v_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "V-Margin", ReaderSettings::kVPaddingNames[settings_->padding_v_idx]));

    idx_line_spacing_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Line spacing",
                         ReaderSettings::kSpacingNames[static_cast<uint8_t>(settings_->spacing_override)]));

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

void ReaderOptionsScreen::on_select(int index) {
  if (!settings_) {
    if (index == idx_chapters_) {
      app_->push_screen(ScreenId::ChapterSelect);
      return;
    }
    return;
  }

  if (index == idx_justify_) {
    settings_->align_override = static_cast<AlignOverride>((static_cast<uint8_t>(settings_->align_override) + 1) %
                                                           ReaderSettings::kNumAlignPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_font_size_) {
    uint8_t max_idx = ReaderSettings::kNumFontSizePresets;
    if (app_ && app_->font_manager() && app_->font_manager()->valid()) {
      auto* fonts = app_->font_manager()->font_set();
      if (fonts && fonts->num_fonts() > 0)
        max_idx = static_cast<uint8_t>(fonts->num_fonts());
    }
    settings_->font_size_idx = static_cast<uint8_t>((settings_->font_size_idx + 1) % max_idx);
    refresh_items_(index);
    return;
  }
  if (index == idx_padding_h_) {
    settings_->padding_h_idx = static_cast<uint8_t>((settings_->padding_h_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_padding_v_) {
    settings_->padding_v_idx = static_cast<uint8_t>((settings_->padding_v_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_line_spacing_) {
    settings_->spacing_override = static_cast<SpacingOverride>((static_cast<uint8_t>(settings_->spacing_override) + 1) %
                                                               ReaderSettings::kNumSpacingPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_progress_) {
    settings_->progress_style = static_cast<ProgressStyle>((static_cast<uint8_t>(settings_->progress_style) + 1) % 3);
    refresh_items_(index);
    return;
  }
  if (index == idx_chapters_) {
    app_->push_screen(ScreenId::ChapterSelect);
    return;
  }
  return;
}

}  // namespace microreader
