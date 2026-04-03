#pragma once

// Receives frames over USB Serial/JTAG:
//   - LUT frames (magic 0xDEADBEEF) → applied to the EPD
//   - EPUB uploads (magic "EPUB")    → written to /sdcard/books/
//
// LUT frame format (little-endian):
//   [0xDE 0xAD 0xBE 0xEF]  magic (4 bytes)
//   [length]                payload length (4 bytes LE)
//   [payload]               LUT bytes (112 bytes)
//   [crc32]                 CRC-32 of payload (4 bytes LE)
//
// EPUB upload format (little-endian):
//   [0x45 0x50 0x55 0x42]  magic "EPUB" (4 bytes)
//   [name_len]             filename length (2 bytes LE)
//   [filename]             UTF-8 filename (N bytes)
//   [file_size]            payload size (4 bytes LE)
//   [payload]              file data (S bytes)
//   [crc32]                CRC-32 of payload (4 bytes LE)
//
// Device responses for EPUB upload:
//   "READY\n"  after parsing header
//   "OK\n"     on success
//   "ERR:...\n" on failure
//
// Call serial_start() once from app_main.
// Poll serial_lut_take(buf) each loop for LUT data.

#include <sys/stat.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_vfs_usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr const char* kLutRxTag = "lut_rx";
static constexpr const char* kUpTag = "upload";
static constexpr uint8_t kLutMagic[4] = {0xDE, 0xAD, 0xBE, 0xEF};
static constexpr uint8_t kEpubMagic[4] = {'E', 'P', 'U', 'B'};
static constexpr uint32_t kMaxPayload = 256;
static constexpr uint32_t kLutSize = 112;

// Shared between the receiver task and the main loop.
static uint8_t g_lut_buf[kLutSize];
static volatile bool g_lut_pending = false;

// Call from the main loop. Returns true (and copies into `out`) when a fresh
// LUT has been received since the last call.
inline bool serial_lut_take(uint8_t* out) {
  if (!g_lut_pending)
    return false;
  memcpy(out, g_lut_buf, kLutSize);
  g_lut_pending = false;
  return true;
}

// Read exactly `n` bytes with a timeout. Returns true on success.
static bool serial_read_exact(uint8_t* buf, size_t n, uint32_t timeout_ms) {
  size_t received = 0;
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (received < n) {
    const TickType_t now = xTaskGetTickCount();
    if ((int32_t)(deadline - now) <= 0)
      return false;
    const int r = usb_serial_jtag_read_bytes(buf + received, n - received, deadline - now);
    if (r > 0)
      received += r;
  }
  return true;
}

static void serial_write(const char* msg) {
  usb_serial_jtag_write_bytes((const uint8_t*)msg, strlen(msg), pdMS_TO_TICKS(1000));
}

// ---------------------------------------------------------------------------
// Handle an incoming LUT frame (after magic has been matched).
// ---------------------------------------------------------------------------
static void handle_lut_frame() {
  uint8_t len_buf[4];
  if (!serial_read_exact(len_buf, 4, 500)) {
    ESP_LOGW(kLutRxTag, "timeout reading length");
    return;
  }
  const uint32_t length =
      (uint32_t)len_buf[0] | ((uint32_t)len_buf[1] << 8) | ((uint32_t)len_buf[2] << 16) | ((uint32_t)len_buf[3] << 24);
  if (length == 0 || length > kMaxPayload) {
    ESP_LOGW(kLutRxTag, "invalid length: %lu", length);
    return;
  }

  uint8_t payload[kMaxPayload];
  if (!serial_read_exact(payload, length, 2000)) {
    ESP_LOGW(kLutRxTag, "timeout reading payload");
    return;
  }

  uint8_t crc_buf[4];
  if (!serial_read_exact(crc_buf, 4, 500)) {
    ESP_LOGW(kLutRxTag, "timeout reading crc");
    return;
  }
  const uint32_t recv_crc =
      (uint32_t)crc_buf[0] | ((uint32_t)crc_buf[1] << 8) | ((uint32_t)crc_buf[2] << 16) | ((uint32_t)crc_buf[3] << 24);
  const uint32_t calc_crc = esp_rom_crc32_le(0, payload, length);

  if (recv_crc != calc_crc) {
    ESP_LOGE(kLutRxTag, "CRC mismatch: recv=0x%08lX calc=0x%08lX", recv_crc, calc_crc);
    return;
  }

  ESP_LOGI(kLutRxTag, "OK: received %lu-byte LUT (CRC 0x%08lX)", length, calc_crc);
  memcpy(g_lut_buf, payload, kLutSize);
  g_lut_pending = true;
}

