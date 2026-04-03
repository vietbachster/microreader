#pragma once

#include <cstdio>
#include <string>

#include "Book.h"
#include "TextLayout.h"

namespace microreader {

struct HtmlExportOptions {
  uint16_t page_width = 480;   // real e-ink display width
  uint16_t page_height = 800;  // real e-ink display height
  uint16_t padding = 20;
  uint16_t para_spacing = 8;
  Alignment alignment = Alignment::Justify;
  bool show_page_breaks = true;  // visual page separators
  bool show_debug_info = true;   // paragraph/line metadata
  size_t max_chapters = 0;       // 0 = all chapters
};

// Export a Book to a self-contained HTML file.
// Uses the TextLayout engine to paginate content exactly as the reader would.
// Returns true on success.
bool export_to_html(Book& book, const IFont& font, const HtmlExportOptions& opts, const char* output_path);

}  // namespace microreader
