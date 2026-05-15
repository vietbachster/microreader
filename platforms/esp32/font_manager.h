#pragma once

#include <cstring>

#include "asset_blob.h"
#include "esp_log.h"
#include "font_partition.h"
#include "microreader/Application.h"
#include "microreader/FontManager.h"

// ESP32 font manager: extends the core FontManager with spiffs partition
// provisioning.  Declare as `static FontManager font_mgr(app)` in app_main
// to keep objects in BSS (not on the stack).
//
// Font bundles live in the appended asset blob (see asset_blob.h) so they
// don't consume DROM at boot.  We mmap on demand only during provisioning
// (~5 s, once per firmware update), then unmap so the MMU pages are free.
class FontManager : public microreader::FontManager {
 public:
  explicit FontManager(microreader::Application& app) : app_(app) {}

  // Returns the asset name for the currently selected built-in font, or
  // nullptr if the user selected a custom (SD-card) font.
  static const char* embedded_asset_for(const std::string& font_name) {
    if (font_name == "Alegreya")
      return "alegreya.bin";
    if (font_name == "" || font_name == "Bookerly")
      return "bookerly.bin";
    return nullptr;
  }

  void init() {
    if (font_part_.mmap()) {
      load_fonts_();
      if (font_set_.valid()) {
        for (int i = 0; i < microreader::kMaxFontSizes; i++) {
          if (prop_fonts_[i].valid()) {
            ESP_LOGI("font", "Size %d: %u glyphs, height=%u baseline=%u", i, (unsigned)prop_fonts_[i].num_glyphs(),
                     (unsigned)prop_fonts_[i].glyph_height(), (unsigned)prop_fonts_[i].baseline());
          }
        }
        app_.set_reader_font(font_set());
        const char* target_asset = embedded_asset_for(app_.custom_font_path());
        if (target_asset) {
          uint32_t expected_crc = asset_blob::g_assets.crc(target_asset);
          if (FontPartition::needs_provisioning(expected_crc)) {
            ESP_LOGI("font", "font needs provisioning \u2014 will install before app start");
          }
        }
      } else {
        ESP_LOGW("font", "no valid Normal font found");
      }
    }
  }

  // Called by ReaderScreen before opening a book (IFontEnsurer interface).
  // No-op if fonts are already provisioned.
  void ensure_ready(microreader::DrawBuffer& buf) override {
    const std::string& custom_font = app_.custom_font_path();
    const std::string& installed_font = app_.installed_font_path();

    const char* target_asset = embedded_asset_for(custom_font);
    bool is_embedded = (target_asset != nullptr);
    uint32_t target_crc = is_embedded ? asset_blob::g_assets.crc(target_asset) : 0;

    if (custom_font == installed_font && (!is_embedded || !FontPartition::needs_provisioning(target_crc))) {
      // If we skipped setting it in init() due to CRC checks, set it now.
      if (font_set_.valid()) {
        app_.set_reader_font(font_set());
      }
      return;
    }

    buf.sync_bw_ram();
    buf.show_loading("Installing fonts...", 0);

    if (is_embedded) {
      ESP_LOGI("font", "Provisioning font \"%s\" from firmware...", target_asset);

      size_t mapped_size = 0;
      esp_partition_mmap_handle_t mmap_h = 0;
      const uint8_t* data = static_cast<const uint8_t*>(asset_blob::g_assets.map(target_asset, mapped_size, mmap_h));
      if (!data) {
        ESP_LOGE("font", "failed to map asset %s", target_asset);
        return;
      }
      bool ok = FontPartition::provision_embedded(
          data, mapped_size, target_crc, buf.scratch_buf1(), microreader::DrawBuffer::kBufSize, buf.scratch_buf2(),
          microreader::DrawBuffer::kBufSize, [&buf](int pct) { buf.show_loading("Installing fonts...", pct); });
      asset_blob::g_assets.unmap(mmap_h);

      if (ok) {
        app_.set_installed_font_path(custom_font);
        buf.reset_after_scratch();
        if (font_part_.mmap()) {
          load_fonts_();
          app_.set_reader_font(font_set());
        }
      }
    } else {
      ESP_LOGI("font", "Provisioning font from SD card: %s", custom_font.c_str());
      if (FontPartition::provision_uncompressed_file(
              custom_font.c_str(), buf.scratch_buf2(), microreader::DrawBuffer::kBufSize,
              [&buf](int pct) { buf.show_loading("Installing fonts...", pct); })) {
        app_.set_installed_font_path(custom_font);
        buf.reset_after_scratch();
        if (font_part_.mmap()) {
          load_fonts_();
          app_.set_reader_font(font_set());
        }
      }
    }
  }

  // Call in the main loop when g_font_uploaded is true (serial upload).
  void on_serial_upload() {
    if (font_part_.mmap()) {
      load_fonts_();
      if (font_set_.valid()) {
        ESP_LOGI("font", "re-loaded fonts after upload");
        app_.set_reader_font(font_set());
      }
    }
  }

 private:
  void load_fonts_() {
    for (auto& f : prop_fonts_)
      f = microreader::BitmapFont();
    font_set_ = microreader::BitmapFontSet();
    num_fonts_ = 0;

    const uint8_t* d = font_part_.data;
    size_t sz = font_part_.size;

    if (sz < 40 || memcmp(d, "FNTS", 4) != 0 || d[5] < 1) {
      ESP_LOGE("font", "Invalid font partition (expected FNTS v1 bundle)");
      return;
    }

    // FNTS v1: [FNTS:4][num:1][version:1][res:2][name:32][num×size:4][data...]
    uint8_t num = d[4];
    if (num > microreader::kMaxFontSizes)
      num = microreader::kMaxFontSizes;

    char font_name[33] = {};
    memcpy(font_name, d + 8, 32);
    font_name[32] = '\0';
    ESP_LOGI("font", "Bundle font: \"%s\" (v%u, %u sizes)", font_name, d[5], num);

    constexpr size_t kSizeTableOff = 8 + 32;
    uint32_t sizes[microreader::kMaxFontSizes] = {};
    for (int i = 0; i < num; i++) {
      const uint8_t* p = d + kSizeTableOff + i * 4;
      sizes[i] = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }
    size_t off = kSizeTableOff + static_cast<size_t>(num) * 4;
    for (int i = 0; i < num; i++) {
      if (off + sizes[i] > sz)
        break;
      load_font(d + off, sizes[i]);
      off += sizes[i];
    }
  }

  microreader::Application& app_;
  FontPartition font_part_;
};
