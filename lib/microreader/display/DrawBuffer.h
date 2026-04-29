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

// Which bitmap plane to read from a GlyphData during grayscale rendering.
enum class GrayPlane { BW, LSB, MSB };

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
  virtual void full_refresh(const uint8_t* pixels, RefreshMode mode, bool turnOffScreen = false) = 0;

  // Partial refresh: new_pixels → BW RAM. Driver tracks previous frame for RED RAM.
  // prev_pixels is the previous BW frame (used to restore BW RAM after grayscale revert).
  virtual void partial_refresh(const uint8_t* new_pixels, const uint8_t* prev_pixels) = 0;

  // Write data to BW RAM only (no refresh). Used for grayscale LSB plane.
  virtual void write_ram_bw(const uint8_t* data) {
    (void)data;
  }

  // Write data to RED RAM only (no refresh). Used for grayscale MSB plane.
  virtual void write_ram_red(const uint8_t* data) {
    (void)data;
  }

  // Trigger a grayscale display refresh using a custom LUT.
  // Assumes BW RAM and RED RAM have already been written via write_ram_bw/write_ram_red.
  virtual void grayscale_refresh(bool turnOffScreen = false) {}

  // Revert grayscale overlay and restore prev_pixels into RED RAM.
  // Must be called while the buffer holding the pre-grayscale BW frame is still valid.
  virtual void revert_grayscale(const uint8_t* prev_pixels) {
    (void)prev_pixels;
  }

  // Partially refresh a physical sub-rectangle of the display.
  // new_buf/old_buf: 1-bit packed, row-major, stride_bytes per row.
  // (phys_x, phys_y): physical top-left; (phys_w, phys_h): pixel dimensions.
  // phys_x must be byte-aligned (multiple of 8).
  // Default: no-op — platforms may override for efficient region updates.
  virtual void partial_refresh_region(int phys_x, int phys_y, int phys_w, int phys_h, const uint8_t* new_buf,
                                      int stride_bytes) {
    (void)phys_x;
    (void)phys_y;
    (void)phys_w;
    (void)phys_h;
    (void)new_buf;
    (void)stride_bytes;
  }

  // Put the display controller into deep sleep (low-power mode).
  virtual void deep_sleep() {}

  // Notify the display of the logical rotation used by the caller.
  // Used by the desktop emulator to resize/orient the SDL window.
  virtual void set_rotation(Rotation r) {
    (void)r;
  }

  // Query if display is currently in grayscale mode.
  virtual bool in_grayscale_mode() const {
    return false;
  }

  // Returns true if the display hardware is currently busy refreshing.
  // Default: always ready. Platforms may override for non-blocking region updates.
  virtual bool is_busy() const {
    return false;
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

  IDisplay& display() {
    return display_;
  }
  const IDisplay& display() const {
    return display_;
  }

  // ── Draw helpers (logical portrait coordinates) ─────────────────────────

  // Fill the entire inactive buffer.
  void fill(bool white = true) {
    memset(inactive_(), white ? 0xFF : 0x00, kBufSize);
  }

  // Fill a logical rectangle.
  void fill_rect(int lx, int ly, int lw, int lh, bool white) {
    // Deg90: logical (lx,ly,lw,lh) → physical (px=ly, py=PhysH-lx-lw, pw=lh, ph=lw)
    fill_rect_physical_(full_target_(), ly, DisplayFrame::kPhysicalHeight - lx - lw, lh, lw, white);
  }

  // Fill a logical horizontal span [x1, x2) on logical row ly.
  // In physical space this is a vertical column segment.
  void fill_row(int ly, int x1, int x2, bool white) {
    x1 = std::max(x1, 0);
    x2 = std::min(x2, kWidth);
    if (x1 >= x2 || ly < 0 || ly >= kHeight)
      return;
    // Deg90: logical row ly → physical col ly; logical cols [x1,x2) → physical rows [PhysH-x2, PhysH-x1)
    fill_col_physical_(full_target_(), ly, DisplayFrame::kPhysicalHeight - x2, DisplayFrame::kPhysicalHeight - x1,
                       white);
  }

  void draw_image(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!imageData || w == 0 || h == 0)
      return;

    if (x >= DisplayFrame::kPhysicalWidth || y >= DisplayFrame::kPhysicalHeight)
      return;

    const uint16_t imageWidthBytes = static_cast<uint16_t>((w + 7) / 8);
    const uint16_t max_width = static_cast<uint16_t>(DisplayFrame::kPhysicalWidth - x);
    const uint16_t draw_width = std::min<uint16_t>(w, max_width);
    const uint16_t draw_bytes = static_cast<uint16_t>((draw_width + 7) / 8);
    uint8_t* buf = inactive_();

    const uint16_t dest_offset_x = static_cast<uint16_t>(x / 8);
    const uint8_t bit_offset = static_cast<uint8_t>(x & 7);

    auto set_pixel_physical = [&](uint16_t px, uint16_t py, bool white) {
      if (px >= DisplayFrame::kPhysicalWidth || py >= DisplayFrame::kPhysicalHeight)
        return;
      size_t idx = static_cast<size_t>(py) * DisplayFrame::kStride + (px / 8);
      uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
      if (white)
        buf[idx] |= bit;
      else
        buf[idx] &= static_cast<uint8_t>(~bit);
    };

    for (uint16_t row = 0; row < h; ++row) {
      uint16_t destY = y + row;
      if (destY >= DisplayFrame::kPhysicalHeight)
        break;

      const size_t destRowStart = static_cast<size_t>(destY) * DisplayFrame::kStride + dest_offset_x;
      const size_t srcRowStart = static_cast<size_t>(row) * imageWidthBytes;

      if (bit_offset == 0 && (w & 7) == 0) {
        const uint16_t copy_bytes = std::min<uint16_t>(imageWidthBytes, draw_bytes);
        memcpy(buf + destRowStart, imageData + srcRowStart, copy_bytes);
      } else {
        for (uint16_t col = 0; col < draw_width; ++col) {
          const size_t src_byte = srcRowStart + (col / 8);
          const uint8_t src_bit = static_cast<uint8_t>((imageData[src_byte] >> (7 - (col & 7))) & 1);
          set_pixel_physical(static_cast<uint16_t>(x + col), destY, src_bit != 0);
        }
      }
    }
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
    draw_glyph_impl_(full_target_(), x, y, bits, bitmap_width, bitmap_height, x_offset, y_offset, white);
  }

  // Draw a glyph from a specific grayscale plane into an explicit buffer.
  void draw_glyph_plane(uint8_t* buf, int x, int y, const GlyphData& g, GrayPlane plane, bool white) {
    const uint8_t* bits = nullptr;
    switch (plane) {
      case GrayPlane::BW:
        bits = g.bits;
        break;
      case GrayPlane::LSB:
        bits = g.gray_lsb_bits;
        break;
      case GrayPlane::MSB:
        bits = g.gray_msb_bits;
        break;
    }
    if (bits) {
      // Gray planes use inverted bit polarity: bit=1 means gray pixel present.
      const bool invert = (plane != GrayPlane::BW);
      RenderTarget t{buf, DisplayFrame::kStride, 0, 0, DisplayFrame::kPhysicalWidth, DisplayFrame::kPhysicalHeight};
      draw_glyph_impl_(t, x, y, bits, g.bitmap_width, g.bitmap_height, g.x_offset, g.y_offset, white, invert);
    }
  }

 private:
  // Describes a render target: a pixel buffer with its own stride and physical offset/bounds.
  // phys_x0 must be byte-aligned (multiple of 8).
  struct RenderTarget {
    uint8_t* buf;
    int stride;   // bytes per physical row
    int phys_x0;  // absolute physical X of the left edge (byte-aligned)
    int phys_y0;  // absolute physical Y of the top edge
    int phys_w;   // width in pixels
    int phys_h;   // height in rows
  };

  static void draw_glyph_impl_(const RenderTarget& t, int x, int y, const uint8_t* bits, int bitmap_width,
                               int bitmap_height, int x_offset, int y_offset, bool white, bool invert_select = false) {
    if (!bits || bitmap_width <= 0 || bitmap_height <= 0)
      return;
    const int gx = x + x_offset;
    const int gy = y + y_offset;
    const int row_stride = (bitmap_width + 7) / 8;

    for (int row = 0; row < bitmap_height; ++row) {
      const int ly = gy + row;
      const uint8_t* row_data = bits + row * row_stride;
      for (int col = 0; col < bitmap_width; ++col) {
        const int lx = gx + col;
        const bool bit_set = (row_data[col >> 3] >> (7 - (col & 7))) & 1;
        if (invert_select ? bit_set : !bit_set) {
          // Ink pixel — compute absolute physical coords (Deg90: logical Y → physical X).
          const int px = ly;
          const int py = DisplayFrame::kPhysicalHeight - 1 - lx;
          if (px < t.phys_x0 || px >= t.phys_x0 + t.phys_w)
            continue;
          if (py < t.phys_y0 || py >= t.phys_y0 + t.phys_h)
            continue;
          const int lpx = px - t.phys_x0;  // local pixel X within target
          const int lpy = py - t.phys_y0;  // local pixel Y within target
          const size_t bidx = static_cast<size_t>(lpy * t.stride + lpx / 8);
          const uint8_t bit = static_cast<uint8_t>(0x80u >> (lpx & 7));
          if (white)
            t.buf[bidx] |= bit;
          else
            t.buf[bidx] &= static_cast<uint8_t>(~bit);
        }
      }
    }
  }

 public:
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

  // ── Plane-aware text rendering (for grayscale two-pass) ─────────────────

  // Render text from a specific grayscale plane into an explicit buffer.
  int draw_text_plane(uint8_t* buf, int x, int baseline_y, const char* text, size_t len, const BitmapFontSet& fonts,
                      GrayPlane plane, bool white, FontStyle style = FontStyle::Regular,
                      FontSize size = FontSize::Normal);

  // ── Grayscale display operations ─────────────────────────────────────

  // Write the inactive buffer to BW RAM only (no refresh).
  void write_ram_bw() {
    display_.write_ram_bw(inactive_());
  }

  // Write the inactive buffer to RED RAM only (no refresh).
  void write_ram_red() {
    display_.write_ram_red(inactive_());
  }

  // Trigger grayscale refresh (assumes BW/RED RAM already written).
  void grayscale_refresh(bool turnOffScreen = false) {
    display_.grayscale_refresh(turnOffScreen);
  }

  // Revert grayscale using the active (displayed) buffer as prev_pixels.
  void revert_grayscale() {
    display_.revert_grayscale(active_());
  }

  // Provide direct access to the inactive buffer for multi-pass rendering.
  uint8_t* render_buf() {
    return inactive_();
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
    display_.partial_refresh(inactive_(), active_());
    active_idx_ = 1 - active_idx_;
  }

  // Call full hardware refresh using the current inactive buffer, then sync both.
  void full_refresh(RefreshMode mode = RefreshMode::Half, bool turnOffScreen = false) {
    display_.full_refresh(inactive_(), mode, turnOffScreen);
    memcpy(bufs_[active_idx_], bufs_[1 - active_idx_], kBufSize);
    active_idx_ = 1 - active_idx_;
  }

  void deep_sleep() {
    display_.deep_sleep();
  }

  void upload_current_frame() {
    display_.write_ram_bw(active_());
    // display_.write_ram_red(active_());
  }

  // Write pre-built LSB/MSB plane arrays to display RAM, trigger a grayscale
  // refresh with screen power-off, then deep sleep. Intended for the power-off splash screen.
  // Uses draw_image() so that images wider than kPhysicalWidth (e.g. 800px) are clipped correctly.
  void show_grayscale_image(const uint8_t* lsb, const uint8_t* msb, uint16_t w, uint16_t h) {
    fill(true);
    draw_image(lsb, 0, 0, w, h);
    display_.write_ram_bw(inactive_());
    fill(true);
    draw_image(msb, 0, 0, w, h);
    display_.write_ram_red(inactive_());
    display_.grayscale_refresh(/*turnOffScreen=*/true);
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

  // ── Loading box region update ────────────────────────────────────────────
  //
  // A small fixed region at the bottom-centre of the logical screen, used to
  // show a "Converting…" indicator while both display buffers are in use as
  // scratch.  The region is byte-aligned in physical space so the extraction
  // can be done with plain memcpy (no bit-shifting).
  //
  // Logical box: 256 × 40 px, centred horizontally, 4 px from the bottom.
  //   lx = (480 - 256) / 2 = 112,  ly = 788 - 40 - 4 = 744
  // Physical (Deg90 rotation):
  //   px = ly = 744  (byte-aligned: 744 / 8 = 93)
  //   py = PhysH - lx - lw = 480 - 112 - 256 = 112
  //   pw = lh = 40   →  stride = 5 bytes
  //   ph = lw = 256
  // Mini-buffer size: 5 × 256 = 1280 bytes each (stack-allocated).

  static constexpr int kLoadLogW = 256;
  static constexpr int kLoadLogH = 40;
  static constexpr int kLoadLogX = (kWidth - kLoadLogW) / 2;  // 112
  static constexpr int kLoadLogY = kHeight - kLoadLogH;       // - 4;   // 744

  static constexpr int kLoadPhysX = kLoadLogY;                                              // 744
  static constexpr int kLoadPhysY = DisplayFrame::kPhysicalHeight - kLoadLogX - kLoadLogW;  // 112
  static constexpr int kLoadPhysW = kLoadLogH;                                              // 40
  static constexpr int kLoadPhysH = kLoadLogW;                                              // 256
  static constexpr int kLoadStride = (kLoadPhysW + 7) / 8;                                  // 5
  static constexpr int kLoadBufBytes = kLoadStride * kLoadPhysH;                            // 1280

  // Bar geometry (derived from loading box constants).
  static constexpr int kBarPad = 6;
  static constexpr int kBarH = 6;
  static constexpr int kBarX = kLoadLogX + kBarPad;                // 118
  static constexpr int kBarW = kLoadLogW - kBarPad * 2;            // 244
  static constexpr int kBarY = kLoadLogY + kLoadLogH - kBarH - 4;  // 774

  // Draw a loading box (label + progress bar) and push it to the display
  // via a region-only hardware update.  The main draw buffers are NEVER
  // touched — all rendering goes directly into the 1280-byte mini-buffer.
  // Both display buffers may be used as scratch before or after this call.
  //
  //   text         — label shown inside the box (e.g. "Converting…")
  //   progress_pct — 0-100; controls how much of the bar is filled
  void show_loading(const char* text, int progress_pct) {
    static_assert((kLoadPhysX + 12) % 8 == 0, "kLoadPhysX + panel offset must be byte-aligned");
    if (display_.is_busy())
      return;  // skip if panel is still refreshing
    uint8_t new_buf[kLoadBufBytes];
    render_loading_box_(new_buf, text, progress_pct);
    display_.partial_refresh_region(kLoadPhysX, kLoadPhysY, kLoadPhysW, kLoadPhysH, new_buf, kLoadStride);
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

  // Render target for the full inactive buffer.
  RenderTarget full_target_() {
    return {inactive_(), DisplayFrame::kStride, 0, 0, DisplayFrame::kPhysicalWidth, DisplayFrame::kPhysicalHeight};
  }

  // Render target for the mini loading-box buffer (absolute physical coords of the box).
  static RenderTarget mini_target_(uint8_t* buf) {
    return {buf, kLoadStride, kLoadPhysX, kLoadPhysY, kLoadPhysW, kLoadPhysH};
  }

  // Fill a physical horizontal span [x1, x2) on physical row `row` (absolute physical coords).
  // phys_x0 must be byte-aligned so that local_x has the same bit position as absolute x.
  static void fill_row_physical_(const RenderTarget& t, int row, int x1, int x2, bool white) {
    x1 = std::max(x1, t.phys_x0);
    x2 = std::min(x2, t.phys_x0 + t.phys_w);
    if (x1 >= x2 || row < t.phys_y0 || row >= t.phys_y0 + t.phys_h)
      return;
    const int lrow = row - t.phys_y0;
    const int lx1 = x1 - t.phys_x0;
    const int lx2 = x2 - t.phys_x0;
    const int bx1 = lx1 / 8;
    const int bx2 = (lx2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (lx1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((lx2 - 1) & 7)));
    uint8_t* rp = t.buf + lrow * t.stride;
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

  // Fill a physical rectangle (absolute physical coords).
  static void fill_rect_physical_(const RenderTarget& t, int rx, int ry, int rw, int rh, bool white) {
    const int x1 = std::max(rx, t.phys_x0);
    const int y1 = std::max(ry, t.phys_y0);
    const int x2 = std::min(rx + rw, t.phys_x0 + t.phys_w);
    const int y2 = std::min(ry + rh, t.phys_y0 + t.phys_h);
    if (x1 >= x2 || y1 >= y2)
      return;
    for (int row = y1; row < y2; ++row)
      fill_row_physical_(t, row, x1, x2, white);
  }

  // Fill physical column `pcol` for rows [py1, py2) (absolute physical coords).
  static void fill_col_physical_(const RenderTarget& t, int pcol, int py1, int py2, bool white) {
    py1 = std::max(py1, t.phys_y0);
    py2 = std::min(py2, t.phys_y0 + t.phys_h);
    if (pcol < t.phys_x0 || pcol >= t.phys_x0 + t.phys_w || py1 >= py2)
      return;
    const int lrow0 = py1 - t.phys_y0;
    const int lrow1 = py2 - t.phys_y0;
    const int lpcol = pcol - t.phys_x0;
    const int bidx = lpcol / 8;
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (lpcol & 7));
    for (int r = lrow0; r < lrow1; ++r) {
      uint8_t* p = t.buf + r * t.stride + bidx;
      if (white)
        *p |= bit;
      else
        *p &= static_cast<uint8_t>(~bit);
    }
  }

  // Draw text into any RenderTarget using a BitmapFont.
  static void draw_text_to_(const RenderTarget& t, int x, int baseline_y, const char* text, const BitmapFont& font,
                            bool white) {
    if (!text || !*text || !font.valid())
      return;
    for (const char* p = text; *p; ++p) {
      GlyphData g = font.glyph_data(static_cast<uint8_t>(*p), FontStyle::Regular);
      if (g.bits)
        draw_glyph_impl_(t, x, baseline_y, g.bits, g.bitmap_width, g.bitmap_height, g.x_offset, g.y_offset, white);
      x += g.advance_width;
    }
  }

  // Render the loading box into a mini-buffer via the unified helpers.
  // Never reads or writes bufs_.
  static void render_loading_box_(uint8_t* mini, const char* text, int progress_pct) {
    const RenderTarget t = mini_target_(mini);
    // White fill.
    memset(mini, 0xFF, kLoadBufBytes);
    // Text centred horizontally inside the box.
    const BitmapFont& font = ui_font_();
    if (text && *text) {
      const int tw = static_cast<int>(font.word_width(text, strlen(text), FontStyle::Regular));
      const int text_lx = kWidth / 2 - tw / 2;
      const int baseline_ly = kLoadLogY + 8 + static_cast<int>(font.baseline());
      draw_text_to_(t, text_lx, baseline_ly, text, font, /*white=*/false);
    }
    // Progress bar border (logical coords → physical via helpers).
    // Deg90: fill_rect_physical_(t, phys_x=ly, phys_y=PhysH-lx-lw, phys_w=lh, phys_h=lw)
    auto bar_rect = [&](int lx, int ly, int lw, int lh) {
      fill_rect_physical_(t, ly, DisplayFrame::kPhysicalHeight - lx - lw, lh, lw, /*white=*/false);
    };
    bar_rect(kBarX, kBarY, kBarW, 1);              // top
    bar_rect(kBarX, kBarY + kBarH - 1, kBarW, 1);  // bottom
    bar_rect(kBarX, kBarY, 1, kBarH);              // left
    bar_rect(kBarX + kBarW - 1, kBarY, 1, kBarH);  // right
    // Progress fill.
    const int filled = (progress_pct * (kBarW - 2)) / 100;
    if (filled > 0)
      bar_rect(kBarX + 1, kBarY + 1, filled, kBarH - 2);
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

inline int DrawBuffer::draw_text_plane(uint8_t* buf, int x, int baseline_y, const char* text, size_t len,
                                       const BitmapFontSet& fonts, GrayPlane plane, bool white, FontStyle style,
                                       FontSize size) {
  const BitmapFont* f = fonts.get(size);
  if (!text || len == 0 || !f || !f->valid())
    return x;
  const char* p = text;
  const char* end = text + len;
  int cursor_x = x;
  while (p < end) {
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

    GlyphData g = f->glyph_data(cp, style);
    draw_glyph_plane(buf, cursor_x, baseline_y, g, plane, white);
    cursor_x += g.advance_width;
  }
  return cursor_x;
}

}  // namespace microreader
