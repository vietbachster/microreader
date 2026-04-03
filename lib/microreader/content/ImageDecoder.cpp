#include "ImageDecoder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>

// We use stb_image for JPEG/PNG decoding on desktop.
// On embedded (ESP32), this would be replaced with a streaming decoder.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

namespace microreader {

// ---------------------------------------------------------------------------
// Format detection
// ---------------------------------------------------------------------------

ImageFormat guess_format(const char* filename) {
  if (!filename)
    return ImageFormat::Unknown;
  const char* dot = std::strrchr(filename, '.');
  if (!dot)
    return ImageFormat::Unknown;
  ++dot;

  // Case-insensitive comparison
  char ext[8] = {};
  for (int i = 0; i < 7 && dot[i]; ++i) {
    ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(dot[i])));
  }

  if (std::strcmp(ext, "jpg") == 0 || std::strcmp(ext, "jpeg") == 0)
    return ImageFormat::Jpeg;
  if (std::strcmp(ext, "png") == 0)
    return ImageFormat::Png;
  return ImageFormat::Unknown;
}

ImageFormat guess_format_from_magic(const uint8_t* data, size_t size) {
  if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8)
    return ImageFormat::Jpeg;
  if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
    return ImageFormat::Png;
  return ImageFormat::Unknown;
}

// ---------------------------------------------------------------------------
// Size reading (without full decode)
// ---------------------------------------------------------------------------

// Read big-endian u16
static uint16_t be16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

static ImageError read_jpeg_size(const uint8_t* data, size_t size, uint16_t& w, uint16_t& h) {
  // Scan for SOF0/SOF2 marker
  size_t pos = 0;
  if (size < 2 || data[0] != 0xFF || data[1] != 0xD8)
    return ImageError::InvalidData;
  pos = 2;

  while (pos + 4 < size) {
    if (data[pos] != 0xFF) {
      ++pos;
      continue;
    }
    uint8_t marker = data[pos + 1];

    // Skip padding FF bytes
    if (marker == 0xFF) {
      ++pos;
      continue;
    }
    if (marker == 0x00) {
      pos += 2;
      continue;
    }

    // SOF markers: 0xC0-0xCF except 0xC4 (DHT) and 0xCC (DAC)
    bool is_sof = (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xCC);

    if (is_sof) {
      if (pos + 9 >= size)
        return ImageError::InvalidData;
      h = be16(&data[pos + 5]);
      w = be16(&data[pos + 7]);
      return ImageError::Ok;
    }

    // Skip this segment
    if (pos + 3 >= size)
      return ImageError::InvalidData;
    uint16_t seg_len = be16(&data[pos + 2]);
    pos += 2 + seg_len;
  }

  return ImageError::InvalidData;
}

static ImageError read_png_size(const uint8_t* data, size_t size, uint16_t& w, uint16_t& h) {
  // PNG: 8-byte signature + IHDR chunk
  if (size < 24)
    return ImageError::InvalidData;
  static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (std::memcmp(data, png_sig, 8) != 0)
    return ImageError::InvalidData;

  // IHDR starts at offset 8: 4 bytes length + 4 bytes "IHDR" + 4 width + 4 height
  if (std::memcmp(data + 12, "IHDR", 4) != 0)
    return ImageError::InvalidData;
  uint32_t width = be32(&data[16]);
  uint32_t height = be32(&data[20]);

  if (width > 0xFFFF || height > 0xFFFF)
    return ImageError::TooLarge;
  w = static_cast<uint16_t>(width);
  h = static_cast<uint16_t>(height);
  return ImageError::Ok;
}

ImageError read_image_size(const uint8_t* data, size_t size, uint16_t& width, uint16_t& height) {
  auto fmt = guess_format_from_magic(data, size);
  if (fmt == ImageFormat::Jpeg)
    return read_jpeg_size(data, size, width, height);
  if (fmt == ImageFormat::Png)
    return read_png_size(data, size, width, height);
  return ImageError::UnsupportedFormat;
}

// ---------------------------------------------------------------------------
// Scaling
// ---------------------------------------------------------------------------

