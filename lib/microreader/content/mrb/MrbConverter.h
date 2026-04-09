#pragma once

#include "../Book.h"
#include "MrbWriter.h"

namespace microreader {

// Convert an opened EPUB book to MRB format.
// The Book must already be open().  Writes output to `output_path`.
// Returns true on success.
bool convert_epub_to_mrb(Book& book, const char* output_path);

// Streaming variant: uses ~37KB working memory per chapter instead of
// extracting the full XHTML. Safe for ESP32's limited RAM.
// Optional work_buf/xml_buf avoid heap allocation for the decompression
// buffers (pass nullptr to allocate from heap as before).
bool convert_epub_to_mrb_streaming(Book& book, const char* output_path, uint8_t* work_buf = nullptr,
                                   uint8_t* xml_buf = nullptr);

#ifdef ESP_PLATFORM
// Measures conversion sub-stages individually via serial log output.
// open_ms: time already spent in Book::open() (ZIP + OPF + CSS + NCX), reported as BENCH_OPEN.
void benchmark_epub_conversion(Book& book, const char* tmp_path, long open_ms = 0, uint8_t* work_buf = nullptr,
                               uint8_t* xml_buf = nullptr);

// Measures image size-read performance: for each image entry in the EPUB,
// reads the raw bytes from the ZIP and calls read_image_size().
// Emits a per-image table and summary over ESP_LOGI.
void benchmark_image_size_read(Book& book, uint8_t* work_buf = nullptr);

// Fully decodes every image in the EPUB using the streaming decoder, capped
// at 240×320.  Emits per-image timing, decoded dimensions, and heap delta.
void benchmark_image_decode(Book& book, uint8_t* work_buf = nullptr);
#endif

}  // namespace microreader
