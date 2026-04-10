#include "TextLayout.h"

#include <algorithm>
#include <cstring>

namespace microreader {

// ---------------------------------------------------------------------------
// UTF-8 helpers
// ---------------------------------------------------------------------------

static size_t utf8_codepoint_len(uint8_t b) {
  if (b < 0x80)
    return 1;
  if (b < 0xE0)
    return 2;
  if (b < 0xF0)
    return 3;
  return 4;
}

// Decode one UTF-8 codepoint, advance `pos`
static char32_t utf8_decode(const char* s, size_t len, size_t& pos) {
  if (pos >= len)
    return 0;
  uint8_t b = static_cast<uint8_t>(s[pos]);
  char32_t cp;
  size_t n;
  if (b < 0x80) {
    cp = b;
    n = 1;
  } else if (b < 0xE0) {
    cp = b & 0x1F;
    n = 2;
  } else if (b < 0xF0) {
    cp = b & 0x0F;
    n = 3;
  } else {
    cp = b & 0x07;
    n = 4;
  }
  for (size_t i = 1; i < n && pos + i < len; ++i)
    cp = (cp << 6) | (static_cast<uint8_t>(s[pos + i]) & 0x3F);
  pos += n;
  return cp;
}

// ---------------------------------------------------------------------------
// Word splitting helpers
// ---------------------------------------------------------------------------

// Check if byte is ASCII whitespace
static bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Check for Unicode whitespace at position `i` in text of length `len`.
// Returns the byte length of the whitespace sequence (0 if not whitespace).
// Handles ASCII whitespace and multi-byte Unicode space characters:
//   U+00A0 NBSP (C2 A0), U+2000–U+200A various spaces (E2 80 80–8A),
//   U+2028 LINE SEP (E2 80 A8), U+2029 PARA SEP (E2 80 A9),
//   U+202F NARROW NBSP (E2 80 AF), U+205F MATH SPACE (E2 81 9F),
//   U+3000 IDEOGRAPHIC SPACE (E3 80 80).
static size_t ws_len(const char* text, size_t len, size_t i) {
  uint8_t b = static_cast<uint8_t>(text[i]);
  if (b <= 0x7F)
    return is_ws(text[i]) ? 1 : 0;
  if (b == 0xC2 && i + 1 < len && static_cast<uint8_t>(text[i + 1]) == 0xA0)
    return 2;  // U+00A0 NBSP
  if (b == 0xE2 && i + 2 < len) {
    uint8_t b1 = static_cast<uint8_t>(text[i + 1]);
    uint8_t b2 = static_cast<uint8_t>(text[i + 2]);
    if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A)
      return 3;  // U+2000–U+200A
    if (b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF))
      return 3;  // U+2028, U+2029, U+202F
    if (b1 == 0x81 && b2 == 0x9F)
      return 3;  // U+205F
  }
  if (b == 0xE3 && i + 2 < len && static_cast<uint8_t>(text[i + 1]) == 0x80 &&
      static_cast<uint8_t>(text[i + 2]) == 0x80)
    return 3;  // U+3000
  return 0;
}

// Split text into (start, len) word spans, preserving byte offsets.
// Recognizes both ASCII and Unicode whitespace as word boundaries.
struct WordSpan {
  size_t start;
  size_t len;
};

static std::vector<WordSpan> split_words(const char* text, size_t text_len) {
  std::vector<WordSpan> spans;
  size_t i = 0;
  while (i < text_len) {
    // Skip whitespace (ASCII or Unicode)
    size_t wl;
    while (i < text_len && (wl = ws_len(text, text_len, i)) > 0)
      i += wl;
    if (i >= text_len)
      break;
    size_t start = i;
    // Advance through non-whitespace
    while (i < text_len && ws_len(text, text_len, i) == 0)
      ++i;
    spans.push_back({start, i - start});
  }
  return spans;
}

// ---------------------------------------------------------------------------
// Alignment helpers (matching TrustyReader's approach)
// ---------------------------------------------------------------------------

