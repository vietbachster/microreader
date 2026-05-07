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

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    ListMenuScreen::update(buttons, buf, runtime);
  }

 protected:
  void on_start() override;
  void on_select(int index) override;

 private:
  const char* data_dir_ = nullptr;

  // Item indices (assigned during on_start).
  int idx_bouncing_ball_ = -1;
  int idx_grayscale_demo_ = -1;
  int idx_clear_cache_ = -1;
  int idx_rebuild_index_ = -1;
  int idx_list_format_ = -1;
  int idx_switch_ota_ = -1;
  int idx_invalidate_font_ = -1;
  int idx_spiffs_ = -1;
  int idx_invert_menu_ = -1;
  int idx_invert_side_ = -1;
  int idx_rotate_display_ = -1;
  DrawBuffer* buf_ = nullptr;

  void clear_cache_();
};

}  // namespace microreader
