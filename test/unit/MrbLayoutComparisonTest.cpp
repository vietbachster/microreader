#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "TestBooks.h"
#include "microreader/content/Book.h"
#include "microreader/content/MrbConverter.h"
#include "microreader/content/MrbReader.h"
#include "microreader/content/TextLayout.h"

namespace fs = std::filesystem;
using namespace microreader;

// MrbChapterSource is now in MrbReader.h — used directly from library.

// ---------------------------------------------------------------------------
// Comparison helpers
// ---------------------------------------------------------------------------

static void assert_words_equal(const LayoutWord& a, const LayoutWord& b, const std::string& ctx) {
  std::string a_text(a.text, a.len);
  std::string b_text(b.text, b.len);
  EXPECT_EQ(a_text, b_text) << ctx << " text";
  EXPECT_EQ(a.x, b.x) << ctx << " x (text='" << a_text << "')";
  EXPECT_EQ(a.style, b.style) << ctx << " style";
  EXPECT_EQ(a.size, b.size) << ctx << " size";
  EXPECT_EQ(a.vertical_align, b.vertical_align) << ctx << " vertical_align";
  EXPECT_EQ(a.continues_prev, b.continues_prev) << ctx << " continues_prev";
}

static void assert_pages_equal(const PageContent& epub_page, const PageContent& mrb_page, const std::string& ctx) {
  EXPECT_EQ(epub_page.start, mrb_page.start) << ctx << " start";
  EXPECT_EQ(epub_page.end, mrb_page.end) << ctx << " end";
  EXPECT_EQ(epub_page.at_chapter_end, mrb_page.at_chapter_end) << ctx << " at_chapter_end";
  EXPECT_EQ(epub_page.vertical_offset, mrb_page.vertical_offset) << ctx << " vertical_offset";

  // Text items
  ASSERT_EQ(epub_page.text_items.size(), mrb_page.text_items.size()) << ctx << " text_items count";
  for (size_t ti = 0; ti < epub_page.text_items.size(); ++ti) {
    const auto& ea = epub_page.text_items[ti];
    const auto& ma = mrb_page.text_items[ti];
    std::string tctx = ctx + " text[" + std::to_string(ti) + "]";

    EXPECT_EQ(ea.paragraph_index, ma.paragraph_index) << tctx << " para_idx";
    EXPECT_EQ(ea.line_index, ma.line_index) << tctx << " line_idx";
    EXPECT_EQ(ea.y_offset, ma.y_offset) << tctx << " y_offset";
    EXPECT_EQ(ea.line.hyphenated, ma.line.hyphenated) << tctx << " hyphenated";

    ASSERT_EQ(ea.line.words.size(), ma.line.words.size()) << tctx << " word count";
    for (size_t wi = 0; wi < ea.line.words.size(); ++wi) {
      assert_words_equal(ea.line.words[wi], ma.line.words[wi], tctx + " word[" + std::to_string(wi) + "]");
    }
  }

  // Image items
  ASSERT_EQ(epub_page.image_items.size(), mrb_page.image_items.size()) << ctx << " image_items count";
  for (size_t ii = 0; ii < epub_page.image_items.size(); ++ii) {
    const auto& ea = epub_page.image_items[ii];
    const auto& ma = mrb_page.image_items[ii];
    std::string ictx = ctx + " img[" + std::to_string(ii) + "]";

    EXPECT_EQ(ea.paragraph_index, ma.paragraph_index) << ictx << " para_idx";
    // Note: image key differs — EPUB uses zip entry index, MRB uses remapped
    // image ref table index.  Dimensions and positions are what matter.
    EXPECT_EQ(ea.width, ma.width) << ictx << " width";
    EXPECT_EQ(ea.height, ma.height) << ictx << " height";
    EXPECT_EQ(ea.x_offset, ma.x_offset) << ictx << " x_offset";
    EXPECT_EQ(ea.y_offset, ma.y_offset) << ictx << " y_offset";
  }

  // HR items
  ASSERT_EQ(epub_page.hr_items.size(), mrb_page.hr_items.size()) << ctx << " hr_items count";
  for (size_t hi = 0; hi < epub_page.hr_items.size(); ++hi) {
    const auto& ea = epub_page.hr_items[hi];
    const auto& ma = mrb_page.hr_items[hi];
    std::string hctx = ctx + " hr[" + std::to_string(hi) + "]";

    EXPECT_EQ(ea.x_offset, ma.x_offset) << hctx << " x_offset";
    EXPECT_EQ(ea.y_offset, ma.y_offset) << hctx << " y_offset";
    EXPECT_EQ(ea.width, ma.width) << hctx << " width";
  }
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "."
#endif

class MrbLayoutComparisonTest : public ::testing::Test {
 protected:
  std::string tmp_path_;

  void SetUp() override {
    tmp_path_ = (fs::temp_directory_path() / "mrb_layout_cmp.mrb").string();
  }

  void TearDown() override {
    std::remove(tmp_path_.c_str());
  }

  static std::string fixture(const char* name) {
    return std::string(TEST_FIXTURES_DIR) + "/" + name;
  }

  // Compare all chapters' full pagination between EPUB and MRB paths.
  void compare_book(const char* epub_name) {
    Book book;
    auto err = book.open(fixture(epub_name).c_str());
    if (err != EpubError::Ok)
      GTEST_SKIP() << epub_name << " not found";

    ASSERT_TRUE(convert_epub_to_mrb(book, tmp_path_.c_str())) << "Failed to convert " << epub_name;

    MrbReader mrb;
    ASSERT_TRUE(mrb.open(tmp_path_.c_str())) << "Failed to open MRB for " << epub_name;

    ASSERT_EQ(mrb.chapter_count(), static_cast<uint16_t>(book.chapter_count()))
        << epub_name << " chapter count mismatch";

    FixedFont font(16, 20);
    PageOptions opts(480, 800, 20, 16, Alignment::Start);

    for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
      // EPUB path: load chapter into memory.
      Chapter ch;
      book.load_chapter(ci, ch);

      // MRB path: on-demand paragraph source.
      MrbChapterSource mrb_src(mrb, ci);

      // Verify paragraph counts match.
      ASSERT_EQ(ch.paragraphs.size(), mrb_src.paragraph_count()) << epub_name << " ch" << ci << " paragraph count";

      // Paginate both and compare.
      PagePosition epub_pos{0, 0};
      PagePosition mrb_pos{0, 0};
      int page_num = 0;

      while (true) {
        auto epub_page = layout_page(font, opts, ch, epub_pos);
        auto mrb_page = layout_page(font, opts, mrb_src, mrb_pos);

        std::string ctx = std::string(epub_name) + " ch" + std::to_string(ci) + " page" + std::to_string(page_num);
        assert_pages_equal(epub_page, mrb_page, ctx);

        if (epub_page.at_chapter_end) {
          EXPECT_TRUE(mrb_page.at_chapter_end) << ctx << " MRB should also be at chapter end";
          break;
        }
        ASSERT_FALSE(mrb_page.at_chapter_end) << ctx << " MRB at chapter end too early";

        epub_pos = epub_page.end;
        mrb_pos = mrb_page.end;
        ++page_num;

        ASSERT_LT(page_num, 5000) << ctx << " too many pages (safety limit)";
      }
    }
  }
};

