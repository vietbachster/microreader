#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace microreader {

// ---------------------------------------------------------------------------
// Font style
// ---------------------------------------------------------------------------

enum class FontStyle : uint8_t {
  Regular = 0,
  Bold = 1,
  Italic = 2,
  BoldItalic = 3,
};

// ---------------------------------------------------------------------------
// Font size — 5 discrete sizes mapped to pre-rendered bitmap fonts
// ---------------------------------------------------------------------------

enum class FontSize : uint8_t {
  Small = 0,
  Normal = 1,
  Large = 2,
  XLarge = 3,
  XXLarge = 4,
};

static constexpr int kFontSizeCount = 5;

// ---------------------------------------------------------------------------
// Text alignment
// ---------------------------------------------------------------------------

enum class Alignment : uint8_t {
  Start = 0,
  Center,
  End,
  Justify,
};

enum class VerticalAlign : uint8_t {
  Baseline = 0,
  Super,
  Sub,
};

// ---------------------------------------------------------------------------
// A "run" of text with uniform style (matches TrustyReader's layout::Run)
// ---------------------------------------------------------------------------

struct Run {
  std::string text;
  FontStyle style = FontStyle::Regular;
  FontSize size = FontSize::Normal;
  VerticalAlign vertical_align = VerticalAlign::Baseline;
  bool breaking = false;      // true for <br> line breaks
  uint16_t margin_left = 0;   // inline left margin in pixels
  uint16_t margin_right = 0;  // inline right margin in pixels

  Run() = default;
  Run(std::string t, FontStyle s = FontStyle::Regular, bool br = false)
      : text(std::move(t)), style(s), size(FontSize::Normal), breaking(br) {}
  Run(std::string t, FontStyle s, FontSize sz, bool br = false)
      : text(std::move(t)), style(s), size(sz), breaking(br) {}

  bool operator==(const Run& o) const {
    return text == o.text && style == o.style && size == o.size && vertical_align == o.vertical_align &&
           breaking == o.breaking && margin_left == o.margin_left && margin_right == o.margin_right;
  }
};

// ---------------------------------------------------------------------------
// A text paragraph: a sequence of Runs
// ---------------------------------------------------------------------------

struct ImageRef {
  uint16_t key = 0;         // index into the EPUB's file entries / MRB image ref table
  uint16_t attr_width = 0;  // from HTML attributes or MRB image table; 0 = unknown
  uint16_t attr_height = 0;

  ImageRef() = default;
  ImageRef(uint16_t k, uint16_t w, uint16_t h) : key(k), attr_width(w), attr_height(h) {}
};

struct TextParagraph {
  std::vector<Run> runs;
  std::optional<Alignment> alignment;
  std::optional<int16_t> indent;
  std::optional<ImageRef> inline_image;  // small float image rendered at start of first line
  uint8_t line_height_pct = 100;         // line-height as % of default (100 = normal)
};

// ---------------------------------------------------------------------------
// Paragraph = Text | Image | Hr
// ---------------------------------------------------------------------------

enum class ParagraphType : uint8_t {
  Text,
  Image,
  Hr,
  PageBreak,
};

struct Paragraph {
  ParagraphType type = ParagraphType::Text;
  TextParagraph text;                      // valid when type == Text
  ImageRef image;                          // valid when type == Image
  std::optional<uint16_t> spacing_before;  // override para_spacing if set

  Paragraph() = default;

  static Paragraph make_text(TextParagraph&& t) {
    Paragraph p;
    p.type = ParagraphType::Text;
    p.text = std::move(t);
    return p;
  }
  static Paragraph make_image(uint16_t key, uint16_t w = 0, uint16_t h = 0) {
    Paragraph p;
    p.type = ParagraphType::Image;
    p.image = {key, w, h};
    return p;
  }
  static Paragraph make_hr() {
    Paragraph p;
    p.type = ParagraphType::Hr;
    return p;
  }
  static Paragraph make_page_break() {
    Paragraph p;
    p.type = ParagraphType::PageBreak;
    return p;
  }
};

// ---------------------------------------------------------------------------
// Chapter
// ---------------------------------------------------------------------------

struct Chapter {
  std::optional<std::string> title;
  std::vector<Paragraph> paragraphs;
};

// ---------------------------------------------------------------------------
// EPUB metadata
// ---------------------------------------------------------------------------

struct EpubMetadata {
  std::string title;
  std::optional<std::string> author;
  std::optional<std::string> language;
  std::optional<std::string> cover_id;
};

// ---------------------------------------------------------------------------
// EPUB spine item — references a file in the ZIP
// ---------------------------------------------------------------------------

struct SpineItem {
  uint16_t file_idx = 0;
};

// ---------------------------------------------------------------------------
// Table of contents entry
// ---------------------------------------------------------------------------

struct TocEntry {
  std::string label;
  uint16_t file_idx = 0;
};

struct TableOfContents {
  std::vector<TocEntry> entries;
};

}  // namespace microreader
