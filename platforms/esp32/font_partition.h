#pragma once

// Font partition: write font data to the raw spiffs partition and memory-map
// it for zero-RAM, XIP-speed access via esp_partition_mmap().
//
// The spiffs partition (3.375MB) is used as raw storage — NOT as a SPIFFS
// filesystem. We write the MBF font file directly at offset 0 and mmap it.
//
// Layout in partition:
//   [4 bytes]  "FONT" magic
//   [4 bytes]  data size (LE)
//   [N bytes]  MBF font data
//
// After mmap, the returned pointer is the start of the MBF data (after the
// 8-byte header), directly usable by BitmapFont::init().

#include <cstdint>
#include <cstring>

#include "esp_log.h"
#include "esp_partition.h"

static constexpr const char* kFontPartTag = "font_part";
static constexpr uint8_t kFontPartMagic[4] = {'F', 'O', 'N', 'T'};
static constexpr size_t kFontPartHeaderSize = 8;  // 4 magic + 4 size

struct FontPartition {
  const uint8_t* data = nullptr;  // mmapped pointer to MBF data
  size_t size = 0;                // MBF data size in bytes

  // Find the spiffs partition.
  static const esp_partition_t* find() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
  }

  // Write font data to the partition. Erases first, then writes header + data.
  // Returns true on success.
  static bool write(const uint8_t* font_data, size_t font_size) {
    const esp_partition_t* part = find();
    if (!part) {
      ESP_LOGE(kFontPartTag, "spiffs partition not found");
      return false;
    }
    size_t total = kFontPartHeaderSize + font_size;
    if (total > part->size) {
      ESP_LOGE(kFontPartTag, "font too large: %u > partition %lu", (unsigned)total, (unsigned long)part->size);
      return false;
    }

    // Erase the sectors we need (rounded up to 4KB).
    size_t erase_size = (total + 0xFFF) & ~0xFFF;
    esp_err_t err = esp_partition_erase_range(part, 0, erase_size);
    if (err != ESP_OK) {
      ESP_LOGE(kFontPartTag, "erase failed: %s", esp_err_to_name(err));
      return false;
    }

    // Write header: magic + size.
    uint8_t header[kFontPartHeaderSize];
    memcpy(header, kFontPartMagic, 4);
    header[4] = font_size & 0xFF;
    header[5] = (font_size >> 8) & 0xFF;
    header[6] = (font_size >> 16) & 0xFF;
    header[7] = (font_size >> 24) & 0xFF;
    err = esp_partition_write(part, 0, header, kFontPartHeaderSize);
    if (err != ESP_OK) {
      ESP_LOGE(kFontPartTag, "header write failed: %s", esp_err_to_name(err));
      return false;
    }

    // Write font data in 4KB chunks.
    size_t offset = kFontPartHeaderSize;
    size_t remaining = font_size;
    const uint8_t* src = font_data;
    while (remaining > 0) {
      size_t chunk = remaining > 4096 ? 4096 : remaining;
      err = esp_partition_write(part, offset, src, chunk);
      if (err != ESP_OK) {
        ESP_LOGE(kFontPartTag, "data write failed at offset %u: %s", (unsigned)offset, esp_err_to_name(err));
        return false;
      }
      offset += chunk;
      src += chunk;
      remaining -= chunk;
    }

    ESP_LOGI(kFontPartTag, "wrote %u bytes to spiffs partition", (unsigned)font_size);
    return true;
  }

  // Memory-map the font from the partition. Returns true if a valid font
  // was found and mapped. After success, data/size are set.
  bool mmap() {
    const esp_partition_t* part = find();
    if (!part) {
      ESP_LOGW(kFontPartTag, "spiffs partition not found");
      return false;
    }

    // Read header to check magic and get size.
    uint8_t header[kFontPartHeaderSize];
    if (esp_partition_read(part, 0, header, kFontPartHeaderSize) != ESP_OK) {
      ESP_LOGW(kFontPartTag, "failed to read header");
      return false;
    }
    if (memcmp(header, kFontPartMagic, 4) != 0) {
      ESP_LOGI(kFontPartTag, "no font in partition (magic mismatch)");
      return false;
    }
    uint32_t font_size = header[4] | (header[5] << 8) | (header[6] << 16) | (header[7] << 24);
    if (font_size == 0 || kFontPartHeaderSize + font_size > part->size) {
      ESP_LOGW(kFontPartTag, "invalid font size: %lu", (unsigned long)font_size);
      return false;
    }

    // Memory-map the font data region.
    esp_partition_mmap_handle_t handle;
    const void* mapped = nullptr;
    esp_err_t err = esp_partition_mmap(part, kFontPartHeaderSize, font_size, ESP_PARTITION_MMAP_DATA, &mapped, &handle);
    if (err != ESP_OK) {
      ESP_LOGE(kFontPartTag, "mmap failed: %s", esp_err_to_name(err));
      return false;
    }

    data = static_cast<const uint8_t*>(mapped);
    size = font_size;
    ESP_LOGI(kFontPartTag, "mmapped font: %lu bytes at %p", (unsigned long)font_size, mapped);
    return true;
  }
};