// ---------------------------------------------------------------------------
// Per-fixture EPUB comparison tests
// ---------------------------------------------------------------------------

TEST_F(MrbLayoutComparisonTest, Basic) {
  compare_book("basic.epub");
}

TEST_F(MrbLayoutComparisonTest, MultiChapter) {
  compare_book("multi_chapter.epub");
}

TEST_F(MrbLayoutComparisonTest, LargeChapter) {
  compare_book("large_chapter.epub");
}

TEST_F(MrbLayoutComparisonTest, WithCss) {
  compare_book("with_css.epub");
}

TEST_F(MrbLayoutComparisonTest, WithImages) {
  compare_book("with_images.epub");
}

TEST_F(MrbLayoutComparisonTest, SpecialChars) {
  compare_book("special_chars.epub");
}

TEST_F(MrbLayoutComparisonTest, NestedDirs) {
  compare_book("nested_dirs.epub");
}

TEST_F(MrbLayoutComparisonTest, RegressionTest) {
  compare_book("regression_test.epub");
}

TEST_F(MrbLayoutComparisonTest, Stored) {
  compare_book("stored.epub");
}

// ---------------------------------------------------------------------------
// Bulk comparison: curated EPUB list from TestBooks.h
// ---------------------------------------------------------------------------

class BulkMrbComparisonTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string tmp_path_;

  void SetUp() override {
    tmp_path_ = (fs::temp_directory_path() / "mrb_bulk_cmp.mrb").string();
  }

  void TearDown() override {
    std::remove(tmp_path_.c_str());
  }
};

