#include "Book.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#define HEAP_LOG(tag)                                                                       \
  ESP_LOGI("mem", "%s: free=%lu largest=%lu", tag, (unsigned long)esp_get_free_heap_size(), \
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))
#else
#define HEAP_LOG(tag) ((void)0)
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

#ifndef MICROREADER_NO_IMAGES
ImageError Book::decode_image(uint16_t entry_index, DecodedImage& out, uint16_t max_w, uint16_t max_h) {
  if (entry_index >= epub_.zip().entry_count())
    return ImageError::UnsupportedFormat;

  auto& entry = epub_.zip().entry(entry_index);
  std::vector<uint8_t> data;
  if (epub_.zip().extract(file_, entry, data) != ZipError::Ok) {
    return ImageError::ReadError;
  }

  return microreader::decode_image(data.data(), data.size(), max_w, max_h, out);
}
#endif

ZipError Book::extract_entry(uint16_t entry_index, std::vector<uint8_t>& out) {
  if (entry_index >= epub_.zip().entry_count())
    return ZipError::InvalidData;
  auto& entry = epub_.zip().entry(entry_index);
  return epub_.zip().extract(file_, entry, out);
}

}  // namespace microreader
