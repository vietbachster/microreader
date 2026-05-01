#pragma once

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

class SettingsScreen final : public ListMenuScreen {
 public:
  SettingsScreen() = default;

  void set_data_dir(const char* dir) {
    data_dir_ = dir;
  }

  const char* name() const override {
    return "Settings";
  }

  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    return ListMenuScreen::update(buttons, buf, runtime);
  }

 protected:
  void on_start() override;
  bool on_select(int index) override;

 private:
  const char* data_dir_ = nullptr;

  // Item indices (assigned during on_start).
  int idx_bouncing_ball_ = -1;
  int idx_grayscale_demo_ = -1;
  int idx_clear_converted_ = -1;
  int idx_switch_ota_ = -1;
  int idx_invalidate_font_ = -1;
  int idx_spiffs_ = -1;
  DrawBuffer* buf_ = nullptr;

  void clear_converted_();
};

}  // namespace microreader
