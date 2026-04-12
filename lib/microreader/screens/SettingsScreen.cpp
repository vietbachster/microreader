#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <dirent.h>

#include "esp_ota_ops.h"
#include "esp_system.h"
#else
#include <filesystem>
#endif

namespace microreader {

void SettingsScreen::on_start() {
  title_ = "Settings";

  idx_bouncing_ball_ = count();
  add_item(bouncing_ball_.name());

  if (books_dir_) {
    idx_clear_converted_ = count();
    add_item("Clear Converted");
  }

#ifdef ESP_PLATFORM
  idx_switch_ota_ = count();
  add_item("Switch OTA");
#endif
}

bool SettingsScreen::on_select(int index) {
  if (index == idx_bouncing_ball_) {
    chosen_ = &bouncing_ball_;
    return false;
  }
  if (index == idx_clear_converted_) {
    clear_converted_();
    return true;  // stay on screen
  }
#ifdef ESP_PLATFORM
  if (index == idx_switch_ota_) {
    auto running = esp_ota_get_running_partition();
    auto next = esp_ota_get_next_update_partition(running);
    esp_ota_set_boot_partition(next);
    esp_restart();
  }
#endif
  return true;
}

void SettingsScreen::clear_converted_() {
  if (!books_dir_)
    return;
#ifdef ESP_PLATFORM
  DIR* d = opendir(books_dir_);
  if (!d)
    return;
  struct dirent* ent;
  char path[300];
  while ((ent = readdir(d)) != nullptr) {
    size_t len = std::strlen(ent->d_name);
    if (len > 4 && std::strcmp(ent->d_name + len - 4, ".mrb") == 0) {
      std::snprintf(path, sizeof(path), "%s/%s", books_dir_, ent->d_name);
      std::remove(path);
    }
  }
  closedir(d);
#else
  namespace fs = std::filesystem;
  try {
    for (const auto& entry : fs::directory_iterator(books_dir_)) {
      if (entry.is_regular_file() && entry.path().extension() == ".mrb")
        fs::remove(entry.path());
    }
  } catch (...) {}
#endif
}

}  // namespace microreader
