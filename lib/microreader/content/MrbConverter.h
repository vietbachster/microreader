#pragma once

#include "Book.h"
#include "MrbWriter.h"

namespace microreader {

// Convert an opened EPUB book to MRB format.
// The Book must already be open().  Writes output to `output_path`.
// Returns true on success.
bool convert_epub_to_mrb(Book& book, const char* output_path);

// Streaming variant: uses ~37KB working memory per chapter instead of
// extracting the full XHTML. Safe for ESP32's limited RAM.
bool convert_epub_to_mrb_streaming(Book& book, const char* output_path);

}  // namespace microreader
