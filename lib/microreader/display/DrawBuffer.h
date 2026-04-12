#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../content/BitmapFont.h"
#include "ui_font_small.h"

namespace microreader {

enum class Rotation { Deg0 = 0, Deg90 = 90 };

// Refresh mode for full-screen updates.
enum class RefreshMode { Full, Half };

// Physical screen constants and bit-packed pixel helpers (used internally by DrawBuffer).
struct DisplayFrame {
  // The full size is 800x480 but we only use 788x480 to avoid the hidden areas.
  static constexpr int kPhysicalWidth = 788;
  static constexpr int kPhysicalHeight = 480;
  static constexpr int kStride = (kPhysicalWidth + 7) / 8;
  static constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kStride) * kPhysicalHeight;
};

// Display driver interface implemented by each platform.
class IDisplay {
 public:
  virtual ~IDisplay() = default;

  // Full physical refresh. Both BW and RED RAM are set to `pixels`.
  virtual void full_refresh(const uint8_t* pixels, RefreshMode mode) = 0;

  // Partial refresh: new_pixels → BW RAM. Driver tracks previous frame for RED RAM.
  virtual void partial_refresh(const uint8_t* new_pixels) = 0;

  // Put the display controller into deep sleep (low-power mode).
  virtual void deep_sleep() {}

  // Notify the display of the logical rotation used by the caller.
  // Used by the desktop emulator to resize/orient the SDL window.
  virtual void set_rotation(Rotation r) {
    (void)r;
  }
};

// Double-buffered display with simple draw helpers.
//
// Uses Deg90 (portrait) rotation: logical 480×788, physical 788×480.
// The "inactive" buffer is drawn to; "active" is what's currently displayed.
// refresh() swaps and does a partial hardware refresh.
//
// Scratch buffer loan: scratch_buf1()/scratch_buf2() expose both ~48KB buffers
// for callers (e.g. EPUB→MRB conversion). Call reset_after_scratch() when done.
class DrawBuffer {
 public:
  // Logical portrait dimensions.
  static constexpr int kWidth = DisplayFrame::kPhysicalHeight;
  static constexpr int kHeight = DisplayFrame::kPhysicalWidth;
  static constexpr size_t kBufSize = DisplayFrame::kPixelBytes;

  explicit DrawBuffer(IDisplay& display) : display_(display) {
    memset(bufs_[0], 0xFF, kBufSize);
    memset(bufs_[1], 0xFF, kBufSize);
    display_.set_rotation(Rotation::Deg90);
  }

  // ── Draw helpers (logical portrait coordinates) ─────────────────────────

  // Fill the entire inactive buffer.
  void fill(bool white = true) {
    memset(inactive_(), white ? 0xFF : 0x00, kBufSize);
  }

  // Fill a logical rectangle.
  void fill_rect(int lx, int ly, int lw, int lh, bool white) {
    // Deg90: logical (lx,ly,lw,lh) → physical (px=ly, py=PhysH-lx-lw, pw=lh, ph=lw)
    fill_rect_physical_(inactive_(), ly, DisplayFrame::kPhysicalHeight - lx - lw, lh, lw, white);
  }

  // Fill a logical horizontal span [x1, x2) on logical row ly.
  // In physical space this is a vertical column segment.
  void fill_row(int ly, int x1, int x2, bool white) {
    x1 = std::max(x1, 0);
    x2 = std::min(x2, kWidth);
    if (x1 >= x2 || ly < 0 || ly >= kHeight)
      return;
    // Deg90: logical row ly → physical col ly; logical cols [x1,x2) → physical rows [PhysH-x2, PhysH-x1)
    fill_col_physical_(inactive_(), ly, DisplayFrame::kPhysicalHeight - x2, DisplayFrame::kPhysicalHeight - x1, white);
  }

  // Set a single logical pixel.
  void set_pixel(int lx, int ly, bool white) {
    if (lx < 0 || lx >= kWidth || ly < 0 || ly >= kHeight)
      return;
    const int px = ly;
    const int py = DisplayFrame::kPhysicalHeight - 1 - lx;
    uint8_t* buf = inactive_();
    const size_t bidx = static_cast<size_t>(py * DisplayFrame::kStride + px / 8);
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
    if (white)
      buf[bidx] |= bit;
    else
      buf[bidx] &= static_cast<uint8_t>(~bit);
  }

