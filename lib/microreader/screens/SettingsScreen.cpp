#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

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

  idx_grayscale_demo_ = count();
  add_item(grayscale_demo_.name());

  if (data_dir_) {
    idx_clear_converted_ = count();
    add_item("Clear Converted");
  }

#ifdef ESP_PLATFORM
  idx_switch_ota_ = count();
  add_item("Switch OTA");

  if (invalidate_font_fn_) {
    idx_invalidate_font_ = count();
    add_item("Invalidate Font");
  }
#endif
}

bool SettingsScreen::on_select(int index) {
  if (index == idx_bouncing_ball_) {
    chosen_ = &bouncing_ball_;
    return false;
  }
  if (index == idx_grayscale_demo_) {
    chosen_ = &grayscale_demo_;
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
  if (index == idx_invalidate_font_) {
    if (invalidate_font_fn_)
      invalidate_font_fn_();
    return true;  // stay on settings screen
  }
#endif
  return true;
}

void SettingsScreen::clear_converted_() {
  if (!data_dir_)
    return;
#ifdef ESP_PLATFORM
  char cache_dir[768];
  std::snprintf(cache_dir, sizeof(cache_dir), "%s/cache", data_dir_);
  DIR* d = opendir(cache_dir);
  if (!d) {
    mkdir(cache_dir, 0775);
    return;
  }
  struct dirent* ent;
  char subdir_path[768];
  while ((ent = readdir(d)) != nullptr) {
    if (ent->d_name[0] == '.')
      continue;
    std::snprintf(subdir_path, sizeof(subdir_path), "%s/%s", cache_dir, ent->d_name);
    // Remove all files inside the per-book subdir.
    DIR* sd = opendir(subdir_path);
    if (sd) {
      struct dirent* sf;
      char file_path[768];
      while ((sf = readdir(sd)) != nullptr) {
        if (sf->d_name[0] == '.')
          continue;
        std::snprintf(file_path, sizeof(file_path), "%s/%s", subdir_path, sf->d_name);
        std::remove(file_path);
      }
      closedir(sd);
    }
    rmdir(subdir_path);
  }
  closedir(d);
  rmdir(cache_dir);
  mkdir(cache_dir, 0775);
#else
  namespace fs = std::filesystem;
  try {
    std::string cache_path = std::string(data_dir_) + "/cache";
    fs::remove_all(cache_path);
    fs::create_directories(cache_path);
  } catch (...) {}
#endif
}

}  // namespace microreader
