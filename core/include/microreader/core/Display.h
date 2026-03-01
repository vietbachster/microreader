#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace microreader {

enum class RefreshMode { Full, Fast };

enum class Rotation { Deg0 = 0, Deg90 = 90 };

// 800×480 monochrome frame buffer (1 bit per pixel, row-major, MSB = leftmost).
// Total storage: 100 bytes/row × 480 rows = 48 000 bytes.
struct DisplayFrame {
  static constexpr int kWidth = 800;
  static constexpr int kHeight = 480;
  static constexpr int kStride = (kWidth + 7) / 8;  // bytes per row
  static constexpr std::size_t kPixelBytes = static_cast<std::size_t>(kStride) * kHeight;

  std::array<uint8_t, kPixelBytes> pixels{};

  void set_pixel(int x, int y, bool on) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight)
      return;
    const std::size_t idx = static_cast<std::size_t>(y * kStride + x / 8);
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
    if (on)
      pixels[idx] |= bit;
    else
      pixels[idx] &= static_cast<uint8_t>(~bit);
  }

  bool get_pixel(int x, int y) const {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight)
      return false;
    const std::size_t idx = static_cast<std::size_t>(y * kStride + x / 8);
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
    return (pixels[idx] & bit) != 0;
  }

  void fill(bool on) {
    pixels.fill(on ? 0xFF : 0x00);
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
