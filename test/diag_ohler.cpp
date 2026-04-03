// Quick diagnostic: dump first few paragraphs of ohler.epub and ohler.mrb
// to see what text actually looks like (entity decoding, UTF-8 etc.)
//
// Build: add to test CMakeLists or compile standalone:
//   cl /EHsc /std:c++17 /I../../lib diag_ohler.cpp ... -o diag_ohler.exe

#include <cstdio>
#include <cstring>
#include <string>

#include "microreader/content/Book.h"
#include "microreader/content/MrbReader.h"

using namespace microreader;

static void dump_hex(const char* s, size_t len) {
  for (size_t i = 0; i < len && i < 200; ++i) {
    uint8_t b = static_cast<uint8_t>(s[i]);
    if (b >= 0x20 && b < 0x7F)
      printf("%c", b);
    else
      printf("\\x%02X", b);
  }
}

static void dump_paragraph(const Paragraph& p, int idx) {
  printf("  [%d] type=%d", idx, (int)p.type);
  if (p.type == ParagraphType::Text) {
    printf(" runs=%zu", p.text.runs.size());
    for (size_t ri = 0; ri < p.text.runs.size(); ++ri) {
      const auto& r = p.text.runs[ri];
      printf("\n    run[%zu] len=%zu: \"", ri, r.text.size());
      dump_hex(r.text.c_str(), r.text.size());
      printf("\"");
    }
  }
  printf("\n");
}

int main() {
  // --- EPUB path ---
  printf("=== EPUB: ohler.epub ===\n");
  {
    Book book;
    auto err = book.open("c:/Users/Patrick/Desktop/microreader/microreader2/sd/books/ohler.epub");
    if (err != EpubError::Ok) {
      printf("Failed to open epub: %d\n", (int)err);
    } else {
      printf("Chapters: %zu\n", book.chapter_count());
      // Dump first 2 chapters, up to 10 paragraphs each
      for (size_t ci = 0; ci < 2 && ci < book.chapter_count(); ++ci) {
        Chapter ch;
        auto cerr = book.load_chapter(ci, ch);
        if (cerr != EpubError::Ok) {
          printf("Ch %zu: load error %d\n", ci, (int)cerr);
          continue;
        }
        printf("Ch %zu: %zu paragraphs\n", ci, ch.paragraphs.size());
        for (size_t pi = 0; pi < 10 && pi < ch.paragraphs.size(); ++pi) {
          dump_paragraph(ch.paragraphs[pi], (int)pi);
        }
      }
    }
  }

  // --- MRB path ---
  printf("\n=== MRB: ohler.mrb ===\n");
  {
    MrbReader mrb;
    if (!mrb.open("c:/Users/Patrick/Desktop/microreader/microreader2/sd/books/ohler.mrb")) {
      printf("Failed to open mrb\n");
    } else {
      printf("Paragraphs: %u, Chapters: %u\n", mrb.paragraph_count(), mrb.chapter_count());
      // Dump first 2 chapters
      for (uint16_t ci = 0; ci < 2 && ci < mrb.chapter_count(); ++ci) {
        MrbChapterSource src(mrb, ci);
        printf("Ch %u: %zu paragraphs\n", ci, src.paragraph_count());
        for (size_t pi = 0; pi < 10 && pi < src.paragraph_count(); ++pi) {
          dump_paragraph(src.paragraph(pi), (int)pi);
        }
      }
    }
  }

  printf("\n=== Searching for non-ASCII text in first few chapters ===\n");
  {
    Book book;
    auto err = book.open("c:/Users/Patrick/Desktop/microreader/microreader2/sd/books/ohler.epub");
    if (err == EpubError::Ok) {
      for (size_t ci = 0; ci < 3 && ci < book.chapter_count(); ++ci) {
        Chapter ch;
        if (book.load_chapter(ci, ch) != EpubError::Ok)
          continue;
        for (size_t pi = 0; pi < ch.paragraphs.size(); ++pi) {
          if (ch.paragraphs[pi].type != ParagraphType::Text)
            continue;
          for (const auto& run : ch.paragraphs[pi].text.runs) {
            bool has_non_ascii = false;
            bool has_entity = false;
            for (size_t i = 0; i < run.text.size(); ++i) {
              uint8_t b = static_cast<uint8_t>(run.text[i]);
              if (b >= 0x80)
                has_non_ascii = true;
              if (run.text[i] == '&' && i + 1 < run.text.size() && run.text[i + 1] != ' ')
                has_entity = true;
            }
            if (has_non_ascii || has_entity) {
              printf("  ch%zu p%zu: \"", ci, pi);
              dump_hex(run.text.c_str(), run.text.size());
              printf("\"%s%s\n", has_non_ascii ? " [NON-ASCII]" : "", has_entity ? " [HAS-ENTITY]" : "");
            }
          }
        }
      }
    }
  }

  return 0;
}
