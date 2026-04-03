#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ContentModel.h"

namespace microreader {

// ---------------------------------------------------------------------------
// Font measurement interface — abstract so tests can use a fixed-width font
// ---------------------------------------------------------------------------

struct IFont {
  virtual ~IFont() = default;

  // Width of a single character in pixels. Returns 0 if glyph missing.
  virtual uint16_t char_width(char32_t ch, FontStyle style, FontSize size = FontSize::Normal) const = 0;

  // Width of a UTF-8 word (sum of char widths)
  virtual uint16_t word_width(const char* text, size_t len, FontStyle style,
                              FontSize size = FontSize::Normal) const = 0;

  // Vertical advance per line (includes leading)
  virtual uint16_t y_advance(FontSize size = FontSize::Normal) const = 0;

  // Distance from top of line to baseline (where text sits, above descenders)
  virtual uint16_t baseline(FontSize size = FontSize::Normal) const = 0;
};

// ---------------------------------------------------------------------------
// Fixed-width font for testing
// ---------------------------------------------------------------------------

struct FixedFont : IFont {
  uint16_t glyph_width;
  uint16_t line_height;

  explicit FixedFont(uint16_t gw = 8, uint16_t lh = 16) : glyph_width(gw), line_height(lh) {}

  uint16_t char_width(char32_t, FontStyle, FontSize size = FontSize::Normal) const override {
    return scale_width(size);
  }

  uint16_t word_width(const char* text, size_t len, FontStyle, FontSize size = FontSize::Normal) const override {
    // Count Unicode codepoints, not bytes
    uint16_t count = 0;
    for (size_t i = 0; i < len;) {
      uint8_t b = static_cast<uint8_t>(text[i]);
      if (b < 0x80)
        i += 1;
      else if (b < 0xE0)
        i += 2;
      else if (b < 0xF0)
        i += 3;
      else
        i += 4;
      ++count;
    }
    return count * scale_width(size);
  }

  uint16_t y_advance(FontSize size = FontSize::Normal) const override {
    return scale_height(size);
  }

  uint16_t baseline(FontSize size = FontSize::Normal) const override {
    // Baseline at ~80% of line height (above descenders/leading)
    return scale_height(size) * 4 / 5;
  }

 private:
  uint16_t scale_width(FontSize size) const {
    switch (size) {
      case FontSize::Small:
        return glyph_width * 3 / 4;  // 75%
      case FontSize::Large:
        return glyph_width * 5 / 4;  // 125%
      default:
        return glyph_width;
    }
  }
  uint16_t scale_height(FontSize size) const {
    switch (size) {
      case FontSize::Small:
        return line_height * 3 / 4;
      case FontSize::Large:
        return line_height * 5 / 4;
      default:
        return line_height;
    }
  }
};

// ---------------------------------------------------------------------------
// Paragraph source — abstract interface for accessing paragraphs.
// Lets layout_page() work with Chapter (in-memory) or MrbReader (on-disk).
// ---------------------------------------------------------------------------

struct IParagraphSource {
  virtual ~IParagraphSource() = default;
  virtual size_t paragraph_count() const = 0;
  virtual const Paragraph& paragraph(size_t index) const = 0;
};

// Chapter adapter — wraps Chapter::paragraphs for IParagraphSource.
class ChapterParagraphSource : public IParagraphSource {
 public:
  explicit ChapterParagraphSource(const Chapter& ch) : ch_(ch) {}
  size_t paragraph_count() const override {
    return ch_.paragraphs.size();
  }
  const Paragraph& paragraph(size_t index) const override {
    return ch_.paragraphs[index];
  }

 private:
  const Chapter& ch_;
};

// ---------------------------------------------------------------------------
// Layout output types
// ---------------------------------------------------------------------------

struct LayoutWord {
  const char* text;  // pointer into original Run (stable for chapter lifetime)
  uint16_t len;      // byte length
  uint16_t x;        // pixel offset from left edge
  FontStyle style;
  FontSize size = FontSize::Normal;
  VerticalAlign vertical_align = VerticalAlign::Baseline;
  bool continues_prev = false;  // true if placed adjacent to previous word (no inter-word gap)
};

struct LayoutLine {
  std::vector<LayoutWord> words;
  bool hyphenated = false;  // true if line ends with a hyphen
};

// ---------------------------------------------------------------------------
// layout_paragraph() — break a single paragraph into lines
// ---------------------------------------------------------------------------

struct LayoutOptions {
  uint16_t width = 300;  // available line width in pixels
  Alignment alignment = Alignment::Justify;
  uint16_t first_line_extra_indent = 0;  // extra left indent for inline images on first line

  LayoutOptions() = default;
  LayoutOptions(uint16_t w, Alignment a = Alignment::Justify) : width(w), alignment(a) {}
};

std::vector<LayoutLine> layout_paragraph(const IFont& font, const LayoutOptions& opts, const TextParagraph& para);

// ---------------------------------------------------------------------------
// Page layout — fit paragraphs onto a fixed-height page
// ---------------------------------------------------------------------------

struct PagePosition {
  uint16_t paragraph = 0;  // index into chapter.paragraphs
  uint16_t line = 0;       // line within that paragraph (0 for images/hrs)

  PagePosition() = default;
  PagePosition(uint16_t p, uint16_t l) : paragraph(p), line(l) {}

  bool operator==(const PagePosition& o) const {
    return paragraph == o.paragraph && line == o.line;
  }
  bool operator<(const PagePosition& o) const {
    return paragraph < o.paragraph || (paragraph == o.paragraph && line < o.line);
  }
  bool operator<=(const PagePosition& o) const {
    return *this == o || *this < o;
  }
};

struct PageImageItem {
  uint16_t paragraph_index;
  uint16_t key;
  uint16_t width;
  uint16_t height;
  uint16_t x_offset;  // pixel offset from page left (for centering)
  uint16_t y_offset;  // pixel offset from page top
};

struct PageHrItem {
  uint16_t x_offset;  // pixel offset from page left
  uint16_t y_offset;  // pixel offset from page top
  uint16_t width;     // line width
};

struct PageTextItem {
  uint16_t paragraph_index;
  uint16_t line_index;
  LayoutLine line;
  uint16_t y_offset;  // pixel offset from page top
};

struct PageContent {
  std::vector<PageTextItem> text_items;
  std::vector<PageImageItem> image_items;
  std::vector<PageHrItem> hr_items;
  PagePosition start;
  PagePosition end;              // one-past-end position
  uint16_t vertical_offset = 0;  // shift all content down by this amount (for centering)
  bool at_chapter_end = false;
};

struct PageOptions {
  uint16_t width = 300;
  uint16_t height = 400;
  uint16_t padding = 10;
  uint16_t para_spacing = 8;  // extra pixels between paragraphs
  Alignment alignment = Alignment::Justify;

  PageOptions() = default;
  PageOptions(uint16_t w, uint16_t h, uint16_t pad = 10, uint16_t ps = 8, Alignment a = Alignment::Justify)
      : width(w), height(h), padding(pad), para_spacing(ps), alignment(a) {}
};

// Layout one page worth of content starting at `start`.
// The paragraph source provides paragraphs on demand.
PageContent layout_page(const IFont& font, const PageOptions& opts, IParagraphSource& source, PagePosition start);

// Convenience overload: pass a Chapter directly (wraps in ChapterParagraphSource).
inline PageContent layout_page(const IFont& font, const PageOptions& opts, const Chapter& chapter, PagePosition start) {
  ChapterParagraphSource src(chapter);
  return layout_page(font, opts, src, start);
}

}  // namespace microreader