  // Blit a horizontal row of 1-bit packed pixels at logical position (lx, ly).
  // data_1bit is MSB-first packed, width pixels long.
  // Due to Deg90 rotation, this maps to a vertical column in physical space.
  void blit_1bit_row(int lx, int ly, const uint8_t* data_1bit, int width) {
    if (ly < 0 || ly >= kHeight || width <= 0)
      return;
    uint8_t* buf = inactive_();
    const int px = ly;
    const int byte_col = px / 8;
    const uint8_t set_mask = static_cast<uint8_t>(0x80u >> (px & 7));
    const uint8_t clr_mask = static_cast<uint8_t>(~set_mask);

    // Clip x range to [0, kWidth)
    int col_start = 0, col_end = width;
    if (lx < 0)
      col_start = -lx;
    if (lx + width > kWidth)
      col_end = kWidth - lx;

    for (int col = col_start; col < col_end; ++col) {
      const int py = DisplayFrame::kPhysicalHeight - 1 - (lx + col);
      const size_t bidx = static_cast<size_t>(py) * DisplayFrame::kStride + byte_col;
      const bool white = (data_1bit[col >> 3] >> (7 - (col & 7))) & 1;
      if (white)
        buf[bidx] |= set_mask;
      else
        buf[bidx] &= clr_mask;
    }
  }

