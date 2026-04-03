#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "ContentModel.h"
#include "MrbFormat.h"
#include "TextLayout.h"

namespace microreader {

// Reads an MRB file.  Loads the index tables into RAM on open(), then
// provides random-access paragraph loading via seek + deserialize.
class MrbReader {
 public:
  MrbReader() = default;
  ~MrbReader() {
    close();
  }

  MrbReader(const MrbReader&) = delete;
  MrbReader& operator=(const MrbReader&) = delete;

  bool open(const char* path);
  void close();
  bool is_open() const {
    return f_ != nullptr;
  }

  // Counts
  uint32_t paragraph_count() const {
    return header_.paragraph_count;
  }
  uint16_t chapter_count() const {
    return header_.chapter_count;
  }
  uint16_t image_count() const {
    return header_.image_count;
  }

  // Chapter navigation
  uint32_t chapter_first_paragraph(uint16_t chapter_idx) const;
  uint16_t chapter_paragraph_count(uint16_t chapter_idx) const;

  // Load a single paragraph by global index.
  bool load_paragraph(uint32_t index, Paragraph& out);

  // Image references
  const MrbImageRef& image_ref(uint16_t index) const {
    return images_[index];
  }

  // Metadata
  const EpubMetadata& metadata() const {
    return metadata_;
  }
  const TableOfContents& toc() const {
    return toc_;
  }

 private:
  FILE* f_ = nullptr;
  MrbHeader header_{};
  std::vector<MrbChapterEntry> chapters_;
  std::vector<MrbImageRef> images_;
  EpubMetadata metadata_;
  TableOfContents toc_;

  bool read_bytes(void* buf, size_t size);
  bool read_at(uint32_t offset, void* buf, size_t size);
  std::string read_string();
  bool deserialize_text(const uint8_t* data, size_t size, Paragraph& out);
};

// ---------------------------------------------------------------------------
// IParagraphSource backed by MrbReader — loads paragraphs on demand
// for a single chapter, with lazy caching.
// ---------------------------------------------------------------------------

class MrbChapterSource : public IParagraphSource {
 public:
  MrbChapterSource(MrbReader& reader, uint16_t chapter_idx)
      : reader_(reader),
        first_(reader.chapter_first_paragraph(chapter_idx)),
        count_(reader.chapter_paragraph_count(chapter_idx)) {
    cache_.resize(count_);
    loaded_.resize(count_, false);
  }

  size_t paragraph_count() const override {
    return count_;
  }

  const Paragraph& paragraph(size_t index) const override {
    if (!loaded_[index]) {
      reader_.load_paragraph(first_ + static_cast<uint32_t>(index), cache_[index]);
      loaded_[index] = true;
    }
    return cache_[index];
  }

 private:
  MrbReader& reader_;
  uint32_t first_;
  uint16_t count_;
  mutable std::vector<Paragraph> cache_;
  mutable std::vector<bool> loaded_;
};

}  // namespace microreader
