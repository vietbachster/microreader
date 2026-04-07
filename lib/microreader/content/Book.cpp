#include "Book.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#define HEAP_LOG(tag)                                                                       \
  ESP_LOGI("mem", "%s: free=%lu largest=%lu", tag, (unsigned long)esp_get_free_heap_size(), \
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))
#define IMG_LOG(...) ESP_LOGI("book", __VA_ARGS__)
#else
#include <cstdio>
#define HEAP_LOG(tag) ((void)0)
#define IMG_LOG(...) printf("[book] " __VA_ARGS__), printf("\n")
#endif

namespace microreader {

EpubError Book::open(const char* path) {
  HEAP_LOG("book.open: start");
  close();  // release previous resources
  if (!file_.open(path))
    return EpubError::ZipError;
  HEAP_LOG("book.open: file opened");
  file_open_ = true;
  auto err = epub_.open(file_);
  HEAP_LOG("book.open: done");
  return err;
}

void Book::close() {
  epub_.close();
  file_.close();
  file_open_ = false;
}

EpubError Book::load_chapter(size_t index, Chapter& out) {
  return epub_.parse_chapter(file_, index, out);
}

EpubError Book::load_chapter_streaming(size_t index, ParagraphSink sink, void* sink_ctx, uint8_t* work_buf,
                                       uint8_t* xml_buf) {
  return epub_.parse_chapter_streaming(file_, index, sink, sink_ctx, work_buf, xml_buf);
}

ImageError Book::decode_image(uint16_t entry_index, DecodedImage& out, uint16_t max_w, uint16_t max_h) {
  if (!images_enabled)
    return ImageError::UnsupportedFormat;
  if (entry_index >= epub_.zip().entry_count())
    return ImageError::UnsupportedFormat;

  auto& entry = epub_.zip().entry(entry_index);
  std::vector<uint8_t> data;
  if (epub_.zip().extract(file_, entry, data) != ZipError::Ok) {
    return ImageError::ReadError;
  }

  return microreader::decode_image(data.data(), data.size(), max_w, max_h, out);
}

ZipError Book::extract_entry(uint16_t entry_index, std::vector<uint8_t>& out) {
  if (entry_index >= epub_.zip().entry_count())
    return ZipError::InvalidData;
  auto& entry = epub_.zip().entry(entry_index);
  return epub_.zip().extract(file_, entry, out);
}

bool Book::read_image_size(uint16_t entry_index, uint16_t& w, uint16_t& h) {
  if (entry_index >= epub_.zip().entry_count()) {
    IMG_LOG("read_image_size: entry %u out of range (count=%u)", entry_index, (unsigned)epub_.zip().entry_count());
    return false;
  }
  static constexpr size_t kWorkSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 1024;
  auto work_buf = std::make_unique<uint8_t[]>(kWorkSize);
  ImageSizeStream stream;
  epub_.zip().extract_streaming(
      file_, epub_.zip().entry(entry_index),
      [](const uint8_t* d, size_t n, void* ud) -> bool { return !static_cast<ImageSizeStream*>(ud)->feed(d, n); },
      &stream, work_buf.get(), kWorkSize);
  if (!stream.ok()) {
    IMG_LOG("read_image_size: entry %u stream failed", entry_index);
    return false;
  }
  w = stream.width();
  h = stream.height();
  IMG_LOG("read_image_size: entry %u -> %ux%u", entry_index, w, h);
  return true;
}

}  // namespace microreader
