#pragma once

#include <SDL.h>

#include <vector>

#include "microreader/display/DrawBuffer.h"
#include "runtime.h"

// Desktop e-ink emulator. Renders the pixel buffer to an SDL window.
// Maintains a float sim_ buffer to reproduce e-ink colour tonality;
// partial_refresh snaps only changed pixels so the paper texture is preserved.
class DesktopEmulatorDisplay final : public microreader::IDisplay {
 public:
  static constexpr int kPixels = microreader::DisplayFrame::kPhysicalWidth * microreader::DisplayFrame::kPhysicalHeight;

  explicit DesktopEmulatorDisplay(DesktopRuntime& rt) : rt_(rt), sim_(kPixels, 1.0f) {}

  void set_rotation(microreader::Rotation r) override {
    rotation_ = r;
    rt_.apply_rotation(r);
  }

  void full_refresh(const uint8_t* pixels, microreader::RefreshMode /*mode*/) override {
    for (int i = 0; i < kPixels; ++i) {
      const int y = i / microreader::DisplayFrame::kPhysicalWidth;
      const int x = i % microreader::DisplayFrame::kPhysicalWidth;
      const std::size_t byte_idx = static_cast<std::size_t>(y * microreader::DisplayFrame::kStride + x / 8);
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
      sim_[i] = (pixels[byte_idx] & bit) ? 1.0f : 0.0f;
    }
    render_();
  }

  void partial_refresh(const uint8_t* new_pixels) override {
    for (int y = 0; y < microreader::DisplayFrame::kPhysicalHeight; ++y) {
      for (int x = 0; x < microreader::DisplayFrame::kPhysicalWidth; ++x) {
        const std::size_t byte_idx = static_cast<std::size_t>(y * microreader::DisplayFrame::kStride + x / 8);
        const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
        const bool new_white = (new_pixels[byte_idx] & bit) != 0;
        const bool old_white = sim_[y * microreader::DisplayFrame::kPhysicalWidth + x] >= 0.5f;
        if (old_white != new_white)
          sim_[y * microreader::DisplayFrame::kPhysicalWidth + x] = new_white ? 1.0f : 0.0f;
        // Unchanged pixels keep their current sim_ value (preserves ghosting appearance).
      }
    }
    render_();
  }

 private:
  DesktopRuntime& rt_;
  microreader::Rotation rotation_ = microreader::Rotation::Deg0;
  std::vector<float> sim_;

  // E-ink palette: RGB endpoints for black (s=0) and white (s=1).
  static constexpr uint8_t kBlackR = 0x18, kBlackG = 0x1A, kBlackB = 0x1C;
  static constexpr uint8_t kWhiteR = 0xE8, kWhiteG = 0xDC, kWhiteB = 0xC8;

  void render_() {
    void* raw = nullptr;
    int pitch = 0;
    SDL_LockTexture(rt_.texture(), nullptr, &raw, &pitch);
    auto* p = static_cast<uint8_t*>(raw);
    for (int y = 0; y < microreader::DisplayFrame::kPhysicalHeight; ++y) {
      uint8_t* row = p + y * pitch;
      for (int x = 0; x < microreader::DisplayFrame::kPhysicalWidth; ++x) {
        const float s = sim_[y * microreader::DisplayFrame::kPhysicalWidth + x];
        row[x * 3 + 0] = static_cast<uint8_t>(kBlackR + s * (kWhiteR - kBlackR));
        row[x * 3 + 1] = static_cast<uint8_t>(kBlackG + s * (kWhiteG - kBlackG));
        row[x * 3 + 2] = static_cast<uint8_t>(kBlackB + s * (kWhiteB - kBlackB));
      }
    }
    SDL_UnlockTexture(rt_.texture());

    const bool sideways = rotation_ == microreader::Rotation::Deg90;
    const int win_w = sideways ? microreader::DisplayFrame::kPhysicalHeight : microreader::DisplayFrame::kPhysicalWidth;
    const int win_h = sideways ? microreader::DisplayFrame::kPhysicalWidth : microreader::DisplayFrame::kPhysicalHeight;
    SDL_Rect dst = {(win_w - microreader::DisplayFrame::kPhysicalWidth) / 2,
                    (win_h - microreader::DisplayFrame::kPhysicalHeight) / 2, microreader::DisplayFrame::kPhysicalWidth,
                    microreader::DisplayFrame::kPhysicalHeight};
    SDL_RenderClear(rt_.renderer());
    SDL_RenderCopyEx(rt_.renderer(), rt_.texture(), nullptr, &dst, static_cast<double>(static_cast<int>(rotation_)),
                     nullptr, SDL_FLIP_NONE);
    SDL_RenderPresent(rt_.renderer());
  }
};
