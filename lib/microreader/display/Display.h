#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace microreader {

enum class Rotation { Deg0 = 0, Deg90 = 90 };

// Refresh mode for full-screen updates.
enum class RefreshMode { Full, Half };

// Physical screen constants and per-pixel helpers.
// DisplayFrame wraps an external buffer and provides set_pixel/get_pixel;
// it is used internally for font rendering and bit-packing, not as a
// pipeline object passed between layers.
struct DisplayFrame {
  static constexpr int kPhysicalWidth = 800;
  static constexpr int kPhysicalHeight = 480;
  static constexpr int kStride = (kPhysicalWidth + 7) / 8;
  static constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kStride) * kPhysicalHeight;

  uint8_t* const pixels;
  Rotation rotation_ = Rotation::Deg0;

  explicit DisplayFrame(uint8_t* buf, Rotation r = Rotation::Deg0) : pixels(buf), rotation_(r) {}

  DisplayFrame(const DisplayFrame&) = delete;
  DisplayFrame& operator=(const DisplayFrame&) = delete;

  int width() const {
    return rotation_ == Rotation::Deg90 ? kPhysicalHeight : kPhysicalWidth;
  }
  int height() const {
    return rotation_ == Rotation::Deg90 ? kPhysicalWidth : kPhysicalHeight;
  }

  void set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= width() || y < 0 || y >= height())
      return;
    const auto [px, py] = to_physical(x, y);
    const std::size_t idx = static_cast<std::size_t>(py * kStride + px / 8);
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
    if (on)
      pixels[idx] |= bit;
    else
      pixels[idx] &= static_cast<uint8_t>(~bit);
  }

  bool get_pixel(int x, int y) const {
    if (x < 0 || x >= width() || y < 0 || y >= height())
      return false;
    const auto [px, py] = to_physical(x, y);
    const std::size_t idx = static_cast<std::size_t>(py * kStride + px / 8);
    return (pixels[idx] & (0x80u >> (px & 7))) != 0;
  }

  void fill(bool on) {
    memset(pixels, on ? 0xFF : 0x00, kPixelBytes);
  }

  // Fill a horizontal span [x1, x2) on logical row y.
  // Handles rotation automatically — efficient byte ops for Deg0,
  // per-column ops for Deg90.
  void fill_row(int y, int x1, int x2, bool on) {
    if (rotation_ == Rotation::Deg0) {
      fill_row_physical_(y, x1, x2, on);
    } else {
      // Deg90: logical (x, y) → physical (y, kPhysicalHeight-1-x)
      // Logical row y, cols [x1, x2) → physical col y, rows [kPhysH-x2, kPhysH-x1)
      fill_col_physical_(y, kPhysicalHeight - x2, kPhysicalHeight - x1, on);
    }
  }

 private:
  std::pair<int, int> to_physical(int x, int y) const {
    if (rotation_ == Rotation::Deg90)
      return {y, kPhysicalHeight - 1 - x};
    return {x, y};
  }

  // Fast horizontal span fill in physical coords.
  void fill_row_physical_(int row, int x1, int x2, bool on) {
    x1 = std::max(x1, 0);
    x2 = std::min(x2, kPhysicalWidth);
    if (x1 >= x2 || row < 0 || row >= kPhysicalHeight)
      return;
    const int bx1 = x1 / 8;
    const int bx2 = (x2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (x1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((x2 - 1) & 7)));
    uint8_t* rp = pixels + row * kStride;
    if (bx1 == bx2) {
      const auto m = static_cast<uint8_t>(lmask & rmask);
      if (on)
        rp[bx1] |= m;
      else
        rp[bx1] &= static_cast<uint8_t>(~m);
    } else {
      if (on)
        rp[bx1] |= lmask;
      else
        rp[bx1] &= static_cast<uint8_t>(~lmask);
      if (bx2 > bx1 + 1)
        memset(rp + bx1 + 1, on ? 0xFF : 0x00, bx2 - bx1 - 1);
      if (on)
        rp[bx2] |= rmask;
      else
        rp[bx2] &= static_cast<uint8_t>(~rmask);
    }
  }

  // Vertical column fill in physical coords: set bits at column `col` for rows [y1, y2).
  void fill_col_physical_(int col, int y1, int y2, bool on) {
    if (col < 0 || col >= kPhysicalWidth)
      return;
    y1 = std::max(y1, 0);
    y2 = std::min(y2, kPhysicalHeight);
    const int byte_idx = col / 8;
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (col & 7));
    for (int row = y1; row < y2; ++row) {
      uint8_t* p = pixels + row * kStride + byte_idx;
      if (on)
        *p |= mask;
      else
        *p &= static_cast<uint8_t>(~mask);
    }
  }
};

// Display driver interface.
//
// Pixel buffers (ground_truth, target) are owned by DisplayQueue.
// The display receives buffer pointers and dirty flags each tick.
class IDisplay {
 public:
  virtual ~IDisplay() = default;

  // Called each tick while commands are in flight.
  // ground_truth = settled pixel state; target = where pixels are heading.
  // Dirty flags indicate which buffers changed since the last call.
  // refresh: if false, upload data but skip the display refresh cycle.
  virtual void tick(const uint8_t* ground_truth, bool gt_dirty, const uint8_t* target, bool target_dirty,
                    bool refresh = true) = 0;

  // Full physical refresh.  `pixels` is the settled (ground_truth == target) state.
  virtual void full_refresh(const uint8_t* pixels, RefreshMode mode) = 0;

  // Partial refresh: old_pixels → RED RAM, new_pixels → BW RAM, then fast refresh.
  // The display LUT uses the RED/BW difference to drive only changed pixels.
  virtual void partial_refresh(const uint8_t* old_pixels, const uint8_t* new_pixels) = 0;

  // Put the display controller into deep sleep (low-power mode).
  virtual void deep_sleep() {}

  virtual void set_rotation(Rotation r) {
    (void)r;
  }
  virtual Rotation rotation() const {
    return Rotation::Deg0;
  }
};

}  // namespace microreader
