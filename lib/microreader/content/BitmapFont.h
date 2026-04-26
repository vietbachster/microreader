#pragma once

// BitmapFont — proportional bitmap font loaded from MBF binary data.
//
// Implements IFont for the TextLayout engine. Reads glyph metrics and
// bitmap data directly from a `const uint8_t*` pointer (works for both
// firmware-embedded constexpr data and memory-mapped flash partitions).
// Zero heap allocations.
//
// Supports multi-style MBF files: Regular + optional Bold, Italic, BoldItalic.
// Style-specific glyph_data() and char_width() select the right style table.
// Missing styles fall back to Regular.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "BitmapFontFormat.h"
#include "Font.h"

namespace microreader {

// Result of a glyph lookup — points into the MBF data buffer.
struct GlyphData {
  const uint8_t* bits;           // pointer to 1-bit BW packed bitmap (nullptr if no bitmap)
  const uint8_t* gray_lsb_bits;  // pointer to grayscale LSB plane (nullptr if absent)
  const uint8_t* gray_msb_bits;  // pointer to grayscale MSB plane (nullptr if absent)
  uint8_t bitmap_width;          // bitmap width in pixels
  uint8_t bitmap_height;         // bitmap height in pixels
  uint8_t advance_width;         // horizontal cursor advance
  int8_t x_offset;               // offset from cursor to bitmap left edge
  int8_t y_offset;               // offset from baseline to bitmap top (negative = above)
};

class BitmapFont : public IFont {
 public:
  BitmapFont() = default;

  BitmapFont(const uint8_t* data, size_t size) {
    init(data, size);
  }

  void init(const uint8_t* data, size_t size) {
    data_ = nullptr;
    size_ = 0;
    header_ = nullptr;
    bitmaps_ = nullptr;
    gray_lsb_bitmaps_ = nullptr;
    gray_msb_bitmaps_ = nullptr;
    for (auto& s : styles_)
      s = {};

    if (!data || size < sizeof(MbfHeader))
      return;

    auto* hdr = reinterpret_cast<const MbfHeader*>(data);
    if (hdr->magic != kMbfMagic || hdr->version != kMbfVersion)
      return;

    // Validate Regular style data
    const size_t ranges_start = sizeof(MbfHeader);
    const size_t ranges_end = ranges_start + static_cast<size_t>(hdr->num_ranges) * sizeof(MbfRange);
    const size_t glyphs_end = ranges_end + static_cast<size_t>(hdr->num_glyphs) * sizeof(MbfGlyph);
    if (glyphs_end > size || hdr->bitmap_data_offset > size)
      return;

    data_ = data;
    size_ = size;
    header_ = hdr;
    bitmaps_ = data + hdr->bitmap_data_offset;

    // Grayscale planes (optional)
    if (hdr->gray_lsb_offset != 0 && hdr->gray_lsb_offset < size)
      gray_lsb_bitmaps_ = data + hdr->gray_lsb_offset;
    if (hdr->gray_msb_offset != 0 && hdr->gray_msb_offset < size)
      gray_msb_bitmaps_ = data + hdr->gray_msb_offset;

    // Regular style (always present)
    styles_[0].ranges = reinterpret_cast<const MbfRange*>(data + ranges_start);
    styles_[0].glyphs = reinterpret_cast<const MbfGlyph*>(data + ranges_end);
    styles_[0].num_ranges = hdr->num_ranges;
    styles_[0].num_glyphs = hdr->num_glyphs;
    styles_[0].fallback_glyph_idx = find_glyph_index_in(styles_[0], 0xFFFD);

    // Load additional styles if present
    load_style_(data, size, hdr->bold_offset, styles_[1]);
    load_style_(data, size, hdr->italic_offset, styles_[2]);
    load_style_(data, size, hdr->bold_italic_offset, styles_[3]);
  }

  bool valid() const {
    return header_ != nullptr;
  }

  // ── IFont interface ─────────────────────────────────────────────────────

  uint16_t char_width(char32_t ch, FontStyle style, FontSize /*size*/ = FontSize::Normal) const override {
    const StyleData& sd = resolve_style_(style);
    int idx = find_glyph_index_in(sd, ch);
    if (idx < 0)
      idx = sd.fallback_glyph_idx;
    if (idx < 0)
      return header_ ? header_->default_advance : 0;
    return sd.glyphs[idx].advance_width;
  }

