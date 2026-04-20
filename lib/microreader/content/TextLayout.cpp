#include "TextLayout.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace microreader {

// ---------------------------------------------------------------------------
// UTF-8 / word-splitting helpers
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

static bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static size_t ws_len(const char* text, size_t len, size_t i) {
  uint8_t b = static_cast<uint8_t>(text[i]);
  if (b <= 0x7F)
    return is_ws(text[i]) ? 1 : 0;
  if (b == 0xC2 && i + 1 < len && static_cast<uint8_t>(text[i + 1]) == 0xA0)
    return 2;
  if (b == 0xE2 && i + 2 < len) {
    uint8_t b1 = static_cast<uint8_t>(text[i + 1]), b2 = static_cast<uint8_t>(text[i + 2]);
    if (b1 == 0x80 && ((b2 >= 0x80 && b2 <= 0x8A) || b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF))
      return 3;
    if (b1 == 0x81 && b2 == 0x9F)
      return 3;
  }
  if (b == 0xE3 && i + 2 < len && static_cast<uint8_t>(text[i + 1]) == 0x80 &&
      static_cast<uint8_t>(text[i + 2]) == 0x80)
    return 3;
  return 0;
}

struct WordSpan {
  size_t start, len;
};

static std::vector<WordSpan> split_words(const char* text, size_t text_len) {
  std::vector<WordSpan> spans;
  size_t i = 0;
  while (i < text_len) {
    size_t wl;
    while (i < text_len && (wl = ws_len(text, text_len, i)) > 0)
      i += wl;
    if (i >= text_len)
      break;
    size_t start = i;
    while (i < text_len && ws_len(text, text_len, i) == 0)
      ++i;
    spans.push_back({start, i - start});
  }
  return spans;
}

// ---------------------------------------------------------------------------
// Alignment helpers
// ---------------------------------------------------------------------------

static void justify_words(uint16_t room, std::vector<LayoutWord>& words) {
  if (words.size() <= 1)
    return;
  size_t gaps = 0;
  for (size_t i = 1; i < words.size(); ++i)
    if (!words[i].continues_prev)
      ++gaps;
  if (gaps == 0)
    return;
  uint16_t spc = room / static_cast<uint16_t>(gaps);
  uint16_t rem = room % static_cast<uint16_t>(gaps);
  uint16_t off = 0;
  for (size_t i = 1; i < words.size(); ++i) {
    if (!words[i].continues_prev) {
      off += spc + (rem > 0 ? 1 : 0);
      if (rem > 0)
        --rem;
    }
    words[i].x += off;
  }
}

