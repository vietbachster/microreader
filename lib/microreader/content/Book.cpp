#include "Book.h"

namespace microreader {

EpubError Book::open(const char* path) {
  if (!file_.open(path)) return EpubError::ZipError;
  file_open_ = true;
  return epub_.open(file_);
}

EpubError Book::load_chapter(size_t index, Chapter& out) {
  return epub_.parse_chapter(file_, index, out);
}

ImageError Book::decode_image(uint16_t entry_index, DecodedImage& out,
                              uint16_t max_w, uint16_t max_h) {
  if (entry_index >= epub_.zip().entry_count()) return ImageError::UnsupportedFormat;

  auto& entry = epub_.zip().entry(entry_index);
  std::vector<uint8_t> data;
  if (epub_.zip().extract(file_, entry, data) != ZipError::Ok) {
    return ImageError::ReadError;
  }

  return microreader::decode_image(data.data(), data.size(), max_w, max_h, out);
}

ZipError Book::extract_entry(uint16_t entry_index, std::vector<uint8_t>& out) {
  if (entry_index >= epub_.zip().entry_count()) return ZipError::InvalidData;
  auto& entry = epub_.zip().entry(entry_index);
  return epub_.zip().extract(file_, entry, out);
}

}  // namespace microreader
