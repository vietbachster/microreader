#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../Application.h"
#include "../content/BookIndex.h"
#include "../version.h"

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

#include "../resources/spiffs_image_data.h"

namespace microreader {

void SettingsScreen::on_start() {
  title_ = "Settings";
  subtitle_ = MICROREADER_VERSION;

  idx_list_format_ = count();
  std::string format_label = "Book List: Title & Author";
  if (app_ && app_->main_menu()) {
    auto fmt = app_->main_menu()->list_format();
    if (fmt == BookListFormat::TitleOnly)
      format_label = "Book List: Title";
    else if (fmt == BookListFormat::Filename)
      format_label = "Book List: Filename";
  }
  add_item(format_label);

  add_separator();

#ifdef MICROREADER_ENABLE_DEMOS
  idx_bouncing_ball_ = count();
  add_item("Bouncing Ball");

  idx_grayscale_demo_ = count();
  add_item("Grayscale Demo");

  add_separator();
#endif

  if (data_dir_) {
    idx_clear_converted_ = count();
    add_item("Clear Converted");

    idx_rebuild_index_ = count();
    add_item("Rebuild Book Index");
  }

#ifdef ESP_PLATFORM
  if (app_ && app_->has_invalidate_font_fn()) {
    idx_invalidate_font_ = count();
    add_item("Invalidate Font");
  }

  idx_spiffs_ = count();
  add_item("Rebuild SPIFFS");

  idx_switch_ota_ = count();
  add_item("Switch OTA");
#endif
}

void SettingsScreen::on_select(int index) {
#ifdef MICROREADER_ENABLE_DEMOS
  if (index == idx_bouncing_ball_) {
    app_->push_screen(ScreenId::BouncingBall);
    return;
  }
  if (index == idx_grayscale_demo_) {
    app_->push_screen(ScreenId::GrayscaleDemo);
    return;
  }
#endif
  if (index == idx_clear_converted_) {
    clear_converted_();
    return;  // stay on screen
  }
  if (index == idx_rebuild_index_) {
    if (app_->main_menu() && app_->main_menu()->has_books_dir()) {
      std::string root_dir = app_->main_menu()->books_dir();
      std::string index_path = root_dir + "/book_index.dat";

      buf_->sync_bw_ram();
      BookIndex::instance().build_index(root_dir, *buf_);
      BookIndex::instance().save(index_path);
      buf_->reset_after_scratch(true);
      app_->pop_screen();  // go back to main menu
    }
    return;
  }
  if (index == idx_list_format_) {
    if (app_->main_menu()) {
      auto fmt = app_->main_menu()->list_format();
      std::string new_label;
      if (fmt == BookListFormat::TitleAndAuthor) {
        fmt = BookListFormat::TitleOnly;
        new_label = "Book List: Title";
      } else if (fmt == BookListFormat::TitleOnly) {
        fmt = BookListFormat::Filename;
        new_label = "Book List: Filename";
      } else {
        fmt = BookListFormat::TitleAndAuthor;
        new_label = "Book List: Title & Author";
      }
      app_->main_menu()->set_list_format(fmt);
      set_item_label(idx_list_format_, new_label);
    }
    return;
  }
#ifdef ESP_PLATFORM
  if (index == idx_switch_ota_) {
    auto running = esp_ota_get_running_partition();
    auto next = esp_ota_get_next_update_partition(running);
    esp_ota_set_boot_partition(next);
    esp_restart();
  }
  if (index == idx_invalidate_font_) {
    if (app_)
      app_->invalidate_font();
    return;  // stay on settings screen
  }
  if (index == idx_spiffs_) {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");

    if (part) {
      buf_->sync_bw_ram();
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

        buf_->show_loading("Done!", 100);
      }
    }
    esp_restart();
  }
#endif
  return;
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
