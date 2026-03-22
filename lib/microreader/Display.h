#pragma once

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

 private:
  std::pair<int, int> to_physical(int x, int y) const {
    if (rotation_ == Rotation::Deg90)
      return {y, kPhysicalHeight - 1 - x};
    return {x, y};
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
  virtual void tick(const uint8_t* ground_truth, bool gt_dirty, const uint8_t* target, bool target_dirty) = 0;

  // Full physical refresh.  `pixels` is the settled (ground_truth == target) state.
  virtual void full_refresh(const uint8_t* pixels, RefreshMode mode) = 0;

  virtual void set_rotation(Rotation r) {
    (void)r;
  }
  virtual Rotation rotation() const {
    return Rotation::Deg0;
  }
};

}  // namespace microreader
