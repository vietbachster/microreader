#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace microreader {

enum class RefreshMode { Full, Fast };

enum class Rotation { Deg0 = 0, Deg90 = 90 };

// Non-owning view over a 800×480 monochrome pixel buffer
// (1 bit per pixel, row-major, MSB = leftmost pixel).
//
// The caller owns the underlying uint8_t[kPixelBytes] storage.
// All set_pixel / get_pixel calls use logical (rotation-aware) coordinates;
// the physical buffer is always written in Deg0 landscape layout.
struct DisplayFrame {
  // Physical buffer dimensions (always fixed).
  static constexpr int kPhysicalWidth = 800;
  static constexpr int kPhysicalHeight = 480;
  static constexpr int kStride = (kPhysicalWidth + 7) / 8;  // bytes per row
  static constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kStride) * kPhysicalHeight;

  uint8_t* const pixels;  // non-owning pointer to kPixelBytes of pixel data
  Rotation rotation_ = Rotation::Deg0;

  explicit DisplayFrame(uint8_t* buf, Rotation r = Rotation::Deg0) : pixels(buf), rotation_(r) {}

  // Not copyable/movable — the pointer is const and the buffer isn't ours.
  DisplayFrame(const DisplayFrame&) = delete;
  DisplayFrame& operator=(const DisplayFrame&) = delete;

  // Logical dimensions — swap when rotated 90°.
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
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
    return (pixels[idx] & bit) != 0;
  }

  void fill(bool on) {
    memset(pixels, on ? 0xFF : 0x00, kPixelBytes);
  }

 private:
  // Deg90 clockwise: logical (lx, ly) → physical (ly, H−1−lx).
  std::pair<int, int> to_physical(int x, int y) const {
    if (rotation_ == Rotation::Deg90)
      return {y, kPhysicalHeight - 1 - x};
    return {x, y};
  }
};

class IDisplay {
 public:
  virtual ~IDisplay() = default;
  virtual void present(const DisplayFrame& frame, RefreshMode mode) = 0;
  virtual void set_rotation(Rotation r) {
    (void)r;
  }
  virtual Rotation rotation() const {
    return Rotation::Deg0;
  }
};

}  // namespace microreader