static void justify_words(uint16_t room, std::vector<LayoutWord>& words) {
  if (words.size() <= 1)
    return;
  // Count justifiable gaps (skip gaps before words that continue the previous)
  size_t gaps = 0;
  for (size_t i = 1; i < words.size(); ++i) {
    if (!words[i].continues_prev)
      ++gaps;
  }
  if (gaps == 0)
    return;
  uint16_t space_per_gap = room / static_cast<uint16_t>(gaps);
  uint16_t remainder = room % static_cast<uint16_t>(gaps);
  uint16_t offset = 0;
  for (size_t i = 1; i < words.size(); ++i) {
    if (!words[i].continues_prev) {
      uint16_t extra = space_per_gap + (remainder > 0 ? 1 : 0);
      if (remainder > 0)
        --remainder;
      offset += extra;
    }
    words[i].x += offset;
  }
}

static void nudge_words(uint16_t offset, std::vector<LayoutWord>& words) {
  for (auto& w : words)
    w.x += offset;
}

static void align_line(Alignment alignment, uint16_t room, std::vector<LayoutWord>& words, bool is_last_line) {
  if (words.empty())
    return;
  switch (alignment) {
    case Alignment::Start:
      break;
    case Alignment::Center:
      nudge_words(room / 2, words);
      break;
    case Alignment::End:
      nudge_words(room, words);
      break;
    case Alignment::Justify:
      if (is_last_line) {
        // Don't justify last line of paragraph
      } else {
        justify_words(room, words);
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// layout_paragraph() — greedy line-breaking with styled runs
// ---------------------------------------------------------------------------

std::vector<LayoutLine> layout_paragraph(const IFont& font, const LayoutOptions& opts, const TextParagraph& para) {
  const uint16_t max_width = opts.width;
  const uint16_t space_width = font.char_width(' ', FontStyle::Regular);
  const Alignment align = para.alignment.value_or(opts.alignment);
  const int16_t indent = para.indent.value_or(0);

  std::vector<LayoutLine> lines;
  LayoutLine current;
  uint16_t x = opts.first_line_extra_indent + ((indent > 0) ? static_cast<uint16_t>(indent) : 0);
  uint16_t cur_line_width = max_width;  // effective width accounting for margin_right
  bool first_line = true;
  bool prev_run_ended_space = true;  // true initially so first word is normal

  for (const auto& run : para.runs) {
    const char* text = run.text.c_str();
    const size_t text_len = run.text.size();

    // Effective line width accounting for right margin
    const uint16_t line_width = (run.margin_right < max_width) ? (max_width - run.margin_right) : max_width;
    cur_line_width = line_width;

    // Apply margin_left when starting a new line.
    // When margin_left is set, skip positive text-indent to keep all lines aligned (poems).
    // For negative indent (hanging indent), combine with margin_left on first line.
    if (current.words.empty() && run.margin_left > 0) {
      if (first_line && indent < 0) {
        int16_t combined = static_cast<int16_t>(run.margin_left) + indent;
        x = (combined > 0) ? static_cast<uint16_t>(combined) : 0;
      } else {
        x = run.margin_left;
      }
    }

    // Handle leading whitespace on empty lines (matching TrustyReader)
    if (current.words.empty()) {
      size_t i = 0;
      size_t wl;
      while (i < text_len && (wl = ws_len(text, text_len, i)) > 0) {
        x += font.char_width(' ', run.style, run.size);
        i += wl;
      }
    }

    auto spans = split_words(text, text_len);
    bool first_word_of_run = true;
    for (const auto& span : spans) {
      const char* word_ptr = text + span.start;
      uint16_t word_w = font.word_width(word_ptr, span.len, run.style, run.size);

      // Cross-run word continuation: if previous run didn't end with space
      // and this run's text doesn't start with space, place first word
      // immediately adjacent (no inter-word space).
      bool needs_space = !current.words.empty();
      if (first_word_of_run && !prev_run_ended_space && text_len > 0 && ws_len(text, text_len, 0) == 0) {
        needs_space = false;
      }

      // Check if word fits on current line
      uint16_t needed = word_w;
      if (needs_space)
        needed += space_width;

      if (x + needed > line_width && !current.words.empty()) {
        // Finish current line
        uint16_t room = line_width > x ? (line_width - x) : 0;
        align_line(align, room, current.words, false);
        lines.push_back(std::move(current));
        current = LayoutLine{};
        x = run.margin_left;  // re-apply margin on wrapped line
        first_line = false;
        needs_space = false;
      }

      // Add space before word if needed
      if (needs_space) {
        x += space_width;
      }

      current.words.push_back(LayoutWord{word_ptr, static_cast<uint16_t>(span.len), x, run.style, run.size,
                                         run.vertical_align, !needs_space && !current.words.empty()});
      x += word_w;
      first_word_of_run = false;
    }

    // Track whether this run ended with whitespace.
    // For multi-byte Unicode whitespace, check last few bytes.
    prev_run_ended_space = text_len == 0;
    if (text_len > 0) {
      if (is_ws(text[text_len - 1])) {
        prev_run_ended_space = true;
      } else if (text_len >= 2 && ws_len(text, text_len, text_len - 2) == 2) {
        prev_run_ended_space = true;
      } else if (text_len >= 3 && ws_len(text, text_len, text_len - 3) == 3) {
        prev_run_ended_space = true;
      }
    }

    // Handle explicit line break
    if (run.breaking) {
      uint16_t room = cur_line_width > x ? (cur_line_width - x) : 0;
      // For breaking runs, treat as last line (don't justify)
      align_line(align, room, current.words, true);
      lines.push_back(std::move(current));
      current = LayoutLine{};
      x = 0;
      first_line = false;
      prev_run_ended_space = true;
    }
  }

  // Push remaining words as last line
  if (!current.words.empty()) {
    uint16_t room = cur_line_width > x ? (cur_line_width - x) : 0;
    align_line(align, room, current.words, true);
    lines.push_back(std::move(current));
  }

  return lines;
}

// ---------------------------------------------------------------------------
// Shared page layout helpers
// ---------------------------------------------------------------------------

// A single item on a page — text line, block image, HR, or empty paragraph.
struct PageItem {
  enum Kind { TextLine, Image, Hr, Empty };
  Kind kind;
  uint16_t para_idx;
  uint16_t line_idx;       // for TextLine
  LayoutLine layout_line;  // for TextLine
  uint16_t height;
  // Image fields (only for Image kind)
  uint16_t img_key = 0;
  uint16_t img_w = 0, img_h = 0;
  uint16_t img_x = 0;
};

// Scale a block image to fit page constraints.
// Large images (>= half page width) scale up to full page width.
// Small images scale down only if wider than content area.
// All images are capped to page height and clamped to >= 1px.
static void scale_image(const PageOptions& opts, uint16_t content_width, uint16_t& img_w, uint16_t& img_h,
                        uint16_t& x_off) {
  const uint16_t full_width = opts.width;
  const uint16_t full_height = opts.height;

  if (img_w >= full_width / 2) {
    if (img_w != full_width) {
      img_h = static_cast<uint16_t>(static_cast<uint32_t>(img_h) * full_width / img_w);
      img_w = full_width;
    }
  } else {
    if (img_w > content_width) {
      img_h = static_cast<uint16_t>(static_cast<uint32_t>(img_h) * content_width / img_w);
      img_w = content_width;
    }
  }
  // Always scale proportionally — preserve aspect ratio in all cases.
  if (img_h > full_height) {
    img_w = static_cast<uint16_t>(static_cast<uint32_t>(img_w) * full_height / img_h);
    img_h = full_height;
  }
  if (img_w == 0)
    img_w = 1;
  if (img_h == 0)
    img_h = 1;

  if (img_w >= full_width) {
    x_off = 0;
  } else if (img_w > content_width) {
    x_off = (full_width > img_w) ? (full_width - img_w) / 2 : 0;
  } else {
    x_off = opts.padding + (content_width > img_w ? (content_width - img_w) / 2 : 0);
  }
}

// Compute height of a text line, accounting for mixed font sizes and line_height_pct.
static uint16_t compute_line_height(const IFont& font, const LayoutLine& line, uint16_t line_height_pct) {
  uint16_t h = font.y_advance();
  for (const auto& w : line.words) {
    uint16_t wh = font.y_advance(w.size);
    if (wh > h)
      h = wh;
  }
  if (line_height_pct != 100) {
    h = static_cast<uint16_t>(h * line_height_pct / 100);
    if (h < 1)
      h = 1;
  }
  return h;
}

// Convert collected items (in forward order) to PageContent.
// Computes y-offsets and paragraph spacing from paragraph transitions.
static PageContent assemble_page(const PageOptions& opts, const IFont& font, IParagraphSource& source,
                                 std::vector<PageItem>& items, PagePosition start, PagePosition end,
                                 bool at_chapter_end, bool center_sparse_text) {
  const uint16_t content_width = opts.width - 2 * opts.padding;
  const uint16_t default_y_advance = font.y_advance();

  PageContent page;
  page.start = start;
  page.end = end;
  page.at_chapter_end = at_chapter_end;

  uint16_t y = 0;
  uint16_t prev_para = UINT16_MAX;

  for (auto& item : items) {
    // Paragraph spacing on transitions between different paragraphs
    if (prev_para != UINT16_MAX && item.para_idx != prev_para) {
      y += source.paragraph(item.para_idx).spacing_before.value_or(opts.para_spacing);
    }

    switch (item.kind) {
      case PageItem::TextLine:
        page.text_items.push_back(PageTextItem{item.para_idx, item.line_idx, std::move(item.layout_line), y});
        break;
      case PageItem::Image:
        page.image_items.push_back(PageImageItem{item.para_idx, item.img_key, item.img_w, item.img_h, item.img_x, y});
        break;
      case PageItem::Hr: {
        uint16_t hr_y = y + default_y_advance / 2;
        page.hr_items.push_back(PageHrItem{opts.padding, hr_y, content_width});
        break;
      }
      case PageItem::Empty:
        break;
    }

    y += item.height;
    prev_para = item.para_idx;
  }

  // vertical_offset is an absolute screen Y — where the content block starts.
  // Default: normal top padding keeps content within the padded area.
  page.vertical_offset = opts.effective_padding_top();

  // Image-only page: center the image block on the full screen.
  if (page.text_items.empty() && !page.image_items.empty()) {
    page.vertical_offset = static_cast<uint16_t>(opts.height > y ? (opts.height - y) / 2 : 0);
  }
  // Sparse text at end of single-page chapter: center on full screen.
  else if (center_sparse_text && at_chapter_end && !page.text_items.empty() && page.image_items.empty() &&
           y <= opts.height / 2) {
    page.vertical_offset = static_cast<uint16_t>((opts.height - y) / 2);
  }

  return page;
}

// ---------------------------------------------------------------------------
// layout_page() — fill a page with paragraphs from a chapter
// ---------------------------------------------------------------------------

PageContent layout_page(const IFont& font, const PageOptions& opts, IParagraphSource& source, PagePosition start,
                        const ImageSizeQuery& size_provider) {
  const uint16_t content_width = opts.width - 2 * opts.padding;
  const uint16_t default_y_advance = font.y_advance();
  const uint16_t page_height = opts.height - opts.effective_padding_top() - opts.padding;
  const size_t para_count = source.paragraph_count();

  LayoutOptions lo;
  lo.width = content_width;
  lo.alignment = opts.alignment;

  std::vector<PageItem> items;
  uint16_t y = 0;  // height budget
  bool has_content = false;
  bool has_text_or_image = false;
  bool at_chapter_end = false;
  PagePosition end_pos = start;

  // Inline image tracking for post-assembly placement
  struct PendingInlineImage {
    uint16_t para_idx, key, width, height;
  };
  std::vector<PendingInlineImage> pending_inline_images;

  for (size_t pi = start.paragraph; pi < para_count; ++pi) {
    const auto& para = source.paragraph(pi);
    uint16_t spacing = has_content ? para.spacing_before.value_or(opts.para_spacing) : 0;

    switch (para.type) {
      case ParagraphType::Text: {
        if (para.text.runs.empty()) {
          if (y + spacing + default_y_advance > page_height && has_content) {
            end_pos = {static_cast<uint16_t>(pi), 0};
            goto assemble;
          }
          y += spacing + default_y_advance;
          items.push_back(PageItem{PageItem::Empty, static_cast<uint16_t>(pi), 0, {}, default_y_advance});
          has_content = true;
          end_pos = {static_cast<uint16_t>(pi + 1), 0};
          continue;
        }

        size_t skip = (pi == start.paragraph) ? start.line : 0;

        // Inline float image setup — resolve dimensions, then decide:
        // large images are promoted to standalone block items before the text;
        // small ones keep first-line indent to wrap text beside them.
        uint16_t inline_img_w = 0, inline_img_h = 0;
        bool has_inline_img = false;
        if (skip == 0 && para.text.inline_image.has_value()) {
          const auto& img = *para.text.inline_image;
          if (img.attr_width > 0 && img.attr_height > 0) {
            inline_img_w = img.attr_width;
            inline_img_h = img.attr_height;
          } else if (size_provider) {
            size_provider(img.key, inline_img_w, inline_img_h);
          }
          if (inline_img_w > 0 && inline_img_h > 0) {
            if (inline_img_w > content_width / 3 || inline_img_h > 120) {
              // Promote: emit as a standalone block image before this paragraph's text.
              uint16_t promo_w = inline_img_w, promo_h = inline_img_h;
              uint16_t promo_x;
              scale_image(opts, content_width, promo_w, promo_h, promo_x);
              if (y + spacing + promo_h > page_height && has_content) {
                end_pos = {static_cast<uint16_t>(pi), 0};
                goto assemble;
              }
              y += spacing + promo_h;
              items.push_back(PageItem{PageItem::Image,
                                       static_cast<uint16_t>(pi),
                                       0,
                                       {},
                                       promo_h,
                                       img.key,
                                       promo_w,
                                       promo_h,
                                       promo_x});
              has_content = true;
              has_text_or_image = true;
              spacing = 0;  // consumed by image; don't re-apply before text lines
            } else {
              has_inline_img = true;
              lo.first_line_extra_indent = inline_img_w + 4;
            }
          }
        }

        auto lines = layout_paragraph(font, lo, para.text);
        lo.first_line_extra_indent = 0;

        // Extra spacing so inline image doesn't overlap previous content
        const uint16_t baseline_off = font.baseline();
        uint16_t inline_extra = 0;
        if (has_inline_img && inline_img_h > baseline_off) {
          inline_extra = inline_img_h - baseline_off;
          if (y + spacing + inline_extra + default_y_advance > page_height && has_content) {
            end_pos = {static_cast<uint16_t>(pi), 0};
            goto assemble;
          }
        }

        bool placed_any_line = false;
        for (size_t li = skip; li < lines.size(); ++li) {
          uint16_t line_h = compute_line_height(font, lines[li], para.text.line_height_pct);
          uint16_t above = placed_any_line ? 0 : (spacing + inline_extra);

          if (y + above + line_h > page_height && has_content) {
            end_pos = {static_cast<uint16_t>(pi), static_cast<uint16_t>(li)};
            if (has_inline_img && placed_any_line) {
              pending_inline_images.push_back(
                  {static_cast<uint16_t>(pi), para.text.inline_image->key, inline_img_w, inline_img_h});
            }
            goto assemble;
          }

          // Emit inline_extra as an invisible spacer before the first line
          if (!placed_any_line && inline_extra > 0) {
            items.push_back(PageItem{PageItem::Empty, static_cast<uint16_t>(pi), 0, {}, inline_extra});
          }

          y += above + line_h;
          items.push_back(PageItem{PageItem::TextLine, static_cast<uint16_t>(pi), static_cast<uint16_t>(li),
                                   std::move(lines[li]), line_h});
          has_content = true;
          has_text_or_image = true;
          placed_any_line = true;
        }

        if (has_inline_img && placed_any_line) {
          pending_inline_images.push_back(
              {static_cast<uint16_t>(pi), para.text.inline_image->key, inline_img_w, inline_img_h});
        }

        end_pos = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }

      case ParagraphType::Image: {
        uint16_t img_w = para.image.attr_width;
        uint16_t img_h = para.image.attr_height;

        if ((img_w == 0 || img_h == 0) && size_provider)
          size_provider(para.image.key, img_w, img_h);

        if (img_w == 0 || img_h == 0) {
          end_pos = {static_cast<uint16_t>(pi + 1), 0};
          break;
        }

        uint16_t x_off;
        scale_image(opts, content_width, img_w, img_h, x_off);

        if (y + spacing + img_h > page_height && has_content) {
          end_pos = {static_cast<uint16_t>(pi), 0};
          goto assemble;
        }

        y += spacing + img_h;
        items.push_back(
            PageItem{PageItem::Image, static_cast<uint16_t>(pi), 0, {}, img_h, para.image.key, img_w, img_h, x_off});
        has_content = true;
        has_text_or_image = true;
        end_pos = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }

      case ParagraphType::Hr: {
        if (y + spacing + default_y_advance > page_height) {
          if (has_text_or_image) {
            end_pos = {static_cast<uint16_t>(pi), 0};
            goto assemble;
          }
          // Drop the HR — it would be stranded on an otherwise empty page
          end_pos = {static_cast<uint16_t>(pi + 1), 0};
          break;
        }
        y += spacing + default_y_advance;
        items.push_back(PageItem{PageItem::Hr, static_cast<uint16_t>(pi), 0, {}, default_y_advance});
        has_content = true;
        end_pos = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }

      case ParagraphType::PageBreak: {
        if (has_text_or_image) {
          bool has_remaining = false;
          for (size_t ri = pi + 1; ri < para_count; ++ri) {
            if (source.paragraph(ri).type != ParagraphType::PageBreak) {
              has_remaining = true;
              break;
            }
          }
          if (has_remaining) {
            end_pos = {static_cast<uint16_t>(pi + 1), 0};
            goto assemble;
          }
        }
        end_pos = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }
    }
  }

  at_chapter_end = true;

assemble: {
  bool is_first_page = (start.paragraph == 0 && start.line == 0);
  auto page = assemble_page(opts, font, source, items, start, end_pos, at_chapter_end, is_first_page);

  // Post-process: place inline images relative to first text line baseline
  for (auto& pii : pending_inline_images) {
    for (auto& ti : page.text_items) {
      if (ti.paragraph_index == pii.para_idx) {
        uint16_t baseline_y = ti.y_offset + font.baseline();
        uint16_t img_y = (baseline_y >= pii.height) ? (baseline_y - pii.height) : 0;
        page.image_items.push_back(PageImageItem{pii.para_idx, pii.key, pii.width, pii.height, opts.padding, img_y});
        break;
      }
    }
  }

  return page;
}
}

// ---------------------------------------------------------------------------
// layout_page_backward() — fill a page from an end position, working backward
// ---------------------------------------------------------------------------
//
// Uses the same helpers as layout_page(). Collects items walking backward
// from `end`, accumulates heights, then reverses and calls assemble_page().
// Word-wrapping is identical because we call layout_paragraph() on each
// text paragraph and then pick lines from the end.

PageContent layout_page_backward(const IFont& font, const PageOptions& opts, IParagraphSource& source, PagePosition end,
                                 const ImageSizeQuery& size_provider) {
  const uint16_t content_width = opts.width - 2 * opts.padding;
  const uint16_t default_y_advance = font.y_advance();
  const uint16_t page_height = opts.height - opts.effective_padding_top() - opts.padding;
  const size_t para_count = source.paragraph_count();

  if (end.paragraph == 0 && end.line == 0) {
    PageContent page;
    page.start = {0, 0};
    page.end = {0, 0};
    return page;
  }

  LayoutOptions lo;
  lo.width = content_width;
  lo.alignment = opts.alignment;

  // Items collected in reverse order, then reversed before assembly.
  std::vector<PageItem> items;
  uint16_t total_height = 0;
  bool page_full = false;

  // Determine first paragraph and line limit.
  // `end` is one-past-end: line>0 means partial paragraph, line==0 means start from previous.
  int first_pi;
  size_t first_line_limit;
  if (end.line > 0) {
    first_pi = static_cast<int>(end.paragraph);
    first_line_limit = end.line;
  } else {
    first_pi = static_cast<int>(end.paragraph) - 1;
    first_line_limit = SIZE_MAX;
  }

  for (int pi = first_pi; pi >= 0 && !page_full; --pi) {
    if (static_cast<size_t>(pi) >= para_count)
      break;

    size_t line_limit = (pi == first_pi) ? first_line_limit : SIZE_MAX;
    const auto& para = source.paragraph(static_cast<size_t>(pi));

    // Inter-paragraph spacing: determined by the paragraph BELOW (already collected).
    uint16_t inter_spacing = 0;
    if (!items.empty()) {
      uint16_t below_pi = items.back().para_idx;
      if (below_pi < para_count) {
        inter_spacing = source.paragraph(below_pi).spacing_before.value_or(opts.para_spacing);
      }
    }

    switch (para.type) {
      case ParagraphType::Text: {
        if (para.text.runs.empty()) {
          uint16_t h = default_y_advance;
          if (total_height + h + inter_spacing > page_height && !items.empty()) {
            page_full = true;
            break;
          }
          total_height += h + inter_spacing;
          items.push_back(PageItem{PageItem::Empty, static_cast<uint16_t>(pi), 0, {}, h});
          break;
        }

        // Inline image — resolve dimensions, matching the forward path so line wrapping is identical.
        // Small images keep first-line indent; large ones are promoted to a block item preceding the text.
        uint16_t inline_img_w = 0, inline_img_h = 0;
        uint16_t inline_img_key = 0;
        bool inline_img_promoted = false;
        if (para.text.inline_image.has_value()) {
          const auto& img = para.text.inline_image.value();
          inline_img_key = img.key;
          if (img.attr_width > 0 && img.attr_height > 0) {
            inline_img_w = img.attr_width;
            inline_img_h = img.attr_height;
          } else if (size_provider) {
            size_provider(img.key, inline_img_w, inline_img_h);
          }
          if (inline_img_w > 0 && inline_img_h > 0) {
            if (inline_img_w > content_width / 3 || inline_img_h > 120) {
              inline_img_promoted = true;
            } else {
              lo.first_line_extra_indent = inline_img_w + 4;
            }
          }
        }

        auto lines = layout_paragraph(font, lo, para.text);
        lo.first_line_extra_indent = 0;
        size_t line_count = lines.size();
        size_t last_line = (line_limit < line_count) ? line_limit : line_count;

        // Collect lines from (last_line - 1) down to 0
        bool first_item_of_para = true;
        bool collected_line_zero = false;
        for (size_t li_rev = 0; li_rev < last_line; ++li_rev) {
          size_t li = last_line - 1 - li_rev;
          uint16_t line_h = compute_line_height(font, lines[li], para.text.line_height_pct);

          // Spacing only before the first item of a paragraph (between paragraphs)
          uint16_t sp = (first_item_of_para && !items.empty()) ? inter_spacing : 0;

          if (total_height + line_h + sp > page_height && !items.empty()) {
            page_full = true;
            break;
          }

          total_height += line_h + sp;
          items.push_back(PageItem{PageItem::TextLine, static_cast<uint16_t>(pi), static_cast<uint16_t>(li),
                                   std::move(lines[li]), line_h});
          first_item_of_para = false;
          if (li == 0)
            collected_line_zero = true;
        }

        // If line 0 was collected, also emit the promoted image (appended last here,
        // placed first after std::reverse — matching the forward layout order).
        if (inline_img_promoted && collected_line_zero) {
          uint16_t promo_w = inline_img_w, promo_h = inline_img_h;
          uint16_t promo_x;
          scale_image(opts, content_width, promo_w, promo_h, promo_x);
          total_height += promo_h;
          items.push_back(PageItem{PageItem::Image,
                                   static_cast<uint16_t>(pi),
                                   0,
                                   {},
                                   promo_h,
                                   inline_img_key,
                                   promo_w,
                                   promo_h,
                                   promo_x});
        }
        break;
      }

      case ParagraphType::Image: {
        uint16_t img_w = para.image.attr_width;
        uint16_t img_h = para.image.attr_height;

        if ((img_w == 0 || img_h == 0) && size_provider)
          size_provider(para.image.key, img_w, img_h);

        if (img_w == 0 || img_h == 0)
          break;

        uint16_t x_off;
        scale_image(opts, content_width, img_w, img_h, x_off);

        if (total_height + img_h + inter_spacing > page_height && !items.empty()) {
          page_full = true;
          break;
        }

        total_height += img_h + inter_spacing;
        items.push_back(
            PageItem{PageItem::Image, static_cast<uint16_t>(pi), 0, {}, img_h, para.image.key, img_w, img_h, x_off});
        break;
      }

      case ParagraphType::Hr: {
        uint16_t h = default_y_advance;
        if (total_height + h + inter_spacing > page_height && !items.empty()) {
          page_full = true;
          break;
        }
        total_height += h + inter_spacing;
        items.push_back(PageItem{PageItem::Hr, static_cast<uint16_t>(pi), 0, {}, h});
        break;
      }

      case ParagraphType::PageBreak: {
        if (!items.empty()) {
          page_full = true;
        }
        break;
      }
    }
  }

  // Reverse to forward order and assemble.
  std::reverse(items.begin(), items.end());

  if (items.empty()) {
    PageContent page;
    page.start = end;
    page.end = end;
    page.at_chapter_end = (end.paragraph >= para_count);
    return page;
  }

  PagePosition start_pos = {items.front().para_idx, items.front().line_idx};
  bool at_chapter_end = (start_pos == PagePosition{0, 0} && end.paragraph >= para_count);

  return assemble_page(opts, font, source, items, start_pos, end, at_chapter_end, false);
}

}  // namespace microreader
