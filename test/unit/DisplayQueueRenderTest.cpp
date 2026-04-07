// DisplayQueueRenderTest.cpp
//
// Tests for display queue rendering correctness, covering:
// 1. Scratch buffer use during rendering does not corrupt ground_truth
// 2. Image rendering produces correct pixel data (no duplication/squish)
// 3. MRB chapter navigation stress (ohler-like books with many chapters)

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "TestBooks.h"
#include "microreader/content/Book.h"
#include "microreader/content/ImageDecoder.h"
#include "microreader/content/TextLayout.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/display/Display.h"
#include "microreader/display/DisplayQueue.h"

namespace fs = std::filesystem;
using namespace microreader;

// Minimal display implementation for testing — no hardware.
class TestDisplay : public IDisplay {
 public:
  int tick_count = 0;
  int refresh_count = 0;
  int partial_count = 0;

  void tick(const uint8_t* /*gt*/, bool /*gt_dirty*/, const uint8_t* /*target*/, bool /*target_dirty*/,
            bool /*refresh*/) override {
    tick_count++;
  }
  void full_refresh(const uint8_t* /*pixels*/, RefreshMode /*mode*/) override {
    refresh_count++;
  }
  void partial_refresh(const uint8_t* /*old_pixels*/, const uint8_t* /*new_pixels*/) override {
    partial_count++;
  }
  void set_rotation(Rotation /*r*/) override {}
};

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "."
#endif

