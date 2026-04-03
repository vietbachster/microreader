// Diagnostic: Open the ohler.mrb and dump a page with German text
// showing word text, positions, byte lengths, and codepoint counts.
//
// Run: .\build2\Debug\unit_tests.exe --gtest_filter="OhlerDiag.*"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "microreader/Font.h"
#include "microreader/content/MrbReader.h"
#include "microreader/content/TextLayout.h"

using namespace microreader;

// Count UTF-8 codepoints in a byte string
static int count_codepoints(const char* text, size_t len) {
  int count = 0;
  for (size_t i = 0; i < len;) {
    uint8_t b = static_cast<uint8_t>(text[i]);
    if (b < 0x80)
      i += 1;
    else if (b < 0xE0)
      i += 2;
    else if (b < 0xF0)
      i += 3;
    else
      i += 4;
    ++count;
  }
  return count;
}

// Count glyphs as ReaderScreen would (via next_glyph_index)
static int count_render_glyphs(const char* text, size_t len) {
  const char* p = text;
  const char* end = text + len;
  int count = 0;
  while (p < end && *p) {
    next_glyph_index(p);
    ++count;
  }
  return count;
}

TEST(OhlerDiag, DumpGermanPage) {
  // Try multiple paths for the ohler MRB
  MrbReader mrb;
  const char* paths[] = {
      "sd/books/ohler.mrb",
      "../sd/books/ohler.mrb",
      "../../sd/books/ohler.mrb",
      "c:/Users/Patrick/Desktop/microreader/microreader2/sd/books/ohler.mrb",
  };
  bool opened = false;
  for (auto path : paths) {
    if (mrb.open(path)) {
      opened = true;
      printf("Opened: %s\n", path);
      break;
    }
  }
  if (!opened)
    GTEST_SKIP() << "ohler.mrb not found";

  printf("Chapters: %u, Total paragraphs: %u\n", mrb.chapter_count(), mrb.paragraph_count());

  // Find first chapter with non-ASCII text
  FixedFont font(16, 20);
  PageOptions opts(480, 800, 20, 12, Alignment::Start);

  for (uint16_t ci = 0; ci < mrb.chapter_count() && ci < 10; ++ci) {
    MrbChapterSource src(mrb, ci);
    printf("\n=== Chapter %u: %zu paragraphs ===\n", ci, src.paragraph_count());

    // Check if chapter has non-ASCII
    bool has_nonascii = false;
    for (size_t pi = 0; pi < src.paragraph_count() && pi < 50; ++pi) {
      const auto& p = src.paragraph(pi);
      if (p.type != ParagraphType::Text)
        continue;
      for (const auto& run : p.text.runs) {
        for (char c : run.text) {
          if (static_cast<uint8_t>(c) > 127) {
            has_nonascii = true;
            break;
          }
        }
        if (has_nonascii)
          break;
      }
      if (has_nonascii)
        break;
    }

    if (!has_nonascii) {
      printf("  (no non-ASCII text in first 50 paragraphs)\n");
      continue;
    }

    // Layout first page
    PagePosition pos{0, 0};
    auto page = layout_page(font, opts, src, pos);
    printf("  Page has %zu text items\n", page.text_items.size());

    int mismatches = 0;
    for (size_t ti = 0; ti < page.text_items.size(); ++ti) {
      const auto& item = page.text_items[ti];
      for (size_t wi = 0; wi < item.line.words.size(); ++wi) {
        const auto& w = item.line.words[wi];
        std::string text(w.text, w.len);
        int cp = count_codepoints(w.text, w.len);
        int rg = count_render_glyphs(w.text, w.len);
        uint16_t mw = font.word_width(w.text, w.len, w.style, w.size);

        bool has_na = false;
        for (char c : text) {
          if (static_cast<uint8_t>(c) > 127) {
            has_na = true;
            break;
          }
        }

        // The rendering now uses font.char_width() for glyph advance,
        // so verify layout width matches codepoints × per-glyph advance.
        uint16_t glyph_advance = font.char_width(' ', w.style, w.size);
        uint16_t expected_width = static_cast<uint16_t>(cp * glyph_advance);
        bool mismatch = (cp != rg) || (mw != expected_width);
        if (has_na || mismatch) {
          printf(
              "  word[%zu][%zu] x=%u bytes=%u cp=%d render_glyphs=%d "
              "layout_width=%u expected=%d (adv=%u) %s: \"",
              ti, wi, w.x, w.len, cp, rg, mw, expected_width, glyph_advance, mismatch ? "*** MISMATCH ***" : "OK");
          // Print with hex for non-ASCII
          for (size_t i = 0; i < w.len; ++i) {
            uint8_t b = static_cast<uint8_t>(w.text[i]);
            if (b >= 0x20 && b < 0x7F)
              printf("%c", b);
            else
              printf("\\x%02X", b);
          }
          printf("\"\n");
          if (mismatch)
            ++mismatches;
        }
      }
    }

    EXPECT_EQ(mismatches, 0) << "Found words with layout/render width mismatches";

    // Only dump first chapter with non-ASCII
    break;
  }
}
