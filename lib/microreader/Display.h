#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace microreader {

enum class Rotation { Deg0 = 0, Deg90 = 90 };

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

// Display interface.
//
// Owns the two 1-bit pixel buffers that the DisplayQueue operates on:
//   ground_truth — settled pixel state (all commands fully committed).
//   target       — ground_truth with all in-flight commands overlaid;
//                  where every pixel is heading.
//
// Both buffers start white (0xFF). The controller receives mutable pointers
// to them via ground_truth_buf()/target_buf() and writes into them directly.
// tick() is called each loop cycle (~30 ms); implementations read their own
// buffers to drive the panel or simulator — no parameters needed.
class IDisplay {
 public:
  IDisplay() {
    memset(ground_truth_, 0xFF, DisplayFrame::kPixelBytes);
    memset(target_, 0xFF, DisplayFrame::kPixelBytes);
  }
  virtual ~IDisplay() = default;

  // Called each loop tick. Implementations read ground_truth_/target_.
  virtual void tick() = 0;

  // Perform a full refresh: immediately commit target to ground_truth.
  // Override in hardware drivers to also trigger a physical slow refresh.
  virtual void full_refresh() {
    memcpy(ground_truth_, target_, DisplayFrame::kPixelBytes);
  }

  virtual void set_rotation(Rotation r) {
    (void)r;
  }
  virtual Rotation rotation() const {
    return Rotation::Deg0;
  }

  // Mutable buffer accessors for DisplayQueue.
  uint8_t* ground_truth_buf() {
    return ground_truth_;
  }
  uint8_t* target_buf() {
    return target_;
  }

 protected:
  alignas(4) uint8_t ground_truth_[DisplayFrame::kPixelBytes];
  alignas(4) uint8_t target_[DisplayFrame::kPixelBytes];
};

}  // namespace microreader
