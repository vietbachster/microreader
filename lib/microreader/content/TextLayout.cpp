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

// Check if character is whitespace
static bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Split text into (start, len) word spans, preserving byte offsets
struct WordSpan {
  size_t start;
  size_t len;
};

static std::vector<WordSpan> split_words(const char* text, size_t text_len) {
  std::vector<WordSpan> spans;
  size_t i = 0;
  while (i < text_len) {
    // Skip whitespace
    while (i < text_len && is_ws(text[i]))
      ++i;
    if (i >= text_len)
      break;
    size_t start = i;
    // Advance through non-whitespace
    while (i < text_len && !is_ws(text[i]))
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
      while (i < text_len && is_ws(text[i])) {
        x += font.char_width(' ', run.style, run.size);
        ++i;
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
      if (first_word_of_run && !prev_run_ended_space && text_len > 0 && !is_ws(text[0])) {
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

    // Track whether this run ended with whitespace
    prev_run_ended_space = (text_len > 0 && is_ws(text[text_len - 1])) || text_len == 0;

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
// layout_page() — fill a page with paragraphs from a chapter
// ---------------------------------------------------------------------------

PageContent layout_page(const IFont& font, const PageOptions& opts, const Chapter& chapter, PagePosition start) {
  PageContent page;
  page.start = start;

  const uint16_t content_width = opts.width - 2 * opts.padding;
  const uint16_t default_y_advance = font.y_advance();
  const uint16_t page_height = opts.height - 2 * opts.padding;

  LayoutOptions lo;
  lo.width = content_width;
  lo.alignment = opts.alignment;

  uint16_t y = 0;
  bool has_content = false;
  bool has_text_or_image = false;  // true only for text/image (not HR alone)

  // Vertically center image-only pages (no text)
  auto center_images = [&]() {
    if (page.text_items.empty() && !page.image_items.empty() && y < opts.height) {
      page.vertical_offset = (opts.height - y) / 2;
    }
  };

  for (size_t pi = start.paragraph; pi < chapter.paragraphs.size(); ++pi) {
    const auto& para = chapter.paragraphs[pi];

    // Paragraph spacing (except before the very first content)
    if (has_content) {
      y += para.spacing_before.value_or(opts.para_spacing);
    }

    switch (para.type) {
      case ParagraphType::Text: {
        if (para.text.runs.empty()) {
          // Empty paragraph (from standalone <br/>) — advance by one line
          y += default_y_advance;
          has_content = true;
          page.end = {static_cast<uint16_t>(pi + 1), 0};
          continue;
        }

        size_t skip = (pi == start.paragraph) ? start.line : 0;

        // Set up first-line indent for inline float image
        uint16_t inline_img_w = 0, inline_img_h = 0;
        bool has_inline_img = false;
        if (skip == 0 && para.text.inline_image.has_value()) {
          const auto& img = *para.text.inline_image;
          if (img.width > 0 && img.height > 0) {
            inline_img_w = img.width;
            inline_img_h = img.height;
            has_inline_img = true;
            lo.first_line_extra_indent = inline_img_w + 4;  // 4px gap
          }
        }

        auto lines = layout_paragraph(font, lo, para.text);
        lo.first_line_extra_indent = 0;  // reset for next paragraph

        // Add extra spacing so the inline image doesn't overlap previous content.
        // The image bottom sits at the baseline, so it extends (img_h - baseline)
        // above the first text line. Advance y by that excess.
        const uint16_t baseline_off = font.baseline();
        if (has_inline_img && inline_img_h > baseline_off) {
          uint16_t extra = inline_img_h - baseline_off;
          if (y + extra + default_y_advance > page_height && has_content) {
            page.end = {static_cast<uint16_t>(pi), 0};
            center_images();
            return page;
          }
          y += extra;
        }

        uint16_t first_line_top = 0;
        uint16_t first_line_h = 0;
        bool first_line_seen = false;

        for (size_t li = skip; li < lines.size(); ++li) {
          // Compute line height from the tallest font size in this line
          uint16_t line_y = default_y_advance;
          for (const auto& w : lines[li].words) {
            uint16_t h = font.y_advance(w.size);
            if (h > line_y)
              line_y = h;
          }
          // Apply paragraph line-height scaling
          if (para.text.line_height_pct != 100) {
            line_y = static_cast<uint16_t>(line_y * para.text.line_height_pct / 100);
            if (line_y < 1)
              line_y = 1;
          }

          if (y + line_y > page_height && has_content) {
            // Page full — this line doesn't fit
            page.end = {static_cast<uint16_t>(pi), static_cast<uint16_t>(li)};
            // Emit inline image if first line was placed on this page
            if (has_inline_img && first_line_seen) {
              uint16_t baseline_y = first_line_top + baseline_off;
              uint16_t img_y = (baseline_y >= inline_img_h) ? (baseline_y - inline_img_h) : 0;
              page.image_items.push_back(PageImageItem{static_cast<uint16_t>(pi), para.text.inline_image->key,
                                                       inline_img_w, inline_img_h, opts.padding, img_y});
            }
            center_images();
            return page;
          }

          if (!first_line_seen) {
            first_line_top = y;
            first_line_h = line_y;
            first_line_seen = true;
          }

          page.text_items.push_back(
              PageTextItem{static_cast<uint16_t>(pi), static_cast<uint16_t>(li), std::move(lines[li]), y});

          y += line_y;
          has_content = true;
          has_text_or_image = true;
        }

        // Emit inline image with bottom aligned to first line baseline
        if (has_inline_img && first_line_seen) {
          uint16_t baseline_y = first_line_top + baseline_off;
          uint16_t img_y = (baseline_y >= inline_img_h) ? (baseline_y - inline_img_h) : 0;
          page.image_items.push_back(PageImageItem{static_cast<uint16_t>(pi), para.text.inline_image->key, inline_img_w,
                                                   inline_img_h, opts.padding, img_y});
        }

        // Finished this paragraph entirely
        page.end = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }

      case ParagraphType::Image: {
        uint16_t img_w = para.image.width;
        uint16_t img_h = para.image.height;

        // Skip images with unknown dimensions — they can't be laid out
        if (img_w == 0 || img_h == 0) {
          page.end = {static_cast<uint16_t>(pi + 1), 0};
          break;
        }

        // Images use the full page width (no padding margins)
        const uint16_t full_width = opts.width;
        const uint16_t full_height = opts.height;

        // Only scale up to full width if the image is already at least half
        // the page width. Smaller images (icons, vignettes) keep their
        // intrinsic size — scaling them up would pixelate badly.
        if (img_w >= full_width / 2) {
          // Large image: scale to fill page width
          if (img_w != full_width) {
            img_h = static_cast<uint16_t>(static_cast<uint32_t>(img_h) * full_width / img_w);
            img_w = full_width;
          }
        } else {
          // Small image: scale down only if wider than content area
          if (img_w > content_width) {
            img_h = static_cast<uint16_t>(static_cast<uint32_t>(img_h) * content_width / img_w);
            img_w = content_width;
          }
        }
        // Cap to full page height
        if (img_h > full_height) {
          img_w = static_cast<uint16_t>(static_cast<uint32_t>(img_w) * full_height / img_h);
          img_h = full_height;
        }
        // Clamp dimensions to at least 1px
        if (img_w == 0)
          img_w = 1;
        if (img_h == 0)
          img_h = 1;

        if (y + img_h > page_height && has_content) {
          page.end = {static_cast<uint16_t>(pi), 0};
          center_images();
          return page;
        }

        // Center image: full-width images on the full page, smaller on content area
        uint16_t x_off;
        if (img_w >= full_width) {
          x_off = 0;
        } else if (img_w > content_width) {
          x_off = (full_width > img_w) ? (full_width - img_w) / 2 : 0;
        } else {
          // Small image: center within content area (offset by padding)
          x_off = opts.padding + (content_width > img_w ? (content_width - img_w) / 2 : 0);
        }

        page.image_items.push_back(PageImageItem{static_cast<uint16_t>(pi), para.image.key, img_w, img_h, x_off, y});
        y += img_h;
        has_content = true;
        has_text_or_image = true;
        page.end = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }

      case ParagraphType::Hr: {
        // HR takes one line height. If it doesn't fit and the page has only
        // images (no text), silently consume it rather than stranding it alone
        // on the next page.
        if (y + default_y_advance > page_height) {
          if (!page.text_items.empty()) {
            page.end = {static_cast<uint16_t>(pi), 0};
            center_images();
            return page;
          }
          // Drop the HR — it would be stranded on an otherwise empty page.
          page.end = {static_cast<uint16_t>(pi + 1), 0};
          break;
        }
        // Center the HR vertically in the line, draw it across the content area
        uint16_t hr_y = y + default_y_advance / 2;
        page.hr_items.push_back(PageHrItem{opts.padding, hr_y, content_width});
        y += default_y_advance;
        has_content = true;
        page.end = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }

      case ParagraphType::PageBreak: {
        // Force a new page, but only if page has text or images and there
        // is remaining content after the break (avoid trailing empty pages).
        if (has_text_or_image) {
          bool has_remaining = false;
          for (size_t ri = pi + 1; ri < chapter.paragraphs.size(); ++ri) {
            if (chapter.paragraphs[ri].type != ParagraphType::PageBreak) {
              has_remaining = true;
              break;
            }
          }
          if (has_remaining) {
            page.end = {static_cast<uint16_t>(pi + 1), 0};
            center_images();
            return page;
          }
        }
        // No content yet or nothing after break — just skip it
        page.end = {static_cast<uint16_t>(pi + 1), 0};
        break;
      }
    }
  }

  page.at_chapter_end = true;

  // Vertical centering at chapter end
  center_images();
  // Also center sparse text on single-page chapters
  bool is_single_page = (start.paragraph == 0 && start.line == 0);
  if (is_single_page && page.vertical_offset == 0 && !page.text_items.empty() && page.image_items.empty() &&
      y <= page_height / 2) {
    page.vertical_offset = (page_height - y) / 2;
  }

  return page;
}

}  // namespace microreader