static std::string fixture(const char* name) {
  return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

// ===========================================================================
// Test 1: Scratch buffer corruption during image size resolution
// ===========================================================================

class ScratchBufferCorruptionTest : public ::testing::Test {
 protected:
  TestDisplay display_;
  std::unique_ptr<DisplayQueue> queue_;

  void SetUp() override {
    queue_ = std::make_unique<DisplayQueue>(display_);
    queue_->phases = 4;
  }
};

// After submitting content and using scratch_buf1, ground_truth must NOT
// equal target. The display queue needs gt != target to drive transitions.
TEST_F(ScratchBufferCorruptionTest, ScratchBuf1RestoreDestroysGroundTruth) {
  // Submit a black rectangle — this sets target to black in that area
  // and fast-forwards ground_truth.
  queue_->submit(10, 10, 100, 50, false);

  // Capture ground_truth and target state before corruption.
  uint8_t gt_before[DisplayFrame::kPixelBytes];
  uint8_t target_before[DisplayFrame::kPixelBytes];
  std::memcpy(gt_before, queue_->scratch_buf1(), DisplayFrame::kPixelBytes);
  std::memcpy(target_before, queue_->scratch_buf2(), DisplayFrame::kPixelBytes);

  // At this point, ground_truth should NOT equal target (the black rect
  // was submitted but not yet committed via tick).
  // Note: For the very first submit (no prior commands), fast_forward is skipped,
  // but after it, there IS a command in the queue.

  // Now submit a second item — this creates a delta in ground_truth.
  queue_->submit(200, 200, 50, 50, false);

  // ground_truth should reflect fast-forward state, not target.
  uint8_t gt_after_submit[DisplayFrame::kPixelBytes];
  std::memcpy(gt_after_submit, queue_->scratch_buf1(), DisplayFrame::kPixelBytes);

  // Now simulate what the old code did: use scratch_buf1 then restore.
  // This should make ground_truth == target.
  uint8_t* scratch = queue_->scratch_buf1();
  std::memset(scratch, 0xAA, 1000);  // Corrupt some of ground_truth
  queue_->restore_scratch_buf1();    // Copy target → ground_truth

  // After restore, ground_truth should equal target.
  bool gt_equals_target = (std::memcmp(queue_->scratch_buf1(), queue_->scratch_buf2(), DisplayFrame::kPixelBytes) == 0);
  EXPECT_TRUE(gt_equals_target) << "restore_scratch_buf1() should make ground_truth == target";

  // This means the display sees no change — BAD for rendering!
  // The fix is to NOT use scratch_buf1 during active rendering.
}

// Verify that when commands are pending, submitting new content creates a
// proper ground_truth != target delta.
TEST_F(ScratchBufferCorruptionTest, SubmitCreatesProperDelta) {
  // First submit: white background
  queue_->submit(0, 0, 480, 800, true);

  // Second submit: black rectangle — should create a delta
  // (ground_truth fast-forwarded, then target changed)
  queue_->submit(100, 100, 200, 200, false);

  // The target should have a black rectangle; ground_truth should differ
  // in the region where the rect is (fast-forward sets gt to old target
  // state for changed pixels).
  uint8_t* gt = queue_->scratch_buf1();
  uint8_t* target = queue_->scratch_buf2();

  // Check a pixel inside the black rect (should differ in target vs gt)
  // In Deg90 rotation, logical (100,100) maps to physical differently.
  // But we can check that the buffers are NOT identical overall.
  bool buffers_identical = (std::memcmp(gt, target, DisplayFrame::kPixelBytes) == 0);
  // After a white bg and black rect submit, gt should NOT equal target
  // because the rect creates a delta.
  EXPECT_FALSE(buffers_identical) << "After submitting content, ground_truth should differ from target";
}

// ===========================================================================
// Test 2: Image decode does NOT corrupt display when scratch is not used
// ===========================================================================

class ImageRenderTest : public ::testing::Test {
 protected:
  TestDisplay display_;
  std::unique_ptr<DisplayQueue> queue_;

  void SetUp() override {
    queue_ = std::make_unique<DisplayQueue>(display_);
    queue_->phases = 4;
  }
};

// Decode an image from an illustrated book and verify the decoded bitmap is
// valid (no stride errors that would cause duplication/squish).
TEST_F(ImageRenderTest, DecodedImageHasCorrectDimensions) {
  // Use alice-illustrated which has known images.
  std::string path = fixture("illustrated.epub");
  if (!fs::exists(path))
    GTEST_SKIP() << "illustrated.epub fixture not available";

  Book book;
  auto err = book.open(path.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Could not open illustrated.epub";

  // Find first image in the book
  for (uint16_t ci = 0; ci < book.chapter_count(); ++ci) {
    Chapter ch;
    book.load_chapter(ci, ch);
    for (const auto& para : ch.paragraphs) {
      if (para.type == ParagraphType::Image) {
        // Decode the image
        DecodedImage decoded;
        auto img_err = book.decode_image(para.image.key, decoded, 440, 700);
        if (img_err != ImageError::Ok)
          continue;

        // Verify dimensions are reasonable
        EXPECT_GT(decoded.width, 0);
        EXPECT_GT(decoded.height, 0);
        EXPECT_LE(decoded.width, 440);
        EXPECT_LE(decoded.height, 700);

        // Verify stride is correct
        size_t expected_stride = (decoded.width + 7) / 8;
        EXPECT_EQ(decoded.stride(), expected_stride);

        // Verify data size matches
        EXPECT_EQ(decoded.data.size(), expected_stride * decoded.height);

        // Verify the image is not all-white or all-black (would indicate decode failure)
        int white_count = 0, black_count = 0;
        for (int y = 0; y < decoded.height; y += 10) {
          for (int x = 0; x < decoded.width; x += 10) {
            if (decoded.pixel(x, y))
              white_count++;
            else
              black_count++;
          }
        }
        EXPECT_GT(white_count, 0) << "Image has no white pixels";
        EXPECT_GT(black_count, 0) << "Image has no black pixels";

        // Verify no stride mismatch by checking that row N+1 doesn't
        // contain shifted copies of row N (which would cause duplication).
        if (decoded.height > 10) {
          int duplicate_rows = 0;
          for (int y = 1; y < std::min(20, (int)decoded.height); ++y) {
            size_t stride = decoded.stride();
            bool identical =
                (std::memcmp(decoded.data.data() + y * stride, decoded.data.data() + (y - 1) * stride, stride) == 0);
            if (identical)
              duplicate_rows++;
          }
          // A real image shouldn't have all consecutive rows identical
          EXPECT_LT(duplicate_rows, 18) << "Too many consecutive identical rows — possible stride bug";
        }

        return;  // One image is enough
      }
    }
  }
  GTEST_SKIP() << "No decodable images found";
}

// Submitting an image to DisplayQueue without scratch buffer corruption:
// After submit, ground_truth should properly differ from target.
TEST_F(ImageRenderTest, ImageSubmitMaintainsDelta) {
  // Create a fake decoded image (checkerboard pattern)
  DecodedImage decoded;
  decoded.width = 100;
  decoded.height = 100;
  decoded.data.resize(decoded.data_size(), 0);
  // Fill with checkerboard: alternating black/white 8px blocks
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < 100; ++x) {
      bool white = ((x / 8) + (y / 8)) % 2 == 0;
      if (white) {
        size_t byte_idx = y * decoded.stride() + x / 8;
        decoded.data[byte_idx] |= (0x80 >> (x % 8));
      }
    }
  }

  // Submit white background first
  queue_->submit(0, 0, 480, 800, true);

  // Submit the image at (50, 50) WITHOUT using scratch buffer
  int img_x = 50, img_y = 50;
  int dw = decoded.width, dh = decoded.height;
  queue_->submit(img_x, img_y, dw, dh, [&decoded, img_x, img_y](DisplayFrame& frame) {
    for (int y = 0; y < decoded.height; ++y)
      for (int x = 0; x < decoded.width; ++x)
        frame.set_pixel(img_x + x, img_y + y, decoded.pixel(x, y));
  });

  // Verify ground_truth differs from target (the image creates a delta)
  bool buffers_identical =
      (std::memcmp(queue_->scratch_buf1(), queue_->scratch_buf2(), DisplayFrame::kPixelBytes) == 0);
  EXPECT_FALSE(buffers_identical) << "After image submit, ground_truth should differ from target";
}