TEST_P(BulkMrbComparisonTest, LayoutMatchesEpub) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Cannot open " << filename << ": error " << (int)err;

  ASSERT_TRUE(convert_epub_to_mrb(book, tmp_path_.c_str())) << "Failed to convert " << filename;

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(tmp_path_.c_str())) << "Failed to open MRB for " << filename;

  ASSERT_EQ(mrb.chapter_count(), static_cast<uint16_t>(book.chapter_count())) << filename << " chapter count mismatch";

  FixedFont font(16, 20);
  PageOptions opts(480, 800, 20, 16, Alignment::Start);

  size_t total_pages = 0;
  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    Chapter ch;
    book.load_chapter(ci, ch);

    MrbChapterSource mrb_src(mrb, ci);
    ASSERT_EQ(ch.paragraphs.size(), mrb_src.paragraph_count()) << filename << " ch" << ci << " paragraph count";

    PagePosition epub_pos{0, 0};
    PagePosition mrb_pos{0, 0};
    int page_num = 0;

    while (true) {
      auto epub_page = layout_page(font, opts, ch, epub_pos);
      auto mrb_page = layout_page(font, opts, mrb_src, mrb_pos);

      std::string ctx = filename + " ch" + std::to_string(ci) + " page" + std::to_string(page_num);
      assert_pages_equal(epub_page, mrb_page, ctx);

      if (epub_page.at_chapter_end) {
        EXPECT_TRUE(mrb_page.at_chapter_end) << ctx;
        break;
      }
      ASSERT_FALSE(mrb_page.at_chapter_end) << ctx;

      epub_pos = epub_page.end;
      mrb_pos = mrb_page.end;
      ++page_num;
      ++total_pages;

      ASSERT_LT(page_num, 50000) << ctx << " too many pages (safety limit)";
    }
    total_pages++;
  }
  printf("  [%s] %d chapters, %zu pages — MATCH\n", filename.c_str(), mrb.chapter_count(), total_pages);
}

INSTANTIATE_TEST_SUITE_P(AllBooks, BulkMrbComparisonTest, ::testing::ValuesIn(test_books::get_curated_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           return test_books::epub_test_name(info.param);
                         });

// ---------------------------------------------------------------------------
// Streaming converter comparison: verify convert_epub_to_mrb_streaming
// produces identical MRB files as convert_epub_to_mrb.
// ---------------------------------------------------------------------------

class StreamingMrbComparisonTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string mrb_normal_;
  std::string mrb_streaming_;

  void SetUp() override {
    mrb_normal_ = (fs::temp_directory_path() / "mrb_stream_cmp_normal.mrb").string();
    mrb_streaming_ = (fs::temp_directory_path() / "mrb_stream_cmp_streaming.mrb").string();
  }

  void TearDown() override {
    std::remove(mrb_normal_.c_str());
    std::remove(mrb_streaming_.c_str());
  }
};