  // Draw text with the UI font, including background fill.
  // white=true  → white background, black glyphs (normal unselected item).
  // white=false → black background, white glyphs (highlighted selected item).
  // The scale parameter is accepted for API compatibility but ignored.
  void draw_text(int x, int y, const char* text, bool white, int /*scale*/ = 1) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    const int w = static_cast<int>(f.word_width(text, strlen(text), FontStyle::Regular));
    const int h = static_cast<int>(f.glyph_height());
    fill_rect(x, y, w, h, white);
    draw_text_proportional(x, y + static_cast<int>(f.baseline()), text, f, !white);
  }

  // Draw text centered at (cx, y) using the UI font with background fill.
  void draw_text_centered(int cx, int y, const char* text, bool white) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    const int w = static_cast<int>(f.word_width(text, strlen(text), FontStyle::Regular));
    draw_text(cx - w / 2, y, text, white);
  }

  // Draw text glyphs only (no background fill). Glyph color = white param.
  // The scale parameter is accepted for API compatibility but ignored.
  void draw_text_no_bg(int x, int y, const char* text, bool white, int /*scale*/ = 1) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    draw_text_proportional(x, y + static_cast<int>(f.baseline()), text, f, white);
  }

  // Draw a single proportional glyph bitmap at logical (x + x_offset, y + y_offset).
  // bits: 1-bit packed MSB-first bitmap (MBF polarity: 1=white, 0=black/ink).
  // Only ink pixels (bit=0) are drawn when invert=false.
  // Only white pixels (bit=1) are drawn when invert=true.
  void draw_glyph(int x, int y, const uint8_t* bits, int bitmap_width, int bitmap_height, int x_offset, int y_offset,
                  bool white) {
    if (!bits || bitmap_width <= 0 || bitmap_height <= 0)
      return;
    const int gx = x + x_offset;
    const int gy = y + y_offset;
    const int row_stride = (bitmap_width + 7) / 8;

    for (int row = 0; row < bitmap_height; ++row) {
      const int ly = gy + row;
      if (ly < 0 || ly >= kHeight)
        continue;
      const uint8_t* row_data = bits + row * row_stride;
      // Draw only ink pixels (bit=0 in MBF = black).
      // We need to set them to the requested color (usually black = !white).
      // Iterate through the row and draw individual ink pixels.
      for (int col = 0; col < bitmap_width; ++col) {
        const int lx = gx + col;
        if (lx < 0 || lx >= kWidth)
          continue;
        const bool bit_set = (row_data[col >> 3] >> (7 - (col & 7))) & 1;
        if (!bit_set) {
          // Ink pixel — draw it in the requested color
          set_pixel(lx, ly, white);
        }
      }
    }
  }

  // Draw proportional text using a BitmapFont. Cursor starts at (x, baseline_y)
  // where baseline_y is the Y position of the text baseline.
  // Returns the X position after the last character (cursor advance).
  int draw_text_proportional(int x, int baseline_y, const char* text, size_t len, const BitmapFont& font, bool white,
                             FontStyle style = FontStyle::Regular);

  // Convenience overload for null-terminated strings.
  int draw_text_proportional(int x, int baseline_y, const char* text, const BitmapFont& font, bool white,
                             FontStyle style = FontStyle::Regular) {
    return draw_text_proportional(x, baseline_y, text, text ? strlen(text) : 0, font, white, style);
  }

  // Overload taking a BitmapFontSet + FontSize — resolves to the appropriate BitmapFont.
  int draw_text_proportional(int x, int baseline_y, const char* text, size_t len, const BitmapFontSet& fonts,
                             bool white, FontStyle style = FontStyle::Regular, FontSize size = FontSize::Normal) {
    const BitmapFont* f = fonts.get(size);
    return f ? draw_text_proportional(x, baseline_y, text, len, *f, white, style) : x;
  }

  // Convenience overload for null-terminated strings with BitmapFontSet.
  int draw_text_proportional(int x, int baseline_y, const char* text, const BitmapFontSet& fonts, bool white,
                             FontStyle style = FontStyle::Regular, FontSize size = FontSize::Normal) {
    return draw_text_proportional(x, baseline_y, text, text ? strlen(text) : 0, fonts, white, style, size);
  }

  // Draw a filled circle.
  void draw_circle(int cx, int cy, int r, bool white) {
    if (r <= 0)
      return;
    const int r2 = r * r;
    int dx = r;
    for (int dy = 0; dy <= r; ++dy) {
      while (dx * dx + dy * dy > r2)
        --dx;
      if (dx < 0)
        break;
      fill_row(cy + dy, cx - dx, cx + dx + 1, white);
      if (dy != 0)
        fill_row(cy - dy, cx - dx, cx + dx + 1, white);
    }
  }

  // ── Display operations ──────────────────────────────────────────────────

  // Swap active↔inactive, then do a partial hardware refresh.
  void refresh() {
    display_.partial_refresh(inactive_());
    active_idx_ = 1 - active_idx_;
  }

  // Call full hardware refresh using the current inactive buffer, then sync both.
  void full_refresh(RefreshMode mode = RefreshMode::Half) {
    display_.full_refresh(inactive_(), mode);
    memcpy(bufs_[active_idx_], bufs_[1 - active_idx_], kBufSize);
    active_idx_ = 1 - active_idx_;
  }

  void deep_sleep() {
    display_.deep_sleep();
  }

  // ── Scratch buffer loan (for EPUB conversion / image decode) ────────────

  // Loan the inactive buffer as scratch (will be overwritten before next refresh).
  uint8_t* scratch_buf1() {
    return bufs_[1 - active_idx_];
  }
  // Loan the active buffer as scratch (for operations needing two buffers).
  uint8_t* scratch_buf2() {
    return bufs_[active_idx_];
  }

  // Reset both buffers after scratch use, before drawing new content.
  void reset_after_scratch(bool white = true) {
    memset(bufs_[0], white ? 0xFF : 0x00, kBufSize);
    memset(bufs_[1], white ? 0xFF : 0x00, kBufSize);
    active_idx_ = 0;
  }

 private:
  IDisplay& display_;
  alignas(4) uint8_t bufs_[2][kBufSize];
  int active_idx_ = 0;

  uint8_t* inactive_() {
    return bufs_[1 - active_idx_];
  }
  const uint8_t* active_() const {
    return bufs_[active_idx_];
  }

  // Returns the shared UI font backed by ui_font_small.h data.
  static const BitmapFont& ui_font_() {
    static BitmapFont font(kFontData_ui_small_mbf, sizeof(kFontData_ui_small_mbf));
    return font;
  }

  // Fill a physical horizontal span [x1, x2) on physical row `row`.
  static void fill_row_physical_(uint8_t* buf, int row, int x1, int x2, bool white) {
    x1 = std::max(x1, 0);
    x2 = std::min(x2, DisplayFrame::kPhysicalWidth);
    if (x1 >= x2 || row < 0 || row >= DisplayFrame::kPhysicalHeight)
      return;
    const int bx1 = x1 / 8;
    const int bx2 = (x2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (x1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((x2 - 1) & 7)));
    uint8_t* rp = buf + row * DisplayFrame::kStride;
    if (bx1 == bx2) {
      const auto m = static_cast<uint8_t>(lmask & rmask);
      if (white)
        rp[bx1] |= m;
      else
        rp[bx1] &= static_cast<uint8_t>(~m);
    } else {
      if (white)
        rp[bx1] |= lmask;
      else
        rp[bx1] &= static_cast<uint8_t>(~lmask);
      if (bx2 > bx1 + 1)
        memset(rp + bx1 + 1, white ? 0xFF : 0x00, bx2 - bx1 - 1);
      if (white)
        rp[bx2] |= rmask;
      else
        rp[bx2] &= static_cast<uint8_t>(~rmask);
    }
  }

  // Fill a physical rectangle (rx, ry, rw, rh).
  static void fill_rect_physical_(uint8_t* buf, int rx, int ry, int rw, int rh, bool white) {
    const int x1 = std::max(rx, 0);
    const int y1 = std::max(ry, 0);
    const int x2 = std::min(rx + rw, DisplayFrame::kPhysicalWidth);
    const int y2 = std::min(ry + rh, DisplayFrame::kPhysicalHeight);
    if (x1 >= x2 || y1 >= y2)
      return;
    for (int row = y1; row < y2; ++row)
      fill_row_physical_(buf, row, x1, x2, white);
  }

  // Fill physical column `pcol` for rows [py1, py2).
  static void fill_col_physical_(uint8_t* buf, int pcol, int py1, int py2, bool white) {
    py1 = std::max(py1, 0);
    py2 = std::min(py2, DisplayFrame::kPhysicalHeight);
    if (pcol < 0 || pcol >= DisplayFrame::kPhysicalWidth || py1 >= py2)
      return;
    const int bidx = pcol / 8;
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (pcol & 7));
    for (int row = py1; row < py2; ++row) {
      uint8_t* p = buf + row * DisplayFrame::kStride + bidx;
      if (white)
        *p |= bit;
      else
        *p &= static_cast<uint8_t>(~bit);
    }
  }
};

}  // namespace microreader

