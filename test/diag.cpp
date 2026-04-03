#include <cstdio>
#include <string>
#include "microreader/content/Book.h"
#include "microreader/content/MrbConverter.h"
#include "microreader/content/MrbReader.h"
using namespace microreader;
int main() {
  const char* files[] = {"books/gutenberg/pg24264-images.epub", "books/gutenberg/tao-te-ching-zh.epub"};
  for (auto* f : files) {
    Book book;
    if (book.open(f) != EpubError::Ok) { printf("SKIP %s\n", f); continue; }
    std::string mrb = std::string(f) + ".mrb";
    convert_epub_to_mrb(book, mrb.c_str());
    MrbReader reader;
    reader.open(mrb.c_str());
    printf("%s: epub_chapters=%d mrb_chapters=%d\n", f, book.chapter_count(), reader.chapter_count());
    for (uint16_t ci = 0; ci < reader.chapter_count(); ++ci) {
      Chapter ch;
      book.load_chapter(ci, ch);
      auto mrb_count = reader.chapter_paragraph_count(ci);
      if (ch.paragraphs.size() != mrb_count) {
        printf("  CH%d: epub_paras=%zu mrb_paras=%u\n", ci, ch.paragraphs.size(), mrb_count);
        for (size_t pi = 0; pi < ch.paragraphs.size(); ++pi) {
          auto& p = ch.paragraphs[pi];
          if (p.type == ParagraphType::Text) {
            size_t total = 0; for (auto& r : p.text.runs) total += r.text.size();
            printf("    p%zu: Text runs=%zu totalchars=%zu", pi, p.text.runs.size(), total);
            if (p.text.alignment.has_value()) printf(" align=%d", (int)*p.text.alignment);
            printf("\n");
          } else {
            printf("    p%zu: type=%d\n", pi, (int)p.type);
          }
        }
      }
    }
    std::remove(mrb.c_str());
  }
}
