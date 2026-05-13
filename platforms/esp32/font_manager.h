#pragma once

#include <cstring>

#include "esp_log.h"
#include "font_partition.h"
#include "microreader/Application.h"
#include "microreader/FontManager.h"

// ESP32 font manager: extends the core FontManager with spiffs partition
// provisioning.  Declare as `static FontManager font_mgr(app)` in app_main
// to keep objects in BSS (not on the stack).
class FontManager : public microreader::FontManager {
 public:
  explicit FontManager(microreader::Application& app) : app_(app) {}

  // Call once after epd.begin().  Mmaps the spiffs font partition and loads
  // fonts into the BitmapFont slots.  If already provisioned, advertises the
  // font to the app immediately.  Otherwise, ensure_ready() handles it lazily
  // when the user first opens a book.
  void init() {
    extern const uint8_t _binary_bookerly_bin_start[];
    extern const uint8_t _binary_bookerly_bin_end[];
    extern const uint8_t _binary_alegreya_bin_start[];
    extern const uint8_t _binary_alegreya_bin_end[];

    const std::string& current = app_.custom_font_path();
    if (current == "Alegreya") {
      bundle_data_ = _binary_alegreya_bin_start;
      bundle_size_ = static_cast<size_t>(_binary_alegreya_bin_end - _binary_alegreya_bin_start);
    } else {
      bundle_data_ = _binary_bookerly_bin_start;
      bundle_size_ = static_cast<size_t>(_binary_bookerly_bin_end - _binary_bookerly_bin_start);
    }

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
        if (FontPartition::needs_provisioning(bundle_data_, bundle_size_)) {
          ESP_LOGI("font", "font needs provisioning � will install before app start");
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

    extern const uint8_t _binary_bookerly_bin_start[];
    extern const uint8_t _binary_bookerly_bin_end[];
    extern const uint8_t _binary_alegreya_bin_start[];
    extern const uint8_t _binary_alegreya_bin_end[];

    const uint8_t* target_bundle_data;
    size_t target_bundle_size;
    if (custom_font == "Alegreya") {
      target_bundle_data = _binary_alegreya_bin_start;
      target_bundle_size = static_cast<size_t>(_binary_alegreya_bin_end - _binary_alegreya_bin_start);
    } else {
      target_bundle_data = _binary_bookerly_bin_start;
      target_bundle_size = static_cast<size_t>(_binary_bookerly_bin_end - _binary_bookerly_bin_start);
    }

    bool is_embedded = (custom_font == "" || custom_font == "Bookerly" || custom_font == "Alegreya");

    if (custom_font == installed_font &&
        (!is_embedded || !FontPartition::needs_provisioning(target_bundle_data, target_bundle_size))) {
      // If we skipped setting it in init() due to CRC checks, set it now.
      if (font_set_.valid()) {
        app_.set_reader_font(font_set());
      }
      return;
    }

    buf.sync_bw_ram();
    buf.show_loading("Installing fonts...", 0);

    if (is_embedded) {
      ESP_LOGI("font", "Provisioning font from firmware (first launch or firmware update)...");
      if (FontPartition::provision_embedded(target_bundle_data, target_bundle_size, buf.scratch_buf1(),
                                            microreader::DrawBuffer::kBufSize, buf.scratch_buf2(),
                                            microreader::DrawBuffer::kBufSize,
                                            [&buf](int pct) { buf.show_loading("Installing fonts...", pct); })) {
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
  const uint8_t* bundle_data_ = nullptr;
  size_t bundle_size_ = 0;
};