// ===========================================================================
// Test 3: MRB chapter navigation stress test (ohler-like many chapters)
// ===========================================================================

class MrbNavigationStressTest : public ::testing::Test {
 protected:
  std::string tmp_mrb_;

  void SetUp() override {
    tmp_mrb_ = (fs::temp_directory_path() / "nav_stress_test.mrb").string();
  }

  void TearDown() override {
    std::remove(tmp_mrb_.c_str());
  }
};

// Navigate through all chapters and pages of an MRB book without crashing.
// This specifically catches issues with rapid chapter transitions and
// MrbChapterSource construction/destruction.
static void navigate_all_pages(const std::string& mrb_path, int max_pages = 2000) {
  MrbReader mrb;
  ASSERT_TRUE(mrb.open(mrb_path.c_str())) << "Failed to open MRB: " << mrb_path;

  FixedFont font(16, 20);
  PageOptions opts(480, 800, 20, 12, Alignment::Start);
  opts.padding_top = 40;

  // Image size cache
  std::vector<std::pair<uint16_t, uint16_t>> img_cache(mrb.image_count(), {0, 0});

  auto resolve_size = [&](uint16_t key, uint16_t& w, uint16_t& h) -> bool {
    if (key >= img_cache.size())
      return false;
    auto [cw, ch] = img_cache[key];
    if (cw != 0 || ch != 0) {
      w = cw;
      h = ch;
      return true;
    }
    // Check MRB image ref table
    const auto& ref = mrb.image_ref(key);
    if (ref.width != 0 || ref.height != 0) {
      w = ref.width;
      h = ref.height;
      img_cache[key] = {w, h};
      return true;
    }
    // For testing, return a dummy size (we can't open the EPUB here).
    w = 200;
    h = 200;
    img_cache[key] = {w, h};
    return true;
  };

  int total_pages = 0;
  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);

    if (src.paragraph_count() == 0) {
      // Empty chapter — skip but count it
      continue;
    }

    PagePosition pos{0, 0};
    int chapter_pages = 0;

    while (true) {
      auto page = layout_page(font, opts, src, pos, resolve_size);

      ASSERT_LE(page.text_items.size(), 10000u)
          << "Suspiciously many text items on page " << total_pages << " ch" << ci;

      if (page.at_chapter_end)
        break;

      pos = page.end;
      ++chapter_pages;
      ++total_pages;

      ASSERT_LT(chapter_pages, 5000) << "Too many pages in chapter " << ci << " (safety limit)";
      ASSERT_LT(total_pages, max_pages) << "Reached page limit — test passed enough pages";
    }
    ++total_pages;
  }

  EXPECT_GT(total_pages, 0) << "Book has no content";
}

