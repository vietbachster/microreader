#include <cstdio>
#include <string>
#include <vector>

#include "microreader/content/Book.h"
#include "microreader/content/ContentModel.h"
using namespace microreader;

// Count paragraphs from streaming path
struct Counter {
  size_t count = 0;
};
static void count_sink(void* ctx, Paragraph&& para) {
  auto& c = *static_cast<Counter*>(ctx);
  c.count++;
}

int main(int argc, char* argv[]) {
  const char* path = "books/gutenberg/pride-prejudice.epub";
  if (argc > 1)
    path = argv[1];

  Book book;
  auto err = book.open(path);
  if (err != EpubError::Ok) {
    printf("Failed to open %s: %d\n", path, (int)err);
    return 1;
  }
  printf("Opened %s: %zu chapters\n", path, book.chapter_count());

  for (size_t ci = 0; ci < book.chapter_count() && ci < 3; ++ci) {
    // Non-streaming
    Chapter ch;
    EpubError cerr = book.load_chapter(ci, ch);
    size_t normal_count = (cerr == EpubError::Ok) ? ch.paragraphs.size() : 0;

    // Streaming
    Counter counter;
    EpubError serr = book.load_chapter_streaming(ci, count_sink, &counter);

    printf("  ch%zu: normal=%zu streaming=%zu (err_n=%d err_s=%d)\n", ci, normal_count, counter.count, (int)cerr,
           (int)serr);

    if (normal_count != counter.count && normal_count > 0) {
      // Show first few paragraph types from normal
      printf("    Normal paragraphs:\n");
      for (size_t i = 0; i < normal_count && i < 10; ++i) {
        auto& p = ch.paragraphs[i];
        if (p.type == ParagraphType::Text) {
          size_t total = 0;
          for (auto& r : p.text.runs)
            total += r.text.size();
          printf("      p%zu: Text runs=%zu chars=%zu\n", i, p.text.runs.size(), total);
        } else {
          printf("      p%zu: type=%d\n", i, (int)p.type);
        }
      }
      if (normal_count > 10)
        printf("      ... (%zu more)\n", normal_count - 10);
    }
  }
  return 0;
}
