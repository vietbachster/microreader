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
    extern const uint8_t _binary_font_bundle_bin_start[];
    extern const uint8_t _binary_font_bundle_bin_end[];
    bundle_data_ = _binary_font_bundle_bin_start;
    bundle_size_ = static_cast<size_t>(_binary_font_bundle_bin_end - _binary_font_bundle_bin_start);

    if (font_part_.mmap()) {
      load_fonts_();
      if (font_set_.valid()) {
        static const char* names[] = {"Small", "Normal", "Large", "XLarge", "XXLarge"};
        for (int i = 0; i < microreader::kMaxFonts; i++) {
          if (prop_fonts_[i].valid())
            ESP_LOGI("font", "%s: %u glyphs, height=%u baseline=%u", names[i], (unsigned)prop_fonts_[i].num_glyphs(),
                     (unsigned)prop_fonts_[i].glyph_height(), (unsigned)prop_fonts_[i].baseline());
        }
        if (!FontPartition::needs_provisioning(bundle_data_, bundle_size_)) {
          app_.set_reader_font(font_set());
        } else {
          ESP_LOGI("font", "font needs provisioning — will install before app start");
        }
      } else {
        ESP_LOGW("font", "no valid Normal font found");
      }
    }
  }

  // Called by ReaderScreen before opening a book (IFontEnsurer interface).
  // No-op if fonts are already provisioned.
  void ensure_ready(microreader::DrawBuffer& buf) override {
    if (!FontPartition::needs_provisioning(bundle_data_, bundle_size_))
      return;
    ESP_LOGI("font", "Provisioning font from firmware (first launch or firmware update)...");
    buf.sync_bw_ram();
    buf.show_loading("Installing fonts...", 0);
    if (FontPartition::provision_embedded(
            bundle_data_, bundle_size_, buf.scratch_buf1(), microreader::DrawBuffer::kBufSize, buf.scratch_buf2(),
            microreader::DrawBuffer::kBufSize, [&buf](int pct) { buf.show_loading("Installing fonts...", pct); })) {
      buf.reset_after_scratch();
      if (font_part_.mmap()) {
        load_fonts_();
        app_.set_reader_font(font_set());
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
    if (num > microreader::kMaxFonts)
      num = microreader::kMaxFonts;

    char font_name[33] = {};
    memcpy(font_name, d + 8, 32);
    font_name[32] = '\0';
    ESP_LOGI("font", "Bundle font: \"%s\" (v%u, %u sizes)", font_name, d[5], num);

    constexpr size_t kSizeTableOff = 8 + 32;
    uint32_t sizes[microreader::kMaxFonts] = {};
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
