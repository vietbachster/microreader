#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../ContentModel.h"
#include "../IParagraphSource.h"
#include "../ImageDecoder.h"
#include "../TextLayout.h"
#include "../ZipReader.h"
#include "MrbFormat.h"

namespace microreader {

// Reads an MRB file.  Loads the chapter table and image refs into RAM on
// open(), then provides paragraph loading by file offset.  Paragraphs are
// linked (prev/next offsets) so they can be traversed sequentially.
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
  uint32_t chapter_first_offset(uint16_t chapter_idx) const;
  uint32_t chapter_last_offset(uint16_t chapter_idx) const;
  uint16_t chapter_paragraph_count(uint16_t chapter_idx) const;

  // Load a single paragraph at a given file offset.
  // On success, fills `out` and returns the prev/next file offsets
  // (0 means no prev/next — start/end of chapter).
  struct LoadResult {
    bool ok = false;
    uint32_t prev_offset = 0;
    uint32_t next_offset = 0;
  };
  LoadResult load_paragraph(uint32_t file_offset, Paragraph& out);

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
// for a single chapter, with a sliding-window cache.
//
// On construction, scans the chapter's linked list to build a local
// vector of file offsets (~4 bytes per paragraph).  This replaces the
// old global paragraph index that was stored in the MRB file.
//
// Only kWindowSize paragraphs are kept in memory at once. When a
// paragraph outside the window is requested, the window slides to
// center on the new access point, evicting old entries.
// ---------------------------------------------------------------------------

class MrbChapterSource : public IParagraphSource {
 public:
  static constexpr size_t kWindowSize = 32;

  MrbChapterSource(MrbReader& reader, uint16_t chapter_idx) : reader_(reader) {
    uint16_t count = reader.chapter_paragraph_count(chapter_idx);
    if (count == 0)
      return;

    // Scan the linked list forward to build the offset vector
    offsets_.reserve(count);
    uint32_t cur = reader.chapter_first_offset(chapter_idx);
    Paragraph tmp;
    while (cur != 0 && offsets_.size() < count) {
      offsets_.push_back(cur);
      auto lr = reader.load_paragraph(cur, tmp);
      if (!lr.ok)
        break;
      cur = lr.next_offset;
    }

    size_t actual = offsets_.size() < kWindowSize ? offsets_.size() : kWindowSize;
    slots_.resize(actual);
    slot_index_.resize(actual, UINT32_MAX);
  }

  size_t paragraph_count() const override {
    return offsets_.size();
  }

  const Paragraph& paragraph(size_t index) const override {
    // Check if already in window
    for (size_t i = 0; i < slot_index_.size(); ++i) {
      if (slot_index_[i] == static_cast<uint32_t>(index))
        return slots_[i];
    }

    // Not in window — find a free slot or evict the furthest entry
    size_t slot = find_slot_(index);
    slot_index_[slot] = static_cast<uint32_t>(index);
    reader_.load_paragraph(offsets_[index], slots_[slot]);
    return slots_[slot];
  }

 private:
  MrbReader& reader_;
  std::vector<uint32_t> offsets_;  // file offsets of each paragraph
  mutable std::vector<Paragraph> slots_;
  mutable std::vector<uint32_t> slot_index_;  // UINT32_MAX = empty

  // Find best slot: prefer empty, then evict the one furthest from `index`
  size_t find_slot_(size_t index) const {
    size_t best = 0;
    uint32_t best_dist = 0;
    for (size_t i = 0; i < slot_index_.size(); ++i) {
      if (slot_index_[i] == UINT32_MAX)
        return i;  // empty slot
      uint32_t d = (slot_index_[i] > index) ? static_cast<uint32_t>(slot_index_[i] - index)
                                            : static_cast<uint32_t>(index - slot_index_[i]);
      if (d > best_dist) {
        best_dist = d;
        best = i;
      }
    }
    return best;
  }
};

// ---------------------------------------------------------------------------
// Build an ImageSizeQuery that resolves image dimensions from an MRB file.
// Fast path: width/height stored in MrbImageRef (from HTML attributes).
// Slow path: stream the image header from the EPUB local file entry.
// Scales to fit max_w preserving aspect ratio. Results are cached.
// Mirrors the logic in ReaderScreen::resolve_image_size_().
// ---------------------------------------------------------------------------
inline ImageSizeQuery make_image_size_query(const MrbReader& mrb, const std::string& epub_path, uint16_t max_w) {
  struct Cache {
    std::vector<uint16_t> w, h;
    const MrbReader* mrb;
    std::string epub_path;
    uint16_t max_w;
  };
  auto cache = std::make_shared<Cache>();
  cache->mrb = &mrb;
  cache->epub_path = epub_path;
  cache->max_w = max_w;
  cache->w.assign(mrb.image_count(), 0);
  cache->h.assign(mrb.image_count(), 0);

  return [cache](uint16_t key, uint16_t& w, uint16_t& h) -> bool {
    if (key >= static_cast<uint16_t>(cache->w.size()))
      return false;
    if (cache->w[key] != 0 || cache->h[key] != 0) {
      w = cache->w[key];
      h = cache->h[key];
      return true;
    }
    const auto& ref = cache->mrb->image_ref(key);
    uint16_t src_w = ref.width, src_h = ref.height;
    if (src_w == 0 || src_h == 0) {
      StdioZipFile file;
      if (!file.open(cache->epub_path.c_str()))
        return false;
      ZipEntry entry;
      if (ZipReader::read_local_entry(file, ref.local_header_offset, entry) != ZipError::Ok)
        return false;
      uint8_t small_buf[256];
      ZipEntryInput inp;
      std::unique_ptr<uint8_t[]> heap_buf;
      auto zerr = inp.open(file, entry, small_buf, sizeof(small_buf));
      if (zerr != ZipError::Ok) {
        heap_buf.reset(new (std::nothrow) uint8_t[ZipEntryInput::kMinWorkBufSize]);
        if (!heap_buf)
          return false;
        zerr = inp.open(file, entry, heap_buf.get(), ZipEntryInput::kMinWorkBufSize);
        if (zerr != ZipError::Ok)
          return false;
      }
      ImageSizeStream stream;
      uint8_t chunk[256];
      for (;;) {
        size_t n = inp.read(chunk, sizeof(chunk));
        if (n == 0)
          break;
        if (stream.feed(chunk, n))
          break;
      }
      if (!stream.ok())
        return false;
      src_w = stream.width();
      src_h = stream.height();
    }
    scaled_size(src_w, src_h, cache->max_w, cache->max_w, cache->w[key], cache->h[key]);
    w = cache->w[key];
    h = cache->h[key];
    return w != 0 || h != 0;
  };
}

}  // namespace microreader
