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
#include "microreader/screens/ReaderScreen.h"

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
  book.close();

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(mrb_path.c_str())) << "MRB open failed";

  auto font = ReaderScreen::make_fixed_font();
  auto opts = ReaderScreen::make_page_opts();
  auto size_fn = make_image_size_query(mrb, epub_path.string(), opts.width);

  // ── Output file ──────────────────────────────────────────────────────────
  fs::path out_path = fs::path(repo_root()) / "test" / "output" / "alice_debug_layout.txt";
  fs::create_directories(out_path.parent_path());
  std::ofstream out(out_path);
  ASSERT_TRUE(out.good()) << "Cannot open output file: " << out_path;

  auto write_separator = [&](char c = '-', int n = 80) { out << std::string(static_cast<size_t>(n), c) << '\n'; };

  out << "Book: " << epub_path.filename().string() << '\n';
  out << "Total chapters: " << mrb.chapter_count() << '\n';
  out << "Page size: " << DrawBuffer::kWidth << "x" << DrawBuffer::kHeight << '\n';
  out << "Padding: top=" << ReaderScreen::kPaddingTop << " right=" << ReaderScreen::kPaddingRight
      << " bottom=" << ReaderScreen::kPaddingBottom << " left=" << ReaderScreen::kPaddingLeft
      << "  para_spacing=" << ReaderScreen::kParaSpacing << '\n';
  write_separator('=');

  int global_page = 0;

  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);
    int page_in_chapter = 0;

    TextLayout tl(font, opts, src, size_fn);
    while (true) {
      auto pc = tl.layout();
      ++global_page;
      ++page_in_chapter;

      out << "=== CH " << ci << " | Page " << page_in_chapter << " (global #" << global_page << ")"
          << " | pos{" << pc.start.paragraph << "," << pc.start.offset << "}"
          << " \xe2\x86\x92 {" << pc.end.paragraph << "," << pc.end.offset << "}"
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
          } else if (gap > ReaderScreen::kParaSpacing + font.y_advance()) {
            out << "  --- gap " << gap << "px between items " << (i - 1) << " and " << i << " ---\n";
          }
        }

        const int last_bottom = items.back().y + items.back().h;
        const int usable_bottom = DrawBuffer::kHeight - ReaderScreen::kPaddingBottom;
        out << "  [tail gap to usable bottom: " << (usable_bottom - last_bottom) << "px"
            << " | usable area: " << (usable_bottom - ReaderScreen::kPaddingTop) << "px]\n";
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

// ---------------------------------------------------------------------------
// PositionRestoreAtCroppedImage — for every page in alice-illustrated that
// contains a cropped image (y_crop > 0, i.e. mid-split), save page.start,
// restore the layout engine to that position, and verify the re-laid-out page
// is identical: same number of items, same image keys/crops/heights, same text
// paragraph and line indices.
//
// Uses the real page dimensions and an ImageSizeQuery backed by the EPUB file
// (same logic as ReaderScreen::resolve_image_size_), so promoted inline images
// are handled just like on the device.
// ---------------------------------------------------------------------------
TEST(DebugLayoutTest, PositionRestoreAtCroppedImage) {
  fs::path epub_path = fs::path(small_books_dir()) / "alice-illustrated.epub";
  ASSERT_TRUE(fs::exists(epub_path)) << "alice-illustrated.epub not found in test/books/small/";

  Book book;
  ASSERT_EQ(book.open(epub_path.string().c_str()), EpubError::Ok);

  auto mrb_path = (fs::temp_directory_path() / "alice_pos_restore.mrb").string();
  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, mrb_path.c_str())) << "MRB conversion failed";
  book.close();

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(mrb_path.c_str())) << "MRB open failed";

  auto font = ReaderScreen::make_fixed_font();
  auto opts = ReaderScreen::make_page_opts();
  auto size_fn = make_image_size_query(mrb, epub_path.string(), opts.width);

  int checked = 0;

  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);
    TextLayout tl(font, opts, src, size_fn);

    int safety = 0;
    while (safety++ < 5000) {
      auto page = tl.layout();

      // Check if any image on this page is a continuation slice (y_crop > 0).
      for (const auto& img : page.image_items) {
        if (img.y_crop == 0)
          continue;

        // Restore position to page.start and re-layout with a cold cache
        // (fresh ImageSizeQuery, new MrbChapterSource) to verify the saved
        // position alone is sufficient — no warm state may carry over.
        MrbReader mrb2;
        ASSERT_TRUE(mrb2.open(mrb_path.c_str())) << "ch" << ci << " MRB reopen failed";
        MrbChapterSource src2(mrb2, ci);
        auto cold_size_fn = make_image_size_query(mrb2, epub_path.string(), opts.width);
        TextLayout tl2(font, opts, src2, cold_size_fn);
        tl2.set_position(page.start);
        auto page2 = tl2.layout();

        ASSERT_EQ(page2.text_items.size(), page.text_items.size())
            << "ch" << ci << " restored page has different text item count";
        ASSERT_EQ(page2.image_items.size(), page.image_items.size())
            << "ch" << ci << " restored page has different image item count";
        ASSERT_EQ(page2.hr_items.size(), page.hr_items.size())
            << "ch" << ci << " restored page has different hr item count";

        for (size_t i = 0; i < page.image_items.size(); ++i) {
          EXPECT_EQ(page2.image_items[i].key, page.image_items[i].key)
              << "ch" << ci << " img[" << i << "] key mismatch";
          EXPECT_EQ(page2.image_items[i].y_crop, page.image_items[i].y_crop)
              << "ch" << ci << " img[" << i << "] y_crop mismatch";
          EXPECT_EQ(page2.image_items[i].height, page.image_items[i].height)
              << "ch" << ci << " img[" << i << "] height mismatch";
          EXPECT_EQ(page2.image_items[i].full_height, page.image_items[i].full_height)
              << "ch" << ci << " img[" << i << "] full_height mismatch";
          EXPECT_EQ(page2.image_items[i].y_offset, page.image_items[i].y_offset)
              << "ch" << ci << " img[" << i << "] y_offset mismatch";
        }

        for (size_t i = 0; i < page.text_items.size(); ++i) {
          EXPECT_EQ(page2.text_items[i].paragraph_index, page.text_items[i].paragraph_index)
              << "ch" << ci << " text[" << i << "] paragraph_index mismatch";
          EXPECT_EQ(page2.text_items[i].line_index, page.text_items[i].line_index)
              << "ch" << ci << " text[" << i << "] line_index mismatch";
        }

        EXPECT_EQ(page2.start, page.start) << "ch" << ci << " restored page.start mismatch";
        EXPECT_EQ(page2.end, page.end) << "ch" << ci << " restored page.end mismatch";

        ++checked;
        break;  // one check per page is enough
      }

      if (page.at_chapter_end)
        break;
      tl.set_position(page.end);
    }
  }

  mrb.close();
  std::remove(mrb_path.c_str());

  EXPECT_GT(checked, 0) << "No cropped images found — alice-illustrated should have split images";
  std::cout << "Checked " << checked << " cropped-image position restores.\n";
}
