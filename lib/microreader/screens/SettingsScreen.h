#pragma once

#include <functional>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "BouncingBallDemo.h"
#include "GrayscaleDemo.h"
#include "ListMenuScreen.h"

namespace microreader {

class SettingsScreen final : public ListMenuScreen {
 public:
  SettingsScreen() = default;

  void set_data_dir(const char* dir) {
    data_dir_ = dir;
  }

  // Optional callback for "Invalidate Font" action. When set, the item is
  // shown in the menu (ESP32 only). Should zero the font partition CRC so the
  // next book open re-provisions from firmware.
  void set_invalidate_font_fn(std::function<void()> fn) {
    invalidate_font_fn_ = std::move(fn);
  }

  const char* name() const override {
    return "Settings";
  }

 protected:
  void on_start() override;
  bool on_select(int index) override;

 private:
  BouncingBallDemo bouncing_ball_;
  GrayscaleDemo grayscale_demo_;
  const char* data_dir_ = nullptr;
  std::function<void()> invalidate_font_fn_;

  // Item indices (assigned during on_start).
  int idx_bouncing_ball_ = -1;
  int idx_grayscale_demo_ = -1;
  int idx_clear_converted_ = -1;
  int idx_switch_ota_ = -1;
  int idx_invalidate_font_ = -1;

  void clear_converted_();
};

}  // namespace microreader