static void align_line(Alignment alignment, uint16_t room, std::vector<LayoutWord>& words, bool is_last) {
  if (words.empty())
    return;
  switch (alignment) {
    case Alignment::Center:
      for (auto& w : words)
        w.x += room / 2;
      break;
    case Alignment::End:
      for (auto& w : words)
        w.x += room;
      break;
    case Alignment::Justify:
      if (!is_last)
        justify_words(room, words);
      break;
    default:
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
  uint16_t cur_line_width = max_width;
  bool first_line = true, prev_run_ended_space = true;

  for (const auto& run : para.runs) {
    const char* text = run.text.c_str();
    const size_t text_len = run.text.size();
    const uint16_t line_width = (run.margin_right < max_width) ? (max_width - run.margin_right) : max_width;
    cur_line_width = line_width;

    if (current.words.empty() && run.margin_left > 0) {
      if (first_line && indent < 0) {
        int16_t combined = static_cast<int16_t>(run.margin_left) + indent;
        x = (combined > 0) ? static_cast<uint16_t>(combined) : 0;
      } else {
        x = run.margin_left;
      }
    }

    if (current.words.empty()) {
      size_t i = 0, wl;
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
      bool needs_space = !current.words.empty();
      if (first_word_of_run && !prev_run_ended_space && text_len > 0 && ws_len(text, text_len, 0) == 0)
        needs_space = false;
      uint16_t needed = word_w + (needs_space ? space_width : 0);
      if (x + needed > line_width && !current.words.empty()) {
        uint16_t room = line_width > x ? line_width - x : 0;
        align_line(align, room, current.words, false);
        lines.push_back(std::move(current));
        current = LayoutLine{};
        x = run.margin_left;
        first_line = false;
        needs_space = false;
      }
      if (needs_space)
        x += space_width;
      current.words.push_back(LayoutWord{word_ptr, static_cast<uint16_t>(span.len), x, run.style, run.size,
                                         run.vertical_align, !needs_space && !current.words.empty()});
      x += word_w;
      first_word_of_run = false;
    }

    prev_run_ended_space = text_len == 0;
    if (text_len > 0) {
      if (is_ws(text[text_len - 1]))
        prev_run_ended_space = true;
      else if (text_len >= 2 && ws_len(text, text_len, text_len - 2) == 2)
        prev_run_ended_space = true;
      else if (text_len >= 3 && ws_len(text, text_len, text_len - 3) == 3)
        prev_run_ended_space = true;
    }

    if (run.breaking) {
      uint16_t room = cur_line_width > x ? cur_line_width - x : 0;
      align_line(align, room, current.words, true);
      lines.push_back(std::move(current));
      current = LayoutLine{};
      x = 0;
      first_line = false;
      prev_run_ended_space = true;
    }
  }

  if (!current.words.empty()) {
    uint16_t room = cur_line_width > x ? cur_line_width - x : 0;
    align_line(align, room, current.words, true);
    lines.push_back(std::move(current));
  }
  return lines;
}

// ---------------------------------------------------------------------------
// Page layout types and shared helpers
// ---------------------------------------------------------------------------

struct PageItem {
  enum Kind { TextLine, Image, Hr, Empty };
  Kind kind;
  uint16_t para_idx, line_idx;
  LayoutLine layout_line;
  uint16_t height, baseline = 0;
  uint16_t img_key = 0, img_w = 0, img_h = 0, img_x = 0;
};

static void scale_image(const PageOptions& opts, uint16_t content_width, uint16_t& img_w, uint16_t& img_h,
                        uint16_t& x_off) {
  const uint16_t fw = opts.width, fh = opts.height;
  if (img_w >= fw / 2) {
    if (img_w != fw) {
      img_h = static_cast<uint16_t>(static_cast<uint32_t>(img_h) * fw / img_w);
      img_w = fw;
    }
  } else {
    if (img_w > content_width) {
      img_h = static_cast<uint16_t>(static_cast<uint32_t>(img_h) * content_width / img_w);
      img_w = content_width;
    }
  }
  if (img_h > fh) {
    img_w = static_cast<uint16_t>(static_cast<uint32_t>(img_w) * fh / img_h);
    img_h = fh;
  }
  if (img_w == 0)
    img_w = 1;
  if (img_h == 0)
    img_h = 1;
  if (img_w >= fw)
    x_off = 0;
  else if (img_w > content_width)
    x_off = fw > img_w ? (fw - img_w) / 2 : 0;
  else
    x_off = opts.padding_left + (content_width > img_w ? (content_width - img_w) / 2 : 0);
}

static uint16_t compute_line_height(const IFont& font, const LayoutLine& line, uint16_t pct) {
  uint16_t h = font.y_advance();
  for (const auto& w : line.words) {
    uint16_t wh = font.y_advance(w.size);
    if (wh > h)
      h = wh;
  }
  if (pct != 100) {
    h = static_cast<uint16_t>(h * pct / 100);
    if (h < 1)
      h = 1;
  }
  return h;
}

static uint16_t line_baseline(const IFont& font, const LayoutLine& line) {
  uint16_t bl = font.baseline();
  for (const auto& w : line.words) {
    uint16_t b = font.baseline(w.size);
    if (b > bl)
      bl = b;
  }
  return bl;
}

struct InlineImageInfo {
  uint16_t key = 0, width = 0, height = 0;
  bool promoted = false, has_image = false;
};

static InlineImageInfo resolve_inline_image(const TextParagraph& text_para, uint16_t content_width,
                                            const ImageSizeQuery& sp) {
  InlineImageInfo info;
  if (!text_para.inline_image.has_value())
    return info;
  const auto& img = *text_para.inline_image;
  info.has_image = true;
  info.key = img.key;
  if (img.attr_width > 0 && img.attr_height > 0) {
    info.width = img.attr_width;
    info.height = img.attr_height;
  } else if (sp)
    sp(img.key, info.width, info.height);
  if (info.width > 0 && info.height > 0)
    info.promoted = (info.width > content_width / 3 || info.height > 120);
  return info;
}

// Resolve and scale a standalone Image paragraph into a PageItem.
// Returns an item with height==0 when dimensions could not be determined.
static PageItem make_image_para_item(uint16_t pi, const Paragraph& para, const PageOptions& opts,
                                     uint16_t content_width, const ImageSizeQuery& sp) {
  uint16_t w = para.image.attr_width, h = para.image.attr_height;
  if ((w == 0 || h == 0) && sp)
    sp(para.image.key, w, h);
  if (w == 0 || h == 0)
    return {PageItem::Image, pi, 0, {}, 0};
  uint16_t x;
  scale_image(opts, content_width, w, h, x);
  return {PageItem::Image, pi, 0, {}, h, 0, para.image.key, w, h, x};
}

// Prepare text lines for a paragraph: resolve inline image, set lo indent, run layout.
// img_info is filled in; lo.first_line_extra_indent is reset to 0 on return.
// at_line_zero controls whether a non-promoted inline image activates the indent.
static std::vector<LayoutLine> prepare_text_lines(const IFont& font, LayoutOptions& lo, const TextParagraph& text,
                                                  uint16_t content_width, const ImageSizeQuery& sp, bool at_line_zero,
                                                  InlineImageInfo& img_info) {
  img_info = resolve_inline_image(text, content_width, sp);
  if (at_line_zero && img_info.has_image && img_info.width > 0 && img_info.height > 0 && !img_info.promoted)
    lo.first_line_extra_indent = img_info.width + 4;
  auto lines = layout_paragraph(font, lo, text);
  lo.first_line_extra_indent = 0;
  return lines;
}

// ---------------------------------------------------------------------------
// assemble_page() — convert ordered PageItems into PageContent.
// Applies paragraph spacing, image-only centering, and line spreading.
// ---------------------------------------------------------------------------

static PageContent assemble_page(const PageOptions& opts, const IFont& font, IParagraphSource& source,
                                 std::vector<PageItem>& items, PagePosition start, PagePosition end,
                                 bool at_chapter_end) {
  const uint16_t content_width = opts.width - opts.padding_left - opts.padding_right;
  const uint16_t default_y_advance = font.y_advance();

  PageContent page;
  page.start = start;
  page.end = end;
  page.at_chapter_end = at_chapter_end;

  uint16_t y = 0;
  uint16_t prev_para = UINT16_MAX;
  bool is_chapter_start = (start.paragraph == 0 && start.line == 0);

  for (auto& item : items) {
    if (prev_para != UINT16_MAX && item.para_idx != prev_para) {
      y += source.paragraph(item.para_idx).spacing_before.value_or(opts.para_spacing);
    } else if (prev_para == UINT16_MAX && is_chapter_start) {
      auto sb = source.paragraph(item.para_idx).spacing_before;
      if (sb.has_value())
        y += *sb;
    }
    switch (item.kind) {
      case PageItem::TextLine:
        page.text_items.push_back(
            {item.para_idx, item.line_idx, std::move(item.layout_line), y, item.height, item.baseline});
        break;
      case PageItem::Image:
        page.image_items.push_back({item.para_idx, item.img_key, item.img_w, item.img_h, item.img_x, y});
        break;
      case PageItem::Hr:
        page.hr_items.push_back({opts.padding_left, static_cast<uint16_t>(y + default_y_advance / 2), content_width});
        break;
      case PageItem::Empty:
        break;
    }
    y += item.height;
    prev_para = item.para_idx;
  }

  page.vertical_offset = opts.padding_top;

  // Image-only page: center vertically on the full screen.
  if (page.text_items.empty() && !page.image_items.empty() && page.hr_items.empty()) {
    page.vertical_offset = static_cast<uint16_t>(opts.height > y ? (opts.height - y) / 2 : 0);
    return page;
  }

  // Text spreading: distribute slack proportionally when page is nearly full.
  if (opts.center_text && !page.text_items.empty()) {
    const uint16_t padded_height =
        opts.height > opts.padding_top + opts.padding_bottom ? opts.height - opts.padding_top - opts.padding_bottom : 0;
    if (y >= padded_height || padded_height - y >= default_y_advance)
      return page;

    size_t renderable = page.text_items.size() + page.image_items.size() + page.hr_items.size();
    if (renderable < 2)
      return page;

    struct Slot {
      enum { ST, SI, SH } type;
      size_t idx;
      uint16_t y0, h, para_idx;
    };
    std::vector<Slot> slots;
    slots.reserve(renderable);
    for (size_t i = 0; i < page.text_items.size(); ++i) {
      auto& ti = page.text_items[i];
      slots.push_back({Slot::ST, i, ti.y_offset, ti.height, ti.paragraph_index});
    }
    for (size_t i = 0; i < page.image_items.size(); ++i) {
      auto& im = page.image_items[i];
      slots.push_back({Slot::SI, i, im.y_offset, im.height, im.paragraph_index});
    }
    for (size_t i = 0; i < page.hr_items.size(); ++i) {
      auto& hr = page.hr_items[i];
      uint16_t item_y = hr.y_offset >= default_y_advance / 2 ? hr.y_offset - default_y_advance / 2 : 0;
      slots.push_back({Slot::SH, i, item_y, default_y_advance, 0});
    }
    std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return a.y0 < b.y0; });

    const size_t N = slots.size();
    int32_t slack = static_cast<int32_t>(padded_height) - static_cast<int32_t>(y);
    if (slack <= 0)
      return page;

    // Preserve the raw gaps already present between slots (para spacings, etc.)
    // as minimums. Only distribute 'slack' as extra above these.
    // This guarantees: first item stays at its natural y, last item bottom
    // lands exactly at padded_height — no separate baseline correction needed.
    std::vector<int32_t> raw_gaps(N - 1);
    for (size_t i = 0; i < N - 1; ++i) {
      int32_t rg =
          static_cast<int32_t>(slots[i + 1].y0) - static_cast<int32_t>(slots[i].y0) - static_cast<int32_t>(slots[i].h);
      raw_gaps[i] = rg > 0 ? rg : 0;
    }

    std::vector<int32_t> weights(N - 1);
    int32_t total_weight = 0;
    for (size_t i = 0; i < N - 1; ++i) {
      bool a_blk = slots[i].type != Slot::ST, b_blk = slots[i + 1].type != Slot::ST;
      weights[i] = (a_blk || b_blk) ? 8 : (slots[i].para_idx != slots[i + 1].para_idx ? 3 : 1);
      total_weight += weights[i];
    }

    int32_t base = slack / total_weight, leftover = slack % total_weight;
    std::vector<int32_t> extra_gaps(N - 1, 0);
    int32_t units = 0;
    for (size_t i = 0; i < N - 1; ++i) {
      for (int32_t w = 0; w < weights[i]; ++w) {
        extra_gaps[i] += base + (units >= total_weight - leftover ? 1 : 0);
        ++units;
      }
    }

    int32_t y_acc = static_cast<int32_t>(slots[0].y0);
    for (size_t i = 0; i < N; ++i) {
      auto& s = slots[i];
      uint16_t ny = static_cast<uint16_t>(y_acc);
      if (s.type == Slot::ST)
        page.text_items[s.idx].y_offset = ny;
      else if (s.type == Slot::SI)
        page.image_items[s.idx].y_offset = ny;
      else
        page.hr_items[s.idx].y_offset = ny + default_y_advance / 2;
      y_acc += static_cast<int32_t>(s.h);
      if (i < N - 1)
        y_acc += raw_gaps[i] + extra_gaps[i];
    }
    // By construction: y_acc == padded_height after the loop,
    // so last item bottom == padded_height and baseline == padded_height - descent.
  }
  return page;
}

