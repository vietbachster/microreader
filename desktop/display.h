#pragma once

#include <SDL.h>

#include "microreader/core/Display.h"
#include "runtime.h"

// Renders the 1-bit DisplayFrame into an SDL texture and presents it.
// White pixels are drawn as near-white (0xE8, 0xDC, 0xC8) to mimic e-ink paper.
// The texture is always 800×480; rotation is applied via SDL_RenderCopyEx so
// the window resizes to match (480×800 for 90°/270°).
class DesktopEmulatorDisplay final : public microreader::IDisplay {
 public:
  explicit DesktopEmulatorDisplay(DesktopRuntime& rt) : rt_(rt) {}

  void set_rotation(microreader::Rotation r) override {
    rotation_ = r;
    rt_.apply_rotation(r);
  }

  microreader::Rotation rotation() const override {
    return rotation_;
  }

  void present(const microreader::DisplayFrame& frame, microreader::RefreshMode /*mode*/) override {
    // Upload pixel buffer to texture.
    void* raw = nullptr;
    int pitch = 0;
    SDL_LockTexture(rt_.texture(), nullptr, &raw, &pitch);
    auto* p = static_cast<uint8_t*>(raw);
    // The SDL texture is always in physical (unrotated) 800×480 layout;
    // rotation is applied visually by SDL_RenderCopyEx below.
    // Read directly from the physical pixel buffer to avoid the logical
    // coordinate translation in get_pixel().
    for (int y = 0; y < microreader::DisplayFrame::kPhysicalHeight; ++y) {
      uint8_t* row = p + y * pitch;
      for (int x = 0; x < microreader::DisplayFrame::kPhysicalWidth; ++x) {
        const std::size_t idx = static_cast<std::size_t>(y * microreader::DisplayFrame::kStride + x / 8);
        const bool on = (frame.pixels[idx] & (0x80u >> (x & 7))) != 0;
        if (on) {
          row[x * 3 + 0] = 0xE8;  // warm white R
          row[x * 3 + 1] = 0xDC;  // warm white G
          row[x * 3 + 2] = 0xC8;  // warm white B
        } else {
          row[x * 3 + 0] = 0x18;  // near-black R
          row[x * 3 + 1] = 0x1A;  // near-black G
          row[x * 3 + 2] = 0x1C;  // near-black B
        }
      }
    }
    SDL_UnlockTexture(rt_.texture());

    // Determine logical window dimensions for this rotation.
    const bool sideways = rotation_ == microreader::Rotation::Deg90;
    const int win_w = sideways ? microreader::DisplayFrame::kPhysicalHeight : microreader::DisplayFrame::kPhysicalWidth;
    const int win_h = sideways ? microreader::DisplayFrame::kPhysicalWidth : microreader::DisplayFrame::kPhysicalHeight;

    // Position the (always 800×480) texture centred in the logical window so
    // SDL_RenderCopyEx rotates it about the correct pivot.
    SDL_Rect dst = {(win_w - microreader::DisplayFrame::kPhysicalWidth) / 2,
                    (win_h - microreader::DisplayFrame::kPhysicalHeight) / 2, microreader::DisplayFrame::kPhysicalWidth,
                    microreader::DisplayFrame::kPhysicalHeight};

    SDL_RenderClear(rt_.renderer());
    SDL_RenderCopyEx(rt_.renderer(), rt_.texture(), nullptr, &dst, static_cast<double>(static_cast<int>(rotation_)),
                     nullptr, SDL_FLIP_NONE);
    SDL_RenderPresent(rt_.renderer());
  }

 private:
  DesktopRuntime& rt_;
  microreader::Rotation rotation_ = microreader::Rotation::Deg0;
};
