#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "../ContentModel.h"
#include "MrbFormat.h"

namespace microreader {

// Batches small writes into a memory buffer (4 KB), flushing to the
// underlying FILE* only when full or on seek.  tell() is O(1).
class BufferedFileWriter {
 public:
  BufferedFileWriter() = default;
  ~BufferedFileWriter() {
    close();
  }
  BufferedFileWriter(const BufferedFileWriter&) = delete;
  BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;

  bool open(const char* path);
  void close();
  bool write(const void* data, size_t size);
  bool seek(uint32_t offset);
  bool flush();
  uint32_t tell() const {
    return pos_;
  }
  bool is_open() const {
    return f_ != nullptr;
  }

 private:
  static constexpr size_t kBufSize = 4096;
  FILE* f_ = nullptr;
  uint32_t pos_ = 0;
  size_t used_ = 0;
  uint8_t buf_[kBufSize];
};

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
  uint16_t add_image_ref(uint32_t local_header_offset, uint16_t width, uint16_t height);

  // Update the size of an existing image ref (used by MRB converter after
  // lazy resolution).  No-op if idx is out of range.
  void update_image_size(uint16_t idx, uint16_t width, uint16_t height);

  // Returns true if the image ref at idx has non-zero dimensions.
  bool image_size_known(uint16_t idx) const {
    return idx < images_.size() && (images_[idx].width != 0 || images_[idx].height != 0);
  }

  // Finalize: write index tables, metadata, TOC, and fix up header.
  bool finish(const EpubMetadata& meta, const TableOfContents& toc);

 private:
  BufferedFileWriter bw_;
  uint32_t paragraph_count_ = 0;
  std::vector<MrbChapterEntry> chapters_;
  std::vector<MrbImageRef> images_;
  bool in_chapter_ = false;

  // Linked-list tracking: file offsets of prev/next paragraph within a chapter.
  uint32_t prev_para_offset_ = 0;      // offset of the previous paragraph (0 = none)
  uint32_t chapter_first_offset_ = 0;  // offset of first paragraph in current chapter
  uint16_t chapter_para_count_ = 0;    // paragraph count in current chapter

  // Deferred paragraph write: we buffer the serialized bytes of the most
  // recent paragraph so that when the *next* paragraph arrives we can patch
  // its next_offset (at byte 4) before flushing to disk.  This eliminates
  // all backward seeks that the old end_chapter() link-patching required.
  std::vector<uint8_t> pending_para_;
  uint32_t pending_para_offset_ = 0;  // file offset where pending_para_ will be written

  // Reusable serialization buffer (avoids per-paragraph heap allocation).
  std::vector<uint8_t> serialize_buf_;

  // Serialize a text paragraph's body into serialize_buf_.
  void serialize_text(const TextParagraph& text, uint16_t spacing_before);

  // Write raw bytes.
  bool write_bytes(const void* data, size_t size);

  // Flush the pending paragraph buffer to disk.  If next_offset is non-zero,
  // it is patched into bytes [4..7] before writing.
  bool flush_pending(uint32_t next_offset);

  // Serialize a paragraph into pending_para_ (link header + type + data).
  // Does NOT write to disk — the bytes stay buffered until flush_pending().
  bool stage_paragraph(const Paragraph& para);

  // Write a length-prefixed UTF-8 string (uint16 length + bytes).
  bool write_string(const std::string& s);
};

}  // namespace microreader
