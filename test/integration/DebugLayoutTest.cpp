#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestPaths.h"
#include "microreader/content/Book.h"
#include "microreader/content/TextLayout.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/display/DrawBuffer.h"

using namespace microreader;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// DebugAliceLayout — dumps a human-readable layout trace for every page
// of alice-illustrated.epub.  Output: test/output/alice_debug_layout.txt
//
// Each page shows every item's y_offset, height, para/line index, and a
// text snippet (for text items) or dimensions (for image items), plus the
// gap between the last item and the page bottom.  Use this to spot
// incorrect spacing, overlapping items, or wasted whitespace.
// ---------------------------------------------------------------------------
TEST(DebugLayoutTest, AliceIllustrated) {
  fs::path epub_path = fs::path(small_books_dir()) / "alice-illustrated.epub";
  ASSERT_TRUE(fs::exists(epub_path)) << "alice-illustrated.epub not found in test/books/small/";

  // ── Open book and convert to MRB ────────────────────────────────────────
  Book book;
  ASSERT_EQ(book.open(epub_path.string().c_str()), EpubError::Ok);

  auto mrb_path = (fs::temp_directory_path() / "alice_debug.mrb").string();
  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, mrb_path.c_str())) << "MRB conversion failed";

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(mrb_path.c_str())) << "MRB open failed";

  // ── Layout parameters matching ReaderScreen exactly ─────────────────────
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;
  static constexpr int kPadTop = 6;
  static constexpr int kPadRight = 12;
  static constexpr int kPadBottom = 12;
  static constexpr int kPadLeft = 12;
  static constexpr int kParaSp = 8;
  FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);

  PageOptions opts(static_cast<uint16_t>(DrawBuffer::kWidth), static_cast<uint16_t>(DrawBuffer::kHeight),
                   static_cast<uint16_t>(kPadTop), static_cast<uint16_t>(kParaSp), Alignment::Start);
  opts.padding_right = static_cast<uint16_t>(kPadRight);
  opts.padding_bottom = static_cast<uint16_t>(kPadBottom);
  opts.padding_left = static_cast<uint16_t>(kPadLeft);
  opts.center_text = true;

  const int page_height = DrawBuffer::kHeight;

  // ── Output file ──────────────────────────────────────────────────────────
  fs::path out_path = fs::path(repo_root()) / "test" / "output" / "alice_debug_layout.txt";
  fs::create_directories(out_path.parent_path());
  std::ofstream out(out_path);
  ASSERT_TRUE(out.good()) << "Cannot open output file: " << out_path;

  auto write_separator = [&](char c = '-', int n = 80) { out << std::string(static_cast<size_t>(n), c) << '\n'; };

  out << "Book: " << epub_path.filename().string() << '\n';
  out << "Total chapters: " << mrb.chapter_count() << '\n';
  out << "Page size: " << DrawBuffer::kWidth << "x" << DrawBuffer::kHeight << '\n';
  out << "Padding: top=" << kPadTop << " right=" << kPadRight << " bottom=" << kPadBottom << " left=" << kPadLeft
      << "  para_spacing=" << kParaSp << '\n';
  write_separator('=');

  int global_page = 0;

  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);
    int page_in_chapter = 0;

    TextLayout tl(font, opts, src);
    while (true) {
      auto pc = tl.layout();
      ++global_page;
      ++page_in_chapter;

      out << "=== CH " << ci << " | Page " << page_in_chapter << " (global #" << global_page << ")"
          << " | pos{" << pc.start.paragraph << "," << pc.start.line << "}"
          << " \xe2\x86\x92 {" << pc.end.paragraph << "," << pc.end.line << "}"
          << " | voff=" << pc.vertical_offset << " | at_end=" << (pc.at_chapter_end ? 1 : 0) << '\n';

      struct Item {
        int y;
        int h;
        std::string label;
      };
      std::vector<Item> items;

      for (const auto& ti : pc.text_items) {
        std::string snippet;
        for (const auto& w : ti.line.words) {
          if (!snippet.empty())
            snippet += ' ';
          snippet.append(w.text, w.len);
          if (snippet.size() > 60) {
            snippet += "...";
            break;
          }
        }
        int h = ti.height > 0 ? ti.height : font.y_advance();
        char buf[256];
        std::snprintf(buf, sizeof(buf), "  TEXT  y=%4d h=%3d  para=%u:ln=%u  \"%s\"", ti.y_offset + pc.vertical_offset,
                      h, ti.paragraph_index, ti.line_index, snippet.c_str());
        items.push_back({ti.y_offset + pc.vertical_offset, h, buf});
      }

      for (const auto& img : pc.image_items) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "  IMG   y=%4d h=%3d  para=%u  key=%u  %ux%u  @x=%u",
                      img.y_offset + pc.vertical_offset, img.height, img.paragraph_index, img.key, img.width,
                      img.height, img.x_offset);
        items.push_back({img.y_offset + pc.vertical_offset, img.height, buf});
      }

      for (const auto& hr : pc.hr_items) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "  HR    y=%4d        @x=%u  w=%u", hr.y_offset + pc.vertical_offset,
                      hr.x_offset, hr.width);
        items.push_back({hr.y_offset + pc.vertical_offset, 1, buf});
      }

      std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y < b.y; });

      if (items.empty()) {
        out << "  (empty page)\n";
      } else {
        for (const auto& it : items)
          out << it.label << '\n';

        for (size_t i = 1; i < items.size(); ++i) {
          const int prev_bottom = items[i - 1].y + items[i - 1].h;
          const int gap = items[i].y - prev_bottom;
          if (gap < 0) {
            out << "  *** OVERLAP: item " << i << " starts at y=" << items[i].y << " but prev ends at y=" << prev_bottom
                << " (overlap=" << -gap << "px) ***\n";
          } else if (gap > kParaSp + font.y_advance()) {
            out << "  --- gap " << gap << "px between items " << (i - 1) << " and " << i << " ---\n";
          }
        }

        const int last_bottom = items.back().y + items.back().h;
        const int usable_bottom = page_height - kPadBottom;
        out << "  [tail gap to usable bottom: " << (usable_bottom - last_bottom) << "px"
            << " | usable area: " << (usable_bottom - kPadTop) << "px]\n";
      }

      if (pc.at_chapter_end)
        break;
      if (page_in_chapter > 5000)
        break;

      tl.set_position(pc.end);
    }

    write_separator();
  }

  mrb.close();
  std::remove(mrb_path.c_str());

  out.close();
  std::cout << "Layout debug: " << out_path << '\n';
}
