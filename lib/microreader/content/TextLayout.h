#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ContentModel.h"
#include "Font.h"
#include "IParagraphSource.h"

namespace microreader {

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
  uint16_t y_offset;      // pixel offset from page top
  uint16_t height = 0;    // line height in pixels
  uint16_t baseline = 0;  // baseline distance from top of line to baseline
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
  uint16_t padding_top = 0;
  uint16_t padding_right = 10;
  uint16_t padding_bottom = 10;
  uint16_t padding_left = 10;
  uint16_t para_spacing = 8;  // extra pixels between paragraphs
  Alignment alignment = Alignment::Justify;
  bool center_text = false;  // vertically center text content within the padded area

  PageOptions() = default;
  // Uniform padding constructor: sets all four sides to pad.
  PageOptions(uint16_t w, uint16_t h, uint16_t pad = 10, uint16_t ps = 8, Alignment a = Alignment::Justify)
      : width(w),
        height(h),
        padding_top(pad),
        padding_right(pad),
        padding_bottom(pad),
        padding_left(pad),
        para_spacing(ps),
        alignment(a) {}
};

// Optional callback for resolving image dimensions lazily at layout time.
// Called when attr_width or attr_height is 0. Returns true if size was resolved.
using ImageSizeQuery = std::function<bool(uint16_t key, uint16_t& w, uint16_t& h)>;

// ---------------------------------------------------------------------------
// TextLayout — stateful layout engine
//
// Stores font, options, paragraph source, image size query, and current
// page position. Call layout() / layout_backward() without arguments.
// Position must be updated manually after navigation (set_position).
// ---------------------------------------------------------------------------

class TextLayout {
 public:
  TextLayout() = default;
  explicit TextLayout(IFont& font) : font_(&font) {}
  TextLayout(IFont& font, const PageOptions& opts) : font_(&font), opts_(opts) {}
  TextLayout(IFont& font, const PageOptions& opts, IParagraphSource& source, ImageSizeQuery size_fn = {})
      : font_(&font), opts_(opts), source_(&source), size_fn_(std::move(size_fn)) {}
  TextLayout(IFont& font, const PageOptions& opts, IParagraphSource& source, PagePosition start,
             ImageSizeQuery size_fn = {})
      : font_(&font), opts_(opts), source_(&source), position_(start), size_fn_(std::move(size_fn)) {}

  PagePosition position() const {
    return position_;
  }
  void set_position(PagePosition pos) {
    position_ = pos;
  }

  // Replace the paragraph source (e.g. after loading a new chapter).
  void set_source(IParagraphSource& source) {
    source_ = &source;
  }

  // Replace the font (e.g. after a proportional font becomes available).
  void set_font(IFont& font) {
    font_ = &font;
  }

  // Replace options (e.g. after a screen resize).
  void set_options(const PageOptions& opts) {
    opts_ = opts;
  }

  // Replace the image size query (e.g. when a book is opened).
  void set_image_size_fn(ImageSizeQuery fn) {
    size_fn_ = std::move(fn);
  }

  // Break a single paragraph into lines using the stored font.
  std::vector<LayoutLine> layout_paragraph(const LayoutOptions& opts, const TextParagraph& para) const;

  // Layout the page starting at position(). Does not change position().
  PageContent layout() const;

  // Layout the page that ends at position() (backward fill). Does not change
  // position(). Use the returned page.start to navigate to the previous page.
  PageContent layout_backward() const;

 private:
  // Internal types used by collect_page_items / assemble_page.
  struct PageItem {
    enum Kind { TextLine, Image, Hr, Empty } kind;
    uint16_t para_idx, line_idx;
    LayoutLine layout_line;
    uint16_t height, baseline = 0;
    uint16_t img_key = 0, img_w = 0, img_h = 0, img_x = 0;
  };
  struct PendingInlineImage {
    uint16_t para_idx, key, width, height;
  };
  struct CollectResult {
    std::vector<PageItem> items;
    std::vector<PendingInlineImage> pending;
    PagePosition boundary;
    bool at_chapter_end = false;
  };

  struct InlineImageInfo {
    uint16_t key = 0, width = 0, height = 0;
    bool promoted = false, has_image = false;
  };

  CollectResult collect_page_items(PagePosition pos, bool backward) const;
  PageContent assemble_page(std::vector<PageItem>& items, PagePosition start, PagePosition end,
                            bool at_chapter_end) const;
  static InlineImageInfo resolve_inline_image(const TextParagraph& text_para, uint16_t content_width,
                                              const ImageSizeQuery& sp);
  static PageItem make_image_para_item(uint16_t pi, const Paragraph& para, const PageOptions& opts,
                                       uint16_t content_width, const ImageSizeQuery& sp);
  static std::vector<LayoutLine> prepare_text_lines(const IFont& font, LayoutOptions& lo, const TextParagraph& text,
                                                    uint16_t content_width, const ImageSizeQuery& sp, bool at_line_zero,
                                                    InlineImageInfo& img_info);

  IFont* font_ = nullptr;
  PageOptions opts_;
  IParagraphSource* source_ = nullptr;
  ImageSizeQuery size_fn_;
  PagePosition position_;
};

}  // namespace microreader
