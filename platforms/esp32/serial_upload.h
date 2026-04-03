#pragma once
// Receive a file over the USB-serial-JTAG link and write it to the SD card.
//
// Protocol (binary, little-endian):
//   TX → device:
//     4 bytes  magic   0x45505542 ("EPUB")
//     2 bytes  name_len
//     N bytes  filename  (UTF-8, no leading /)
//     4 bytes  file_size
//     S bytes  payload
//     4 bytes  CRC-32   (of payload only)
//
//   device → TX:
//     "READY\n"   after parsing header
//     "OK\n"      on success
//     "ERR:...\n" on failure
//
// Usage from the host side (Python helper in tools/upload_epub.py).

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"

static const char* kUpTag = "upload";

// Minimal CRC-32 (same polynomial as zlib).
static uint32_t crc32_update(uint32_t crc, const uint8_t* buf, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= buf[i];
    for (int j = 0; j < 8; ++j)
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

// Blocking read from USB-serial-JTAG. Returns bytes actually read.
static size_t serial_read(uint8_t* buf, size_t len, int timeout_ms) {
  size_t total = 0;
  while (total < len) {
    int n = usb_serial_jtag_read_bytes(buf + total, len - total, pdMS_TO_TICKS(timeout_ms));
    if (n <= 0)
      break;
    total += n;
  }
  return total;
}

static void serial_write(const char* msg) {
  usb_serial_jtag_write_bytes((const uint8_t*)msg, strlen(msg), pdMS_TO_TICKS(1000));
}

// Try to receive one file upload. Non-blocking peek first — returns false
// immediately if no magic byte is waiting.
inline bool serial_upload_poll() {
  // Peek for magic: 0x45505542 ("EPUB")
  uint8_t magic[4];
  int n = usb_serial_jtag_read_bytes(magic, 1, 0);  // non-blocking
  if (n <= 0)
    return false;

  // Got first byte — now read remaining 3 with timeout.
  if (serial_read(magic + 1, 3, 2000) != 3) {
    ESP_LOGW(kUpTag, "incomplete magic");
    return false;
  }

  if (memcmp(magic, "EPUB", 4) != 0) {
    ESP_LOGW(kUpTag, "bad magic: %02x %02x %02x %02x", magic[0], magic[1], magic[2], magic[3]);
    return false;
  }

  // Read filename length (2 bytes LE).
  uint8_t hdr[2];
  if (serial_read(hdr, 2, 2000) != 2) {
    serial_write("ERR:header\n");
    return false;
  }
  uint16_t name_len = hdr[0] | (hdr[1] << 8);
  if (name_len == 0 || name_len > 200) {
    serial_write("ERR:name_len\n");
    return false;
  }

  // Read filename.
  char name[204];
  if (serial_read((uint8_t*)name, name_len, 2000) != name_len) {
    serial_write("ERR:name\n");
    return false;
  }
  name[name_len] = '\0';

  // Read file size (4 bytes LE).
  uint8_t sz_buf[4];
  if (serial_read(sz_buf, 4, 2000) != 4) {
    serial_write("ERR:size\n");
    return false;
  }
  uint32_t file_size = sz_buf[0] | (sz_buf[1] << 8) | (sz_buf[2] << 16) | (sz_buf[3] << 24);
  ESP_LOGI(kUpTag, "receiving '%s' (%u bytes)", name, (unsigned)file_size);

  // Build full path: /sdcard/books/<name>
  char path[256];
  snprintf(path, sizeof(path), "/sdcard/books/%s", name);

  // Ensure /sdcard/books/ exists.
  mkdir("/sdcard/books", 0775);

  FILE* f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(kUpTag, "fopen failed: %s", path);
    serial_write("ERR:fopen\n");
    return false;
  }

  serial_write("READY\n");

  // Receive payload in chunks.
  uint32_t crc = 0;
  uint32_t remaining = file_size;
  uint8_t chunk[1024];
  while (remaining > 0) {
    size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    size_t got = serial_read(chunk, want, 5000);
    if (got == 0) {
      ESP_LOGE(kUpTag, "timeout, %u bytes remaining", (unsigned)remaining);
      fclose(f);
      remove(path);
      serial_write("ERR:timeout\n");
      return false;
    }
    fwrite(chunk, 1, got, f);
    crc = crc32_update(crc, chunk, got);
    remaining -= got;
  }
  fclose(f);

  // Read and verify CRC.
  uint8_t crc_buf[4];
  if (serial_read(crc_buf, 4, 2000) != 4) {
    remove(path);
    serial_write("ERR:crc_missing\n");
    return false;
  }
  uint32_t expected = crc_buf[0] | (crc_buf[1] << 8) | (crc_buf[2] << 16) | (crc_buf[3] << 24);
  if (crc != expected) {
    ESP_LOGE(kUpTag, "CRC mismatch: got 0x%08x, expected 0x%08x", (unsigned)crc, (unsigned)expected);
    remove(path);
    serial_write("ERR:crc\n");
    return false;
  }

  ESP_LOGI(kUpTag, "saved %s (%u bytes, CRC OK)", path, (unsigned)file_size);
  serial_write("OK\n");
  return true;
}