void scaled_size(uint16_t raw_w, uint16_t raw_h, uint16_t max_w, uint16_t max_h, uint16_t& out_w, uint16_t& out_h) {
  // 0 means no limit
  if (max_w == 0) max_w = raw_w;
  if (max_h == 0) max_h = raw_h;

  if (raw_w <= max_w && raw_h <= max_h) {
    out_w = raw_w;
    out_h = raw_h;
    return;
  }

  // Fit within max_w × max_h preserving aspect ratio
  // Cross-multiply to avoid float: raw_w * max_h vs raw_h * max_w
  if (static_cast<uint32_t>(raw_w) * max_h > static_cast<uint32_t>(raw_h) * max_w) {
    // Width-bound
    out_w = max_w;
    out_h = static_cast<uint16_t>(static_cast<uint32_t>(raw_h) * max_w / raw_w);
  } else {
    // Height-bound
    out_h = max_h;
    out_w = static_cast<uint16_t>(static_cast<uint32_t>(raw_w) * max_h / raw_h);
  }
  if (out_w == 0)
    out_w = 1;
  if (out_h == 0)
    out_h = 1;
}

// ---------------------------------------------------------------------------
// Floyd-Steinberg dithering
// ---------------------------------------------------------------------------

void floyd_steinberg_dither(const uint8_t* grayscale, int width, int height, uint8_t* out) {
  // Work buffer: two rows of int16_t for error diffusion
  size_t row_bytes = static_cast<size_t>(width);
  auto current = std::make_unique<int16_t[]>(row_bytes);
  auto next_row = std::make_unique<int16_t[]>(row_bytes);

  size_t stride = (width + 7) / 8;
  std::memset(out, 0, stride * height);

  // Initialize first row
  for (int x = 0; x < width; ++x) {
    current[x] = grayscale[x];
  }

  for (int y = 0; y < height; ++y) {
    // Initialize next row from source
    if (y + 1 < height) {
      for (int x = 0; x < width; ++x) {
        next_row[x] = grayscale[(y + 1) * width + x];
      }
    }

    for (int x = 0; x < width; ++x) {
      int old_pixel = std::max<int>(0, std::min<int>(255, current[x]));
      int new_pixel = (old_pixel >= 128) ? 255 : 0;
      int error = old_pixel - new_pixel;

      // Set output bit (1 = white, 0 = black)
      if (new_pixel) {
        out[y * stride + x / 8] |= (0x80 >> (x % 8));
      }

      // Distribute error to neighbors
      if (x + 1 < width)
        current[x + 1] += error * 7 / 16;
      if (y + 1 < height) {
        if (x > 0)
          next_row[x - 1] += error * 3 / 16;
        next_row[x] += error * 5 / 16;
        if (x + 1 < width)
          next_row[x + 1] += error * 1 / 16;
      }
    }

    // Swap rows
    std::swap(current, next_row);
  }
}

// ---------------------------------------------------------------------------
// Image decoding via stb_image
// ---------------------------------------------------------------------------

// Nearest-neighbor downscale from src (src_w × src_h) to dst (dst_w × dst_h).
// Single channel (grayscale).
static void downscale_nearest(const uint8_t* src, int src_w, int src_h, uint8_t* dst, int dst_w, int dst_h) {
  for (int y = 0; y < dst_h; ++y) {
    int sy = y * src_h / dst_h;
    for (int x = 0; x < dst_w; ++x) {
      int sx = x * src_w / dst_w;
      dst[y * dst_w + x] = src[sy * src_w + sx];
    }
  }
}

ImageError decode_image(const uint8_t* data, size_t size, uint16_t max_w, uint16_t max_h, DecodedImage& out) {
  auto fmt = guess_format_from_magic(data, size);
  if (fmt == ImageFormat::Unknown)
    return ImageError::UnsupportedFormat;

  // Decode to 8-bit grayscale using stb_image
  int w = 0, h = 0, channels = 0;
  uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 1);
  if (!pixels)
    return ImageError::InvalidData;

  // Calculate target dimensions
  uint16_t target_w, target_h;
  scaled_size(static_cast<uint16_t>(w), static_cast<uint16_t>(h), max_w, max_h, target_w, target_h);

  // Downscale if needed
  std::vector<uint8_t> gray;
  if (target_w != w || target_h != h) {
    gray.resize(target_w * target_h);
    downscale_nearest(pixels, w, h, gray.data(), target_w, target_h);
  } else {
    gray.assign(pixels, pixels + w * h);
  }
  stbi_image_free(pixels);

  // Dither to 1-bit
  out.width = target_w;
  out.height = target_h;
  out.data.resize(out.data_size());
  floyd_steinberg_dither(gray.data(), target_w, target_h, out.data.data());

  return ImageError::Ok;
}

}  // namespace microreader