// ---------------------------------------------------------------------------
// Handle an incoming EPUB upload (after magic has been matched).
// ---------------------------------------------------------------------------
static void handle_epub_upload() {
  // Read filename length (2 bytes LE).
  uint8_t hdr[2];
  if (!serial_read_exact(hdr, 2, 2000)) {
    serial_write("ERR:header\n");
    return;
  }
  uint16_t name_len = hdr[0] | (hdr[1] << 8);
  if (name_len == 0 || name_len > 200) {
    serial_write("ERR:name_len\n");
    return;
  }

  // Read filename.
  char name[204];
  if (!serial_read_exact((uint8_t*)name, name_len, 2000)) {
    serial_write("ERR:name\n");
    return;
  }
  name[name_len] = '\0';

  // Read file size (4 bytes LE).
  uint8_t sz_buf[4];
  if (!serial_read_exact(sz_buf, 4, 2000)) {
    serial_write("ERR:size\n");
    return;
  }
  uint32_t file_size = sz_buf[0] | (sz_buf[1] << 8) | (sz_buf[2] << 16) | (sz_buf[3] << 24);

  // Build path: /sdcard/books/<name>
  char path[256];
  snprintf(path, sizeof(path), "/sdcard/books/%s", name);
  mkdir("/sdcard/books", 0775);

  FILE* f = fopen(path, "wb");
  if (!f) {
    ESP_LOGE(kUpTag, "fopen failed: %s (errno=%d: %s)", path, errno, strerror(errno));
    serial_write("ERR:fopen\n");
    return;
  }

  // Log before READY so the host's readline loop can skip it.
  ESP_LOGI(kUpTag, "receiving '%s' (%lu bytes)", name, (unsigned long)file_size);
  serial_write("READY\n");

  // Receive payload in chunks with flow control.
  // After each chunk is written to SD, send ACK (0x06) so the host
  // knows to send the next chunk.  This prevents RX buffer overflow
  // for large files.
  uint32_t crc = 0;
  uint32_t remaining = file_size;
  uint8_t chunk[2048];
  static constexpr uint8_t kAck = 0x06;
  while (remaining > 0) {
    size_t want = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
    if (!serial_read_exact(chunk, want, 30000)) {
      ESP_LOGE(kUpTag, "timeout, %lu bytes remaining", (unsigned long)remaining);
      fclose(f);
      remove(path);
      serial_write("ERR:timeout\n");
      return;
    }
    fwrite(chunk, 1, want, f);
    crc = esp_rom_crc32_le(crc, chunk, want);
    remaining -= want;
    // Flow-control ACK — tell host to send next chunk.
    usb_serial_jtag_write_bytes(&kAck, 1, pdMS_TO_TICKS(1000));
  }
  fclose(f);

  // Verify CRC.
  uint8_t crc_buf[4];
  if (!serial_read_exact(crc_buf, 4, 2000)) {
    remove(path);
    serial_write("ERR:crc_missing\n");
    return;
  }
  uint32_t expected = crc_buf[0] | (crc_buf[1] << 8) | (crc_buf[2] << 16) | (crc_buf[3] << 24);
  if (crc != expected) {
    ESP_LOGE(kUpTag, "CRC mismatch: got 0x%08lx, expected 0x%08lx", (unsigned long)crc, (unsigned long)expected);
    remove(path);
    serial_write("ERR:crc\n");
    return;
  }

  ESP_LOGI(kUpTag, "saved %s (%lu bytes, CRC OK)", path, (unsigned long)file_size);
  serial_write("OK\n");
}

// ---------------------------------------------------------------------------
// Unified receiver task — scans for both magic sequences byte by byte.
// ---------------------------------------------------------------------------
static void serial_receiver_task(void* /*arg*/) {
  uint8_t lut_pos = 0;   // progress matching kLutMagic
  uint8_t epub_pos = 0;  // progress matching kEpubMagic

  ESP_LOGI(kLutRxTag, "receiver ready (LUT + EPUB upload)");

  while (true) {
    uint8_t byte;
    if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(50)) != 1)
      continue;

    // Match LUT magic.
    if (byte == kLutMagic[lut_pos]) {
      if (++lut_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        handle_lut_frame();
        continue;
      }
    } else {
      lut_pos = (byte == kLutMagic[0]) ? 1 : 0;
    }

    // Match EPUB magic.
    if (byte == kEpubMagic[epub_pos]) {
      if (++epub_pos == 4) {
        lut_pos = 0;
        epub_pos = 0;
        handle_epub_upload();
        continue;
      }
    } else {
      epub_pos = (byte == kEpubMagic[0]) ? 1 : 0;
    }
  }
}

// Call once from app_main before the main loop.
inline void serial_start() {
  usb_serial_jtag_driver_config_t cfg = {
      .tx_buffer_size = 1024,
      .rx_buffer_size = 4096,
  };
  usb_serial_jtag_driver_install(&cfg);
  esp_vfs_dev_usb_serial_jtag_register();

  xTaskCreate(serial_receiver_task, "serial_rx", 8192, nullptr, 3, nullptr);
}
