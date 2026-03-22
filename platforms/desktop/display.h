#pragma once

#include <SDL.h>

#include <vector>

#include "microreader/Display.h"
#include "runtime.h"

// Simulates e-ink pixel physics for the desktop emulator.
// On each tick(ground_truth, target) call the float sim buffer steps each
// pixel toward `target` at 1/phases per call, producing a smooth transition
// effect that mirrors the real panel's gradual particle movement.
class DesktopEmulatorDisplay final : public microreader::IDisplay {
 public:
  bool show_transitions = true;  // overlay pink tint on in-flight pixels

  // Point at DisplayQueue::phases so the sim step size stays in sync.
  void set_phases_source(const int* src) {
    phases_ = src;
  }

  static constexpr int kPixels = microreader::DisplayFrame::kPhysicalWidth * microreader::DisplayFrame::kPhysicalHeight;

  explicit DesktopEmulatorDisplay(DesktopRuntime& rt) : rt_(rt), sim_(kPixels, 1.0f), transitioning_(kPixels, false) {}

  void set_rotation(microreader::Rotation r) override {
    rotation_ = r;
    rt_.apply_rotation(r);
  }

  microreader::Rotation rotation() const override {
    return rotation_;
  }

  // Called each loop tick. Steps sim toward target, then renders.
  void tick(const uint8_t* ground_truth, bool /*gt_dirty*/, const uint8_t* target, bool /*target_dirty*/) override {
    step_and_render(ground_truth, target);
  }

  // Snap sim state directly to the settled buffer (no animation), then render.
  void full_refresh(const uint8_t* pixels, microreader::RefreshMode /*mode*/) override {
    for (int y = 0; y < microreader::DisplayFrame::kPhysicalHeight; ++y) {
      for (int x = 0; x < microreader::DisplayFrame::kPhysicalWidth; ++x) {
        const std::size_t byte_idx = static_cast<std::size_t>(y * microreader::DisplayFrame::kStride + x / 8);
        const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
        sim_[y * microreader::DisplayFrame::kPhysicalWidth + x] = (pixels[byte_idx] & bit) ? 1.0f : 0.0f;
      }
    }
    // ground_truth == target after full_refresh, so pass pixels for both.
    step_and_render(pixels, pixels);
  }

 private:
  DesktopRuntime& rt_;
  microreader::Rotation rotation_ = microreader::Rotation::Deg0;
  std::vector<float> sim_;           // per-pixel visual state: 0.0=black, 1.0=white
  std::vector<bool> transitioning_;  // true while ground_truth != target
  const int* phases_ = nullptr;

  int phases() const {
    return phases_ ? *phases_ : 1;
  }

  void step_and_render(const uint8_t* ground_truth, const uint8_t* target) {
    const float step = 1.0f / static_cast<float>(phases());
    for (int y = 0; y < microreader::DisplayFrame::kPhysicalHeight; ++y) {
      for (int x = 0; x < microreader::DisplayFrame::kPhysicalWidth; ++x) {
        const std::size_t byte_idx = static_cast<std::size_t>(y * microreader::DisplayFrame::kStride + x / 8);
        const uint8_t bit = static_cast<uint8_t>(0x80u >> (x & 7));
        const bool gt_white = (ground_truth[byte_idx] & bit) != 0;
        const bool target_white = (target[byte_idx] & bit) != 0;
        float& s = sim_[y * microreader::DisplayFrame::kPhysicalWidth + x];
        const bool transitioning = (gt_white != target_white);
        transitioning_[y * microreader::DisplayFrame::kPhysicalWidth + x] = transitioning;
        if (transitioning) {
          // Pixel is transitioning: step sim toward target.
          s += target_white ? step : -step;
          if (s > 1.0f)
            s = 1.0f;
          if (s < 0.0f)
            s = 0.0f;
        }
      }
    }

    void* raw = nullptr;
    int pitch = 0;
    SDL_LockTexture(rt_.texture(), nullptr, &raw, &pitch);
    auto* p = static_cast<uint8_t*>(raw);
    for (int y = 0; y < microreader::DisplayFrame::kPhysicalHeight; ++y) {
      uint8_t* row = p + y * pitch;
      for (int x = 0; x < microreader::DisplayFrame::kPhysicalWidth; ++x) {
        const float s = sim_[y * microreader::DisplayFrame::kPhysicalWidth + x];
        const bool t = show_transitions && transitioning_[y * microreader::DisplayFrame::kPhysicalWidth + x];
        if (t) {
          // Tint transitioning pixels pink.
          row[x * 3 + 0] = static_cast<uint8_t>(0xE8 + s * (0xFF - 0xE8));
          row[x * 3 + 1] = static_cast<uint8_t>(0x40 + s * (0xA0 - 0x40));
          row[x * 3 + 2] = static_cast<uint8_t>(0x80 + s * (0xC0 - 0x80));
        } else {
          // Settled: e-ink grey palette.
          row[x * 3 + 0] = static_cast<uint8_t>(0x18 + s * (0xE8 - 0x18));
          row[x * 3 + 1] = static_cast<uint8_t>(0x1A + s * (0xDC - 0x1A));
          row[x * 3 + 2] = static_cast<uint8_t>(0x1C + s * (0xC8 - 0x1C));
        }
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