  uint16_t word_width(const char* text, size_t len, FontStyle style, FontSize size = FontSize::Normal) const override {
    uint16_t w = 0;
    const char* p = text;
    const char* end = text + len;
    while (p < end) {
      char32_t cp = decode_utf8(p, end);
      w += char_width(cp, style, size);
    }
    return w;
  }

  uint16_t y_advance(FontSize /*size*/ = FontSize::Normal) const override {
    return header_ ? header_->y_advance : 0;
  }

  uint16_t baseline(FontSize /*size*/ = FontSize::Normal) const override {
    return header_ ? header_->baseline : 0;
  }

  // ── Glyph data access (for rendering) ───────────────────────────────────

  GlyphData glyph_data(char32_t ch, FontStyle style = FontStyle::Regular) const {
    const StyleData& sd = resolve_style_(style);
    int idx = find_glyph_index_in(sd, ch);
    if (idx < 0)
      idx = sd.fallback_glyph_idx;
    if (idx < 0)
      return {nullptr, nullptr, nullptr, 0, 0, header_ ? header_->default_advance : static_cast<uint8_t>(0), 0, 0};

    const MbfGlyph& g = sd.glyphs[idx];
    const uint8_t* bits = nullptr;
    const uint8_t* lsb = nullptr;
    const uint8_t* msb = nullptr;
    if (g.bitmap_width > 0 && g.bitmap_height > 0) {
      bits = bitmaps_ + g.bitmap_offset;
      if (gray_lsb_bitmaps_)
        lsb = gray_lsb_bitmaps_ + g.bitmap_offset;
      if (gray_msb_bitmaps_)
        msb = gray_msb_bitmaps_ + g.bitmap_offset;
    }
    return {bits, lsb, msb, g.bitmap_width, g.bitmap_height, g.advance_width, g.x_offset, g.y_offset};
  }

  bool has_style(FontStyle style) const {
    return styles_[static_cast<uint8_t>(style)].ranges != nullptr;
  }

  bool has_grayscale() const {
    return gray_lsb_bitmaps_ != nullptr;
  }

  uint16_t glyph_height() const {
    return header_ ? header_->glyph_height : 0;
  }

  uint16_t num_glyphs() const {
    return header_ ? header_->num_glyphs : 0;
  }

 private:
  struct StyleData {
    const MbfRange* ranges = nullptr;
    const MbfGlyph* glyphs = nullptr;
    uint16_t num_ranges = 0;
    uint16_t num_glyphs = 0;
    int fallback_glyph_idx = -1;
  };

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  const MbfHeader* header_ = nullptr;
  const uint8_t* bitmaps_ = nullptr;
  const uint8_t* gray_lsb_bitmaps_ = nullptr;
  const uint8_t* gray_msb_bitmaps_ = nullptr;
  StyleData styles_[4] = {};  // indexed by FontStyle enum value

  // Resolve a FontStyle to its StyleData, falling back to Regular.
  const StyleData& resolve_style_(FontStyle style) const {
    uint8_t idx = static_cast<uint8_t>(style);
    if (idx < 4 && styles_[idx].ranges)
      return styles_[idx];
    return styles_[0];  // fallback to Regular
  }

  // Load a style section from the given file offset.
  static void load_style_(const uint8_t* data, size_t size, uint32_t offset, StyleData& out) {
    if (offset == 0 || offset + sizeof(MbfStyleSection) > size)
      return;
    auto* sec = reinterpret_cast<const MbfStyleSection*>(data + offset);
    size_t ranges_start = offset + sizeof(MbfStyleSection);
    size_t ranges_end = ranges_start + static_cast<size_t>(sec->num_ranges) * sizeof(MbfRange);
    size_t glyphs_end = ranges_end + static_cast<size_t>(sec->num_glyphs) * sizeof(MbfGlyph);
    if (glyphs_end > size)
      return;
    out.ranges = reinterpret_cast<const MbfRange*>(data + ranges_start);
    out.glyphs = reinterpret_cast<const MbfGlyph*>(data + ranges_end);
    out.num_ranges = sec->num_ranges;
    out.num_glyphs = sec->num_glyphs;
    out.fallback_glyph_idx = find_glyph_index_in(out, 0xFFFD);
  }