TEST_P(StreamingMrbComparisonTest, StreamingMatchesNormal) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  // Convert with normal path.
  Book book1;
  auto err = book1.open(epub_path.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Cannot open " << filename;
  ASSERT_TRUE(convert_epub_to_mrb(book1, mrb_normal_.c_str())) << "Normal convert failed for " << filename;

  // Convert with streaming path.
  Book book2;
  err = book2.open(epub_path.c_str());
  ASSERT_EQ(err, EpubError::Ok);
  ASSERT_TRUE(convert_epub_to_mrb_streaming(book2, mrb_streaming_.c_str()))
      << "Streaming convert failed for " << filename;

  // Open both MRBs.
  MrbReader mrb_n, mrb_s;
  ASSERT_TRUE(mrb_n.open(mrb_normal_.c_str()));
  ASSERT_TRUE(mrb_s.open(mrb_streaming_.c_str()));

  ASSERT_EQ(mrb_n.chapter_count(), mrb_s.chapter_count()) << filename << " chapter count";

  FixedFont font(16, 20);
  PageOptions opts(480, 800, 20, 16, Alignment::Start);

  size_t total_pages = 0;
  for (uint16_t ci = 0; ci < mrb_n.chapter_count(); ++ci) {
    MrbChapterSource src_n(mrb_n, ci);
    MrbChapterSource src_s(mrb_s, ci);

    ASSERT_EQ(src_n.paragraph_count(), src_s.paragraph_count()) << filename << " ch" << ci << " paragraph count";

    PagePosition pos_n{0, 0}, pos_s{0, 0};
    int page_num = 0;

    while (true) {
      auto page_n = layout_page(font, opts, src_n, pos_n);
      auto page_s = layout_page(font, opts, src_s, pos_s);

      std::string ctx = filename + " ch" + std::to_string(ci) + " page" + std::to_string(page_num);
      assert_pages_equal(page_n, page_s, ctx);

      if (page_n.at_chapter_end)
        break;

      pos_n = page_n.end;
      pos_s = page_s.end;
      ++page_num;
      ++total_pages;
      ASSERT_LT(page_num, 50000) << ctx;
    }
    total_pages++;
  }
  printf("  [%s] %d chapters, %zu pages — STREAMING MATCH\n", filename.c_str(), mrb_n.chapter_count(), total_pages);
}

INSTANTIATE_TEST_SUITE_P(AllBooks, StreamingMrbComparisonTest, ::testing::ValuesIn(test_books::get_curated_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           return test_books::epub_test_name(info.param);
                         });

// ---------------------------------------------------------------------------
// Benchmark: measure streaming conversion time per book
// ---------------------------------------------------------------------------

class ConversionBenchmark : public ::testing::TestWithParam<std::string> {
 protected:
  std::string mrb_path_;

  void SetUp() override {
    mrb_path_ = (fs::temp_directory_path() / "mrb_bench.mrb").string();
  }

  void TearDown() override {
    std::remove(mrb_path_.c_str());
    std::remove((mrb_path_ + ".idx").c_str());
  }
};

TEST_P(ConversionBenchmark, StreamingConversion) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  // --- Phase 1: Book open ---
  auto t0 = std::chrono::high_resolution_clock::now();
  Book book;
  auto err = book.open(epub_path.c_str());
  auto t1 = std::chrono::high_resolution_clock::now();
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Cannot open " << filename;

  double open_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  // --- Phase 2: Streaming conversion ---
  auto t2 = std::chrono::high_resolution_clock::now();
  bool ok = convert_epub_to_mrb_streaming(book, mrb_path_.c_str());
  auto t3 = std::chrono::high_resolution_clock::now();
  ASSERT_TRUE(ok) << "Conversion failed for " << filename;

  double convert_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
  double total_ms = std::chrono::duration<double, std::milli>(t3 - t0).count();

  // Get file sizes
  auto epub_size = fs::file_size(epub_path);
  auto mrb_size = fs::file_size(mrb_path_);

  printf("  %-45s  open=%6.1fms  convert=%7.1fms  total=%7.1fms  epub=%5.1fMB  mrb=%5.1fMB  chapters=%zu\n",
         filename.c_str(), open_ms, convert_ms, total_ms, epub_size / 1048576.0, mrb_size / 1048576.0,
         book.chapter_count());
}

INSTANTIATE_TEST_SUITE_P(AllBooks, ConversionBenchmark, ::testing::ValuesIn(test_books::get_curated_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           return test_books::epub_test_name(info.param);
                         });