// Test navigation through a book with many short chapters (synthetic fixture).
TEST_F(MrbNavigationStressTest, SyntheticManyChapters) {
  std::string epub = fixture("multi_chapter.epub");
  if (!fs::exists(epub))
    GTEST_SKIP() << "multi_chapter.epub fixture not available";

  Book book;
  auto err = book.open(epub.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Could not open multi_chapter.epub";

  ASSERT_TRUE(convert_epub_to_mrb(book, tmp_mrb_.c_str()));
  book.close();

  navigate_all_pages(tmp_mrb_);
}

// Test navigation through a simple book with images.
TEST_F(MrbNavigationStressTest, ImageBook) {
  std::string epub = fixture("illustrated.epub");
  if (!fs::exists(epub))
    GTEST_SKIP() << "illustrated.epub fixture not available";

  Book book;
  auto err = book.open(epub.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Could not open illustrated.epub";

  ASSERT_TRUE(convert_epub_to_mrb(book, tmp_mrb_.c_str()));
  book.close();

  navigate_all_pages(tmp_mrb_);
}

// ===========================================================================
// Test 4: Real book navigation tests (integration, requires test/books/)
// ===========================================================================

#ifndef SMOKE_TESTS_ONLY

class RealBookNavigationTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string tmp_mrb_;

  void SetUp() override {
    tmp_mrb_ = (fs::temp_directory_path() / "real_nav_test.mrb").string();
  }
  void TearDown() override {
    std::remove(tmp_mrb_.c_str());
  }
};

TEST_P(RealBookNavigationTest, NavigateAllPages) {
  const std::string& epub_path = GetParam();
  if (!fs::exists(epub_path))
    GTEST_SKIP() << "Book not found: " << epub_path;

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Could not open: " << epub_path;

  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, tmp_mrb_.c_str()));
  book.close();

  navigate_all_pages(tmp_mrb_, 20000);
}

// Include ohler and bobiverse specifically
static std::vector<std::string> get_navigation_test_books() {
  std::string root = test_books::workspace_root();
  std::vector<std::string> books;

  // Priority: use sd/books versions (what the device actually runs).
  auto add_if_exists = [&](const char* rel_path) {
    std::string path = root + "/" + rel_path;
    if (fs::exists(path))
      books.push_back(path);
  };

  add_if_exists("microreader2/sd/books/ohler.epub");
  add_if_exists("microreader2/sd/books/bobiverse one.epub");

  // Also add curated books, skipping any with duplicate stems.
  std::set<std::string> seen_stems;
  for (const auto& b : books)
    seen_stems.insert(fs::path(b).stem().string());

  for (const auto& b : test_books::get_curated_books()) {
    auto stem = fs::path(b).stem().string();
    if (seen_stems.count(stem) == 0) {
      seen_stems.insert(stem);
      books.push_back(b);
    }
  }
  return books;
}

