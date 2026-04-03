#pragma once

#include <memory>
#include <string>

#include "ContentModel.h"
#include "EpubParser.h"
#include "ImageDecoder.h"
#include "ZipReader.h"

namespace microreader {

// High-level Book interface — owns the file handle and EPUB parser.
// Provides convenient access to chapters, images, and metadata.
class Book {
 public:
  Book() = default;

  // Set CSS unit conversion config (call before open()).
  void set_css_config(const CssConfig& config) {
    epub_.set_css_config(config);
  }

  // Open an EPUB file from a filesystem path.
  EpubError open(const char* path);

  bool is_open() const {
    return file_open_;
  }

  const EpubMetadata& metadata() const {
    return epub_.metadata();
  }
  const TableOfContents& toc() const {
    return epub_.toc();
  }
  size_t chapter_count() const {
    return epub_.chapter_count();
  }

  // Load a chapter by index (0-based).
  EpubError load_chapter(size_t index, Chapter& out);

  // Extract and decode an image from the EPUB (by zip entry index).
  // Returns the 1-bit dithered bitmap. The caller owns the memory.
  // max_w/max_h: maximum output dimensions (0 = no limit).
  ImageError decode_image(uint16_t entry_index, DecodedImage& out, uint16_t max_w = 0, uint16_t max_h = 0);

  // Extract raw image data (for format detection, etc).
  ZipError extract_entry(uint16_t entry_index, std::vector<uint8_t>& out);

  // Access the underlying EPUB for advanced queries.
  const Epub& epub() const {
    return epub_;
  }

 private:
  StdioZipFile file_;
  Epub epub_;
  bool file_open_ = false;
};

}  // namespace microreader