// ---------------------------------------------------------------------------
// collect_forward / collect_backward
//
// Both produce an ordered std::vector<PageItem> which assemble_page() turns
// into PageContent. Shared helpers (make_image_para_item, prepare_text_lines)
// keep the per-paragraph-type logic in one place.
//
// Direction abstraction:
//   backward=false  pos=start  iterate pi ascending;  collect until page full → record end
//   backward=true   pos=end    iterate pi descending; collect until page full → reverse → record start
//
// Spacing rule: the gap between paragraph A and B (B follows A) = B.spacing_before.
//   Forward:  gap to add before pi  = spacing_before(pi)          (pi is the later one)
//   Backward: gap to add before pi  = spacing_before(items.back()) (items.back() is the later one)
// ---------------------------------------------------------------------------

struct PendingInlineImage {
  uint16_t para_idx, key, width, height;
};

struct CollectResult {
  std::vector<PageItem> items;
  std::vector<PendingInlineImage> pending;  // populated only when backward=false
  PagePosition boundary;                    // end_pos (forward) or start_pos (backward)
  bool at_chapter_end = false;
};

static CollectResult collect_page_items(const IFont& font, const PageOptions& opts, IParagraphSource& source,
                                        PagePosition pos, bool backward, const ImageSizeQuery& sp) {
  const uint16_t cw = opts.width - opts.padding_left - opts.padding_right;
  const uint16_t adv = font.y_advance();
  const uint16_t ph = opts.height - opts.padding_top - opts.padding_bottom;
  const size_t pcnt = source.paragraph_count();
  LayoutOptions lo;
  lo.width = cw;
  lo.alignment = opts.alignment;

  std::vector<PageItem> items;
  std::vector<PendingInlineImage> pending;
  uint16_t used = 0;
  bool has_toi = false, page_full = false;
  PagePosition boundary = pos;
  bool is_cs = (!backward && pos.paragraph == 0 && pos.line == 0);

  // Iteration range.
  // Backward: if pos.line>0 we need lines 0..pos.line-1 from pos.paragraph, so start there.
  //           if pos.line==0 there's nothing in pos.paragraph; start one below.
  int pi_start = backward ? ((pos.line > 0) ? (int)pos.paragraph : (int)pos.paragraph - 1) : (int)pos.paragraph;
  // line boundary for the first paragraph in the iteration
  size_t line_bnd = (backward && pos.line == 0) ? SIZE_MAX : pos.line;

  for (int pi = pi_start; !page_full && (backward ? pi >= 0 : (size_t)pi < pcnt); pi += (backward ? -1 : 1)) {
    if ((size_t)pi >= pcnt)
      break;
    const auto& para = source.paragraph((size_t)pi);

    // Spacing (height-budget only; actual y positions are computed by assemble_page).
    uint16_t spc = 0;
    if (!items.empty()) {
      uint16_t ref = backward ? items.back().para_idx : (uint16_t)pi;
      spc = source.paragraph(ref).spacing_before.value_or(opts.para_spacing);
    } else if (is_cs && para.spacing_before.has_value()) {
      spc = *para.spacing_before;
    }

    switch (para.type) {
      case ParagraphType::Text: {
        if (para.text.runs.empty()) {
          if (used + spc + adv > ph && !items.empty()) {
            if (!backward)
              boundary = {(uint16_t)pi, 0};
            page_full = true;
            break;
          }
          used += spc + adv;
          items.push_back({PageItem::Empty, (uint16_t)pi, 0, {}, adv});
          if (!backward)
            boundary = {(uint16_t)(pi + 1), 0};
          break;
        }

        bool is_bnd = ((size_t)pi == (size_t)pi_start);
        size_t skip = (!backward && is_bnd) ? line_bnd : 0;
        size_t limit = (backward && is_bnd) ? line_bnd : SIZE_MAX;

        InlineImageInfo img;
        auto lines = prepare_text_lines(font, lo, para.text, cw, sp, skip == 0, img);
        bool promoted = (img.has_image && img.promoted && img.width > 0 && img.height > 0);

        // Forward only: promoted image block goes before the text lines.
        if (!backward && skip == 0 && promoted) {
          uint16_t pw = img.width, ih = img.height, px;
          scale_image(opts, cw, pw, ih, px);
          if (used + spc + ih > ph && !items.empty()) {
            boundary = {(uint16_t)pi, 0};
            page_full = true;
            break;
          }
          used += spc + ih;
          items.push_back({PageItem::Image, (uint16_t)pi, 0, {}, ih, 0, img.key, pw, ih, px});
          has_toi = true;
          spc = 0;
        }

        // Forward only: non-promoted inline image; compute extra height above first line.
        bool has_inline = (!backward && !promoted && img.has_image && img.width > 0 && img.height > 0 && skip == 0);
        uint16_t inline_extra = 0;
        if (has_inline && !lines.empty()) {
          uint16_t bl0 = line_baseline(font, lines[0]);
          if (img.height > bl0)
            inline_extra = img.height - bl0;
        }

        size_t lcount = lines.size();
        size_t lim = (limit < lcount) ? limit : lcount;
        // Iterate: forward = skip..lcount-1 (ascending); backward = lim-1..0 (descending).
        size_t n = backward ? lim : (lcount - skip);
        bool fst = true, got_l0 = false;
        for (size_t k = 0; k < n; ++k) {
          size_t li = backward ? (lim - 1 - k) : (skip + k);
          uint16_t lh = compute_line_height(font, lines[li], para.text.line_height_pct);
          uint16_t bl = line_baseline(font, lines[li]);
          // For line 0 with an inline drop-cap image, the image top sits above the text
          // baseline by inline_extra pixels. Bake that into this item's height and baseline
          // so the item itself is taller — no phantom Empty item needed.
          if (!backward && li == 0 && inline_extra > 0) {
            lh += inline_extra;
            bl += inline_extra;
          }
          uint16_t gap = fst ? spc : 0;
          if (used + gap + lh > ph && !items.empty()) {
            if (!backward) {
              boundary = {(uint16_t)pi, (uint16_t)li};
              if (has_inline && !fst)
                pending.push_back({(uint16_t)pi, img.key, img.width, img.height});
            }
            page_full = true;
            break;
          }
          used += gap + lh;
          items.push_back({PageItem::TextLine, (uint16_t)pi, (uint16_t)li, std::move(lines[li]), lh, bl});
          has_toi = true;
          fst = false;
          if (li == 0)
            got_l0 = true;
        }

        if (!backward) {
          if (has_inline && !fst && !page_full)
            pending.push_back({(uint16_t)pi, img.key, img.width, img.height});
          if (!page_full)
            boundary = {(uint16_t)(pi + 1), 0};
        } else if (promoted && got_l0) {
          // Backward: promoted image goes after line 0 is collected (before it after reversal).
          uint16_t pw = img.width, ih = img.height, px;
          scale_image(opts, cw, pw, ih, px);
          used += ih;
          items.push_back({PageItem::Image, (uint16_t)pi, 0, {}, ih, 0, img.key, pw, ih, px});
        }
        break;
      }

      case ParagraphType::Image: {
        auto item = make_image_para_item((uint16_t)pi, para, opts, cw, sp);
        if (item.height == 0) {
          if (!backward)
            boundary = {(uint16_t)(pi + 1), 0};
          break;
        }
        if (used + spc + item.height > ph && !items.empty()) {
          if (!backward)
            boundary = {(uint16_t)pi, 0};
          page_full = true;
          break;
        }
        used += spc + item.height;
        items.push_back(std::move(item));
        has_toi = true;
        if (!backward)
          boundary = {(uint16_t)(pi + 1), 0};
        break;
      }

      case ParagraphType::Hr: {
        if (used + spc + adv > ph) {
          if (has_toi || (backward && !items.empty())) {
            if (!backward)
              boundary = {(uint16_t)pi, 0};
            page_full = true;
          } else if (!backward)
            boundary = {(uint16_t)(pi + 1), 0};
          break;
        }
        used += spc + adv;
        items.push_back({PageItem::Hr, (uint16_t)pi, 0, {}, adv});
        if (!backward)
          boundary = {(uint16_t)(pi + 1), 0};
        break;
      }

      case ParagraphType::PageBreak: {
        if (has_toi || (backward && !items.empty())) {
          if (!backward) {
            bool more = false;
            for (size_t ri = (size_t)pi + 1; ri < pcnt; ++ri)
              if (source.paragraph(ri).type != ParagraphType::PageBreak) {
                more = true;
                break;
              }
            if (more) {
              boundary = {(uint16_t)(pi + 1), 0};
              page_full = true;
              break;
            }
          } else {
            page_full = true;
            break;
          }
        }
        if (!backward)
          boundary = {(uint16_t)(pi + 1), 0};
        break;
      }
    }
  }

  if (!backward)
    return {std::move(items), std::move(pending), boundary, !page_full};

  // Backward post-processing: reverse, find start_pos, account for chapter-start spacing.
  std::reverse(items.begin(), items.end());
  if (items.empty())
    return {{}, {}, pos, pos.paragraph >= pcnt};

  PagePosition start_pos = {items.front().para_idx, items.front().line_idx};
  bool at_end = (start_pos == PagePosition{0, 0} && pos.paragraph >= pcnt);

  if (start_pos.paragraph == 0 && start_pos.line == 0) {
    auto sb = source.paragraph(0).spacing_before;
    if (sb.has_value()) {
      if (used + *sb <= ph) {
        used += *sb;
      } else {
        while (!items.empty() && used + *sb > ph) {
          used -= items.front().height;
          items.erase(items.begin());
        }
        if (!items.empty())
          start_pos = {items.front().para_idx, items.front().line_idx};
      }
    }
  }
  return {std::move(items), {}, start_pos, at_end};
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

PageContent layout_page(const IFont& font, const PageOptions& opts, IParagraphSource& source, PagePosition start,
                        const ImageSizeQuery& size_provider) {
  auto c = collect_page_items(font, opts, source, start, false, size_provider);
  auto page = assemble_page(opts, font, source, c.items, start, c.boundary, c.at_chapter_end);
  for (auto& pii : c.pending) {
    for (auto& ti : page.text_items) {
      if (ti.paragraph_index == pii.para_idx) {
        uint16_t baseline_y = ti.y_offset + ti.baseline;
        uint16_t img_y = (baseline_y >= pii.height) ? (baseline_y - pii.height) : 0;
        page.image_items.push_back({pii.para_idx, pii.key, pii.width, pii.height, opts.padding_left, img_y});
        break;
      }
    }
  }
  return page;
}

PageContent layout_page_backward(const IFont& font, const PageOptions& opts, IParagraphSource& source, PagePosition end,
                                 const ImageSizeQuery& size_provider) {
  if (end.paragraph == 0 && end.line == 0) {
    PageContent page;
    page.start = {0, 0};
    page.end = {0, 0};
    return page;
  }
  auto c = collect_page_items(font, opts, source, end, true, size_provider);
  if (c.items.empty()) {
    PageContent page;
    page.start = page.end = end;
    page.at_chapter_end = (end.paragraph >= source.paragraph_count());
    return page;
  }
  return assemble_page(opts, font, source, c.items, c.boundary, end, c.at_chapter_end);
}

}  // namespace microreader