INSTANTIATE_TEST_SUITE_P(RealBooks, RealBookNavigationTest, ::testing::ValuesIn(get_navigation_test_books()),
                         [](const auto& info) { return test_books::epub_test_name(info.param); });

#endif  // SMOKE_TESTS_ONLY

// ===========================================================================
// Test 5: DisplayQueue flush + partial_refresh workflow
// ===========================================================================

class PageTransitionTest : public ::testing::Test {
 protected:
  TestDisplay display_;
  std::unique_ptr<DisplayQueue> queue_;

  void SetUp() override {
    queue_ = std::make_unique<DisplayQueue>(display_);
    queue_->phases = 4;
  }
};

// Simulate rendering two pages in sequence and verify the display is properly
// refreshed between them (via partial_refresh or tick-based animation).
TEST_F(PageTransitionTest, TwoPageRenderMaintainsDelta) {
  // Page 1: white background + some black text
  queue_->submit(0, 0, 480, 800, true);
  queue_->submit(20, 40, 200, 16, false);

  // Flush page 1 — all commands committed
  queue_->flush();

  // After flush, ground_truth == target (all committed)
  bool identical_after_flush =
      (std::memcmp(queue_->scratch_buf1(), queue_->scratch_buf2(), DisplayFrame::kPixelBytes) == 0);
  EXPECT_TRUE(identical_after_flush) << "After flush, gt should equal target";

  // Page 2: submit new content
  queue_->submit(0, 0, 480, 800, true);
  queue_->submit(20, 100, 300, 16, false);  // Different position

  // After rendering page 2, ground_truth should NOT equal target
  // (old committed state vs new submitted state).
  bool identical_after_render2 =
      (std::memcmp(queue_->scratch_buf1(), queue_->scratch_buf2(), DisplayFrame::kPixelBytes) == 0);
  EXPECT_FALSE(identical_after_render2) << "After submitting page 2, gt should differ from target "
                                           "(gt=page1, target=page2)";

  // Now do partial_refresh — this sends the delta to the display
  queue_->partial_refresh();
  EXPECT_EQ(display_.partial_count, 1);

  // After partial_refresh, gt == target again
  bool identical_after_refresh =
      (std::memcmp(queue_->scratch_buf1(), queue_->scratch_buf2(), DisplayFrame::kPixelBytes) == 0);
  EXPECT_TRUE(identical_after_refresh) << "After partial_refresh, gt should equal target";
}

// Verify that rendering with image decode (NO scratch buffer) maintains
// proper display state across page transitions.
TEST_F(PageTransitionTest, ImagePageWithoutScratchMaintainsDelta) {
  // Page 1: white + text
  queue_->submit(0, 0, 480, 800, true);
  queue_->submit(20, 40, 200, 16, false);

  // Flush page 1
  queue_->flush();

  // Page 2: white + image (simulate decode without using scratch)
  queue_->submit(0, 0, 480, 800, true);

  // Create a small fake image
  DecodedImage decoded;
  decoded.width = 50;
  decoded.height = 50;
  decoded.data.resize(decoded.data_size(), 0);  // all black

  int img_x = 100, img_y = 200;
  queue_->submit(img_x, img_y, decoded.width, decoded.height, [&decoded, img_x, img_y](DisplayFrame& frame) {
    for (int y = 0; y < decoded.height; ++y)
      for (int x = 0; x < decoded.width; ++x)
        frame.set_pixel(img_x + x, img_y + y, decoded.pixel(x, y));
  });

  // gt should differ from target (page 1 committed vs page 2 in target)
  bool identical = (std::memcmp(queue_->scratch_buf1(), queue_->scratch_buf2(), DisplayFrame::kPixelBytes) == 0);
  EXPECT_FALSE(identical) << "After submitting page 2 with image, gt should differ from target";

  // Partial refresh should work
  queue_->partial_refresh();
  EXPECT_EQ(display_.partial_count, 1);
}
