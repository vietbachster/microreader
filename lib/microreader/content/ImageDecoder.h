#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ZipReader.h"

namespace microreader {

// Decoded image: 1-bit packed bitmap, MSB first, row-major.
struct DecodedImage {
  uint16_t width = 0;
  uint16_t height = 0;
  std::vector<uint8_t> data;  // packed 1-bit, stride = (width+7)/8 bytes per row

  size_t stride() const {
    return (width + 7) / 8;
  }
  size_t data_size() const {
    return stride() * height;
  }

  // Get pixel at (x, y): 0 = black, 1 = white
  bool pixel(int x, int y) const {
    size_t byte_idx = y * stride() + x / 8;
    uint8_t bit = 7 - (x % 8);
    return (data[byte_idx] >> bit) & 1;
  }
};

enum class ImageFormat {
  Unknown,
  Jpeg,
  Png,
};

// Errors from image operations.
enum class ImageError {
  Ok = 0,
  UnsupportedFormat,
  InvalidData,
  ReadError,
  TooLarge,
};

// Guess format from filename extension.
ImageFormat guess_format(const char* filename);

// Guess format from magic bytes (first 2+ bytes of data).
ImageFormat guess_format_from_magic(const uint8_t* data, size_t size);

// Read image dimensions from raw data without full decoding.
// Works for JPEG (scans for SOF marker) and PNG (reads IHDR).
ImageError read_image_size(const uint8_t* data, size_t size, uint16_t& width, uint16_t& height);

// Compute aspect-ratio-preserving scaled dimensions.
// Returns dimensions that fit within max_w × max_h.
void scaled_size(uint16_t raw_w, uint16_t raw_h, uint16_t max_w, uint16_t max_h, uint16_t& out_w, uint16_t& out_h);

#ifndef MICROREADER_NO_IMAGES
// Decode an image to 1-bit dithered bitmap.
// Input: raw image data (JPEG or PNG).
// Output: DecodedImage with packed 1-bit pixels.
// max_w/max_h: maximum output dimensions (image is scaled to fit).
ImageError decode_image(const uint8_t* data, size_t size, uint16_t max_w, uint16_t max_h, DecodedImage& out);

// Floyd-Steinberg dither a grayscale buffer to 1-bit packed bitmap.
// grayscale: width*height bytes, 0=black, 255=white.
// out: pre-allocated packed bitmap, (width+7)/8 * height bytes.
void floyd_steinberg_dither(const uint8_t* grayscale, int width, int height, uint8_t* out);
#endif

}  // namespace microreader
