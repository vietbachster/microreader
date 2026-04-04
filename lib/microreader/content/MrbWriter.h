#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "ContentModel.h"
#include "MrbFormat.h"

namespace microreader {

// Writes an MRB file sequentially.  Usage:
//
//   MrbWriter w;
//   w.open("book.mrb");
//   w.begin_chapter();
//   w.write_paragraph(para);
//   w.end_chapter();
//   w.finish(metadata, toc);
//
class MrbWriter {
 public:
  MrbWriter() = default;
  ~MrbWriter() {
    close();
  }

  MrbWriter(const MrbWriter&) = delete;
  MrbWriter& operator=(const MrbWriter&) = delete;

  bool open(const char* path);
  void close();

  // Call before writing paragraphs for a new chapter.
  void begin_chapter();

  // Write one paragraph.  Returns false on I/O error.
  bool write_paragraph(const Paragraph& para);

  // Call after writing all paragraphs for a chapter.
  void end_chapter();

  // Add an image reference (dimensions pre-resolved from EPUB).
  // Returns the image index to use in paragraph image refs.
  uint16_t add_image_ref(uint16_t zip_entry_index, uint16_t width, uint16_t height);

  // Finalize: write index tables, metadata, TOC, and fix up header.
  bool finish(const EpubMetadata& meta, const TableOfContents& toc);

 private:
  FILE* f_ = nullptr;
  uint32_t paragraph_count_ = 0;
  std::vector<MrbChapterEntry> chapters_;
  std::vector<MrbImageRef> images_;
  bool in_chapter_ = false;

  // Linked-list tracking: file offsets of prev/next paragraph within a chapter.
  uint32_t prev_para_offset_ = 0;      // offset of the previous paragraph (0 = none)
  uint32_t chapter_first_offset_ = 0;  // offset of first paragraph in current chapter
  uint16_t chapter_para_count_ = 0;    // paragraph count in current chapter

  // Serialize a text paragraph's body (everything after the 3-byte type+size header).
  std::vector<uint8_t> serialize_text(const TextParagraph& text, uint16_t spacing_before);

  // Write raw bytes.
  bool write_bytes(const void* data, size_t size);

  // Write a length-prefixed UTF-8 string (uint16 length + bytes).
  bool write_string(const std::string& s);
};

}  // namespace microreader