  // Find glyph table index for a codepoint within a style. Returns -1 if not found.
  static int find_glyph_index_in(const StyleData& sd, char32_t cp) {
    if (!sd.ranges || sd.num_ranges == 0)
      return -1;
    int lo = 0;
    int hi = static_cast<int>(sd.num_ranges) - 1;
    while (lo <= hi) {
      int mid = lo + (hi - lo) / 2;
      const MbfRange& r = sd.ranges[mid];
      uint32_t range_end = r.first_codepoint + r.count;
      if (cp < r.first_codepoint) {
        hi = mid - 1;
      } else if (cp >= range_end) {
        lo = mid + 1;
      } else {
        uint16_t glyph_idx = r.glyph_table_start + static_cast<uint16_t>(cp - r.first_codepoint);
        if (glyph_idx < sd.num_glyphs)
          return glyph_idx;
        return -1;
      }
    }
    return -1;
  }

  // Decode one UTF-8 codepoint, advance p.
  static char32_t decode_utf8(const char*& p, const char* end) {
    if (p >= end)
      return 0;
    uint8_t b = static_cast<uint8_t>(*p);
    if (b < 0x80) {
      ++p;
      return b;
    }
    if (b < 0xE0 && p + 1 < end) {
      char32_t cp = (static_cast<char32_t>(b & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
      p += 2;
      return cp;
    }
    if (b < 0xF0 && p + 2 < end) {
      char32_t cp = (static_cast<char32_t>(b & 0x0F) << 12) |
                    (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
                    (static_cast<uint8_t>(p[2]) & 0x3F);
      p += 3;
      return cp;
    }
    if (b < 0xF8 && p + 3 < end) {
      char32_t cp =
          (static_cast<char32_t>(b & 0x07) << 18) | (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
          (static_cast<char32_t>(static_cast<uint8_t>(p[2]) & 0x3F) << 6) | (static_cast<uint8_t>(p[3]) & 0x3F);
      p += 4;
      return cp;
    }
    ++p;
    return 0xFFFD;
  }
};

// ---------------------------------------------------------------------------
// BitmapFontSet — wraps up to kFontSizeCount BitmapFont pointers
// (Small/Normal/Large/XLarge/XXLarge) and dispatches IFont methods by FontSize.
// Missing sizes fall back to Normal.
// Also provides glyph_data() for rendering at a specific size.
// ---------------------------------------------------------------------------

class BitmapFontSet : public IFont {
 public:
  BitmapFontSet() = default;

  void set(FontSize size, const BitmapFont* font) {
    fonts_[static_cast<uint8_t>(size)] = font;
  }

  const BitmapFont* get(FontSize size) const {
    return resolve_(size);
  }

  bool valid() const {
    return fonts_[static_cast<uint8_t>(FontSize::Normal)] != nullptr &&
           fonts_[static_cast<uint8_t>(FontSize::Normal)]->valid();
  }

  bool has_grayscale() const {
    auto* f = fonts_[static_cast<uint8_t>(FontSize::Normal)];
    return f && f->has_grayscale();
  }

  // ── IFont interface ─────────────────────────────────────────────────────

  uint16_t char_width(char32_t ch, FontStyle style, FontSize size = FontSize::Normal) const override {
    return resolve_(size)->char_width(ch, style, size);
  }

  uint16_t word_width(const char* text, size_t len, FontStyle style, FontSize size = FontSize::Normal) const override {
    return resolve_(size)->word_width(text, len, style, size);
  }

  uint16_t y_advance(FontSize size = FontSize::Normal) const override {
    return resolve_(size)->y_advance(size);
  }

  uint16_t baseline(FontSize size = FontSize::Normal) const override {
    return resolve_(size)->baseline(size);
  }

  // ── Glyph data (for rendering) ─────────────────────────────────────────

  GlyphData glyph_data(char32_t ch, FontStyle style, FontSize size = FontSize::Normal) const {
    return resolve_(size)->glyph_data(ch, style);
  }

 private:
  // Indexed by FontSize enum: [0]=Small, [1]=Normal, [2]=Large, [3]=XLarge, [4]=XXLarge
  const BitmapFont* fonts_[kFontSizeCount] = {};

  const BitmapFont* resolve_(FontSize size) const {
    uint8_t idx = static_cast<uint8_t>(size);
    if (idx < kFontSizeCount) {
      auto* f = fonts_[idx];
      if (f && f->valid())
        return f;
    }
    return fonts_[static_cast<uint8_t>(FontSize::Normal)];  // fallback to Normal
  }
};

}  // namespace microreader
