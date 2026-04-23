#pragma once

#include <array>
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
  uint16_t offset = 0;     // line index within a text paragraph; pixel row offset within an image paragraph (0 for hrs)

  PagePosition() = default;
  PagePosition(uint16_t p, uint16_t l) : paragraph(p), offset(l) {}

  bool operator==(const PagePosition& o) const {
    return paragraph == o.paragraph && offset == o.offset;
  }
  bool operator!=(const PagePosition& o) const {
    return !(*this == o);
  }
  bool operator<(const PagePosition& o) const {
    return paragraph < o.paragraph || (paragraph == o.paragraph && offset < o.offset);
  }
  bool operator<=(const PagePosition& o) const {
    return *this == o || *this < o;
  }
};

struct PageImageItem {
  uint16_t paragraph_index;
  uint16_t key;
  uint16_t width;
  uint16_t height;           // slice height (pixels rendered on this page)
  uint16_t x_offset;         // pixel offset from page left (for centering)
  uint16_t y_offset;         // pixel offset from page top
  uint16_t y_crop = 0;       // rows into source image to skip (for partial/split images)
  uint16_t full_height = 0;  // full scaled image height (for decode: width × aspect)
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
    cache_valid_ = false;
  }

  // Replace the font (e.g. after a proportional font becomes available).
  void set_font(IFont& font) {
    font_ = &font;
    cache_valid_ = false;
  }

  // Replace options (e.g. after a screen resize).
  void set_options(const PageOptions& opts) {
    opts_ = opts;
    cache_valid_ = false;
  }

  // Replace the image size query (e.g. when a book is opened).
  void set_image_size_fn(ImageSizeQuery fn) {
    size_fn_ = std::move(fn);
    cache_valid_ = false;
  }

  // Break a single paragraph into lines using the stored font.
  std::vector<LayoutLine> layout_paragraph(const LayoutOptions& opts, const TextParagraph& para) const;

  // Layout the page starting at position(). Does not change position().
  PageContent layout() const;

  // Layout the page that ends at position() (backward fill). Does not change
  // position(). Use the returned page.start to navigate to the previous page.
  PageContent layout_backward() const;

  // Implementation-internal types. Declared public so that .cpp-scope helper
  // functions (which are not class members) can reference them without
  // triggering MSVC C2248 private-access errors.
  struct PageItem {
    enum Kind { TextLine, Image, Hr, Empty, PageBreak } kind;
    uint16_t para_idx, line_idx;
    LayoutLine layout_line;
    uint16_t height, baseline = 0;
    uint16_t img_key = 0, img_w = 0, img_h = 0, img_x = 0, img_y_crop = 0;
  };
  struct CollectResult {
    std::vector<PageItem> items;
    PagePosition boundary;
    bool at_chapter_end = false;
  };

  struct InlineImageInfo {
    uint16_t key = 0, width = 0, height = 0;
    bool promoted = false, has_image = false;
  };

  // Laid-out paragraph: result of the expensive per-paragraph work (line-breaking,
  // image scaling, inline image resolution). Stored in a small ring-buffer cache
  // so that a paragraph spanning two pages is not re-laid-out on every layout() call.
  struct LaidOutParagraph {
    uint16_t para_idx = UINT16_MAX;  // UINT16_MAX = empty slot
    ParagraphType type = ParagraphType::Text;

    // ParagraphType::Text:
    bool text_runs_empty = false;  // true when para.text.runs is empty
    std::vector<LayoutLine> lines;
    InlineImageInfo inline_img;
    std::vector<uint16_t> line_heights;                       // pre-computed per-line heights
    std::vector<uint16_t> line_baselines;                     // pre-computed per-line baselines
    uint16_t inline_extra = 0;                                // extra px on line 0 for non-promoted inline image
    uint16_t promoted_w = 0, promoted_h = 0, promoted_x = 0;  // scaled promoted inline image

    // ParagraphType::Image (standalone), Hr, empty Text:
    uint16_t block_height = 0;                              // Image: scaled h (0=unknown); Hr/empty-text: font advance
    uint16_t img_key = 0, img_w = 0, img_h = 0, img_x = 0;  // Image paragraph only

    bool empty() const {
      return para_idx == UINT16_MAX;
    }

    // Result of collect(): the next item from this paragraph, or nullopt when
    // the paragraph is exhausted (no more items at idx).
    // collect() does NOT check whether the item fits on the page; the caller
    // is responsible for that.
    // next_idx: the idx to pass on the next call (idx+1 for most items; for
    //           image slices it advances by the slice height in pixels).
    struct Collected {
      PageItem item;
      size_t next_idx = 0;
    };

    // Returns the item at collect-index `idx` that fits within `available` pixels,
    // or nullopt when the paragraph is exhausted or the next item doesn't fit.
    // Dispatches to a per-type static helper (collect_text / collect_image /
    // collect_hr / collect_page_break). Each helper is responsible for computing
    // next_idx: idx+slice_h for image slices, idx+1 for everything else.
    std::optional<Collected> collect(size_t idx, uint16_t available) const;
  };

 private:
  CollectResult collect_page_items(PagePosition pos) const;
  PageContent assemble_page(std::vector<PageItem>& items, PagePosition start, PagePosition end,
                            bool at_chapter_end) const;
  static InlineImageInfo resolve_inline_image(const TextParagraph& text_para, uint16_t content_width,
                                              const ImageSizeQuery& sp);
  const LaidOutParagraph& get_laid_out_(size_t pi) const;

  IFont* font_ = nullptr;
  PageOptions opts_;
  IParagraphSource* source_ = nullptr;
  ImageSizeQuery size_fn_;
  PagePosition position_;

  static constexpr size_t kCacheCapacity = 8;
  mutable std::array<LaidOutParagraph, kCacheCapacity> para_cache_{};
  mutable size_t cache_next_ = 0;
  mutable bool cache_valid_ = false;
};

}  // namespace microreader
