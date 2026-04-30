#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "miniz.h"
#else
#include <filesystem>
#endif

#include "spiffs_image_data.h"

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

  idx_erase_spiffs_ = count();
  add_item("Erase SPIFFS");
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
  if (index == idx_erase_spiffs_) {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");

    if (part) {
      if (buf_)
        buf_->show_loading("Erasing...", 0);

      static constexpr size_t kDictSize = TINFL_LZ_DICT_SIZE;
      static constexpr size_t kDecompSize = 11264;
      static constexpr size_t kWriteSize = 4096;
      uint8_t* work = static_cast<uint8_t*>(malloc(kDecompSize + kDictSize + kWriteSize));
      if (work) {
        esp_partition_erase_range(part, 0, part->size);

        if (buf_)
          buf_->show_loading("Writing...", 50);

        auto* decomp = reinterpret_cast<tinfl_decompressor*>(work);
        uint8_t* dict = work + kDecompSize;
        uint8_t* wbuf = work + kDecompSize + kDictSize;
        tinfl_init(decomp);
        const uint8_t* in_ptr = kSpiffsImage;
        size_t in_left = kSpiffsImageSize;
        size_t flash_offset = 0;
        size_t dict_ofs = 0;
        tinfl_status status = TINFL_STATUS_HAS_MORE_OUTPUT;
        while (status == TINFL_STATUS_HAS_MORE_OUTPUT || status == TINFL_STATUS_NEEDS_MORE_INPUT) {
          size_t in_sz = in_left;
          size_t out_sz = kDictSize - dict_ofs;
          mz_uint32 flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
          if (in_left > in_sz)
            flags |= TINFL_FLAG_HAS_MORE_INPUT;
          status = tinfl_decompress(decomp, in_ptr, &in_sz, dict, dict + dict_ofs, &out_sz, flags);
          in_ptr += in_sz;
          in_left -= in_sz;
          size_t produced = out_sz;
          size_t write_ofs = 0;
          while (write_ofs < produced) {
            size_t chunk = produced - write_ofs;
            if (chunk > kWriteSize)
              chunk = kWriteSize;
            memcpy(wbuf, dict + dict_ofs + write_ofs, chunk);
            esp_partition_write(part, flash_offset, wbuf, chunk);
            flash_offset += chunk;
            write_ofs += chunk;
          }
          dict_ofs = (dict_ofs + produced) & (kDictSize - 1);
          if (status <= TINFL_STATUS_DONE)
            break;
        }
        free(work);

        if (buf_)
          buf_->show_loading("Done!", 100);
      }
    }
    esp_restart();
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