namespace microreader {

inline int DrawBuffer::draw_text_proportional(int x, int baseline_y, const char* text, size_t len,
                                              const BitmapFont& font, bool white, FontStyle style) {
  if (!text || len == 0 || !font.valid())
    return x;
  const char* p = text;
  const char* end = text + len;
  int cursor_x = x;
  while (p < end) {
    // Decode UTF-8
    char32_t cp = 0;
    uint8_t b = static_cast<uint8_t>(*p);
    if (b < 0x80) {
      cp = b;
      ++p;
    } else if (b < 0xE0 && p + 1 < end) {
      cp = (static_cast<char32_t>(b & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
      p += 2;
    } else if (b < 0xF0 && p + 2 < end) {
      cp = (static_cast<char32_t>(b & 0x0F) << 12) | (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
           (static_cast<uint8_t>(p[2]) & 0x3F);
      p += 3;
    } else if (b < 0xF8 && p + 3 < end) {
      cp = (static_cast<char32_t>(b & 0x07) << 18) | (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
           (static_cast<char32_t>(static_cast<uint8_t>(p[2]) & 0x3F) << 6) | (static_cast<uint8_t>(p[3]) & 0x3F);
      p += 4;
    } else {
      ++p;
      cp = 0xFFFD;
    }

    GlyphData g = font.glyph_data(cp, style);
    if (g.bits) {
      draw_glyph(cursor_x, baseline_y, g.bits, g.bitmap_width, g.bitmap_height, g.x_offset, g.y_offset, white);
    }
    cursor_x += g.advance_width;
  }
  return cursor_x;
}

}  // namespace microreader
