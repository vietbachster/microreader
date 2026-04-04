#pragma once

#include <memory>
#include <string>

#include "ContentModel.h"
#include "EpubParser.h"
#ifndef MICROREADER_NO_IMAGES
#include "ImageDecoder.h"
#endif
#include "ZipReader.h"

namespace microreader {

// High-level Book interface — owns the file handle and EPUB parser.
// Provides convenient access to chapters, images, and metadata.
class Book {
 public:
  Book() = default;
  ~Book() {
    close();
  }

  // Set CSS unit conversion config (call before open()).
  void set_css_config(const CssConfig& config) {
    epub_.set_css_config(config);
  }

  // Open an EPUB file from a filesystem path.
  EpubError open(const char* path);

  // Release all resources (file handle, parsed EPUB data).
  void close();

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

  // Stream-parse a chapter: paragraphs emitted via callback, ~37KB working memory.
  EpubError load_chapter_streaming(size_t index, ParagraphSink sink, void* sink_ctx, uint8_t* work_buf = nullptr,
                                   uint8_t* xml_buf = nullptr);

#ifndef MICROREADER_NO_IMAGES
  // Extract and decode an image from the EPUB (by zip entry index).
  // Returns the 1-bit dithered bitmap. The caller owns the memory.
  // max_w/max_h: maximum output dimensions (0 = no limit).
  ImageError decode_image(uint16_t entry_index, DecodedImage& out, uint16_t max_w = 0, uint16_t max_h = 0);
#endif

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
