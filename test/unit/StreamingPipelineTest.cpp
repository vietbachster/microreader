// StreamingPipelineTest.cpp — intermediate-level tests that verify each stage
// of the streaming pipeline matches the non-streaming (full-extract) path.
//
// Level 1: ZipEntryInput read-all vs ZipReader::extract (raw bytes)
// Level 2: parse_chapter_streaming paragraphs vs parse_chapter paragraphs
//
// These help isolate *where* in the pipeline data gets lost when the
// end-to-end StreamingMrbComparisonTest fails.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "microreader/content/Book.h"
#include "microreader/content/EpubParser.h"
#include "microreader/content/ZipReader.h"

namespace fs = std::filesystem;
using namespace microreader;

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "."
#endif

// ---------------------------------------------------------------------------
// Shared helpers (same discovery logic as MrbLayoutComparisonTest)
// ---------------------------------------------------------------------------

static std::string workspace_root() {
  std::string fixtures = TEST_FIXTURES_DIR;
  auto pos = fixtures.rfind('/');
  if (pos == std::string::npos)
    pos = fixtures.rfind('\\');
  std::string up1 = fixtures.substr(0, pos);
  pos = up1.rfind('/');
  if (pos == std::string::npos)
    pos = up1.rfind('\\');
  std::string up2 = up1.substr(0, pos);
  pos = up2.rfind('/');
  if (pos == std::string::npos)
    pos = up2.rfind('\\');
  return up2.substr(0, pos);
}

static std::vector<std::string> discover_epubs(const std::string& dir) {
  std::vector<std::string> result;
  if (!fs::exists(dir))
    return result;
  for (auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".epub")
        result.push_back(entry.path().string());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

static std::vector<std::string> get_all_test_books() {
  std::string root = workspace_root();
  // Curated list — same 15 books as MrbLayoutComparisonTest.
  static const char* kBooks[] = {
      "gutenberg/alice-wonderland.epub",
      "gutenberg/frankenstein.epub",
      "gutenberg/pride-prejudice.epub",
      "gutenberg/moby-dick.epub",
      "gutenberg/dracula.epub",
      "gutenberg/adventures-tom-sawyer.epub",
      "gutenberg/complete-shakespeare.epub",
      "gutenberg/origin-species-darwin.epub",
      "gutenberg/alice-illustrated.epub",
      "gutenberg/war-and-peace.epub",
      "gutenberg/heart-darkness.epub",
      "gutenberg/metamorphosis-kafka.epub",
      "gutenberg/ulysses-joyce.epub",
      "other/buddenbrooks-de.epub",
      "other/divina-commedia-it.epub",
  };
  std::vector<std::string> all;
  std::string base = root + "/microreader2/test/books/";
  for (auto& b : kBooks) {
    std::string path = base + b;
    if (fs::exists(path))
      all.push_back(path);
  }
  return all;
}

// ===========================================================================
// Level 1: ZipEntryInput read-all vs ZipReader::extract
// Verifies that streaming decompression produces byte-identical output
// to one-shot extraction for every XHTML entry in a book.
// ===========================================================================

class ZipStreamingTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ZipStreamingTest, AllXhtmlEntriesMatch) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  StdioZipFile file;
  ASSERT_TRUE(file.open(epub_path.c_str())) << filename;

  ZipReader zip;
  ASSERT_EQ(zip.open(file), ZipError::Ok) << filename;

  static constexpr size_t kWorkBufSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 1024;
  std::vector<uint8_t> work_buf(kWorkBufSize);

  size_t tested = 0;
  for (size_t i = 0; i < zip.entry_count(); ++i) {
    auto& entry = zip.entry(i);
    auto name = std::string(entry.name);

    // Only test XHTML/HTML entries (the ones parsed by the streaming converter).
    bool is_xhtml =
        name.size() > 4 && (name.find(".xhtml") != std::string::npos || name.find(".html") != std::string::npos ||
                            name.find(".htm") != std::string::npos);
    if (!is_xhtml)
      continue;

    // One-shot extract
    std::vector<uint8_t> expected;
    ASSERT_EQ(zip.extract(file, entry, expected), ZipError::Ok) << filename << " extract failed: " << name;

    // Streaming read-all via ZipEntryInput
    ZipEntryInput input;
    ASSERT_EQ(input.open(file, entry, work_buf.data(), work_buf.size()), ZipError::Ok)
        << filename << " ZipEntryInput::open failed: " << name;

    std::vector<uint8_t> streamed;
    streamed.reserve(entry.uncompressed_size);
    uint8_t chunk[4096];
    for (;;) {
      size_t n = input.read(chunk, sizeof(chunk));
      if (n == 0)
        break;
      streamed.insert(streamed.end(), chunk, chunk + n);
    }

    EXPECT_FALSE(input.has_error()) << filename << " " << name << " ZipEntryInput error after read";
    ASSERT_EQ(streamed.size(), expected.size())
        << filename << " " << name << " size mismatch"
        << " (streamed=" << streamed.size() << " expected=" << expected.size() << ")";
    EXPECT_EQ(streamed, expected) << filename << " " << name << " content mismatch";

    ++tested;
  }

  printf("  [%s] %zu XHTML entries — ZIP STREAMING OK\n", filename.c_str(), tested);
}

INSTANTIATE_TEST_SUITE_P(AllBooks, ZipStreamingTest, ::testing::ValuesIn(get_all_test_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           auto name = fs::path(info.param).stem().string();
                           for (auto& c : name)
                             if (!std::isalnum(static_cast<unsigned char>(c)))
                               c = '_';
                           return name;
                         });

// ===========================================================================
// Level 2: parse_chapter_streaming paragraphs vs parse_chapter
// Verifies that the streaming XHTML→Paragraph pipeline produces the
// same paragraph sequence as the non-streaming path (before MRB encoding).
// ===========================================================================

class ChapterStreamingTest : public ::testing::TestWithParam<std::string> {};

namespace {

// Collect paragraphs from the streaming sink into a vector.
struct CollectCtx {
  std::vector<Paragraph> paragraphs;
};

void collect_sink(void* raw, Paragraph&& para) {
  auto& ctx = *static_cast<CollectCtx*>(raw);
  ctx.paragraphs.push_back(std::move(para));
}

// Compare two paragraphs for equality (ignoring image key remapping).
void assert_paragraph_equal(const Paragraph& a, const Paragraph& b, const std::string& ctx) {
  ASSERT_EQ(a.type, b.type) << ctx << " type";

  if (a.type == ParagraphType::Text) {
    ASSERT_EQ(a.text.runs.size(), b.text.runs.size()) << ctx << " run count";
    for (size_t ri = 0; ri < a.text.runs.size(); ++ri) {
      std::string rctx = ctx + " run[" + std::to_string(ri) + "]";
      EXPECT_EQ(a.text.runs[ri].text, b.text.runs[ri].text) << rctx << " text";
      EXPECT_EQ(a.text.runs[ri].style, b.text.runs[ri].style) << rctx << " style";
      EXPECT_EQ(a.text.runs[ri].size, b.text.runs[ri].size) << rctx << " size";
      EXPECT_EQ(a.text.runs[ri].vertical_align, b.text.runs[ri].vertical_align) << rctx << " valign";
      EXPECT_EQ(a.text.runs[ri].breaking, b.text.runs[ri].breaking) << rctx << " breaking";
      EXPECT_EQ(a.text.runs[ri].margin_left, b.text.runs[ri].margin_left) << rctx << " margin_left";
      EXPECT_EQ(a.text.runs[ri].margin_right, b.text.runs[ri].margin_right) << rctx << " margin_right";
    }
    EXPECT_EQ(a.text.alignment.has_value(), b.text.alignment.has_value()) << ctx << " alignment presence";
    if (a.text.alignment.has_value() && b.text.alignment.has_value())
      EXPECT_EQ(*a.text.alignment, *b.text.alignment) << ctx << " alignment";
    EXPECT_EQ(a.text.indent.has_value(), b.text.indent.has_value()) << ctx << " indent presence";
    if (a.text.indent.has_value() && b.text.indent.has_value())
      EXPECT_EQ(*a.text.indent, *b.text.indent) << ctx << " indent";
    EXPECT_EQ(a.text.line_height_pct, b.text.line_height_pct) << ctx << " line_height_pct";

    // Inline image: compare dimensions (key may differ if image resolution differed)
    EXPECT_EQ(a.text.inline_image.has_value(), b.text.inline_image.has_value()) << ctx << " inline_image presence";
    if (a.text.inline_image.has_value() && b.text.inline_image.has_value()) {
      EXPECT_EQ(a.text.inline_image->key, b.text.inline_image->key) << ctx << " inline_image key";
      EXPECT_EQ(a.text.inline_image->width, b.text.inline_image->width) << ctx << " inline_image width";
      EXPECT_EQ(a.text.inline_image->height, b.text.inline_image->height) << ctx << " inline_image height";
    }
  } else if (a.type == ParagraphType::Image) {
    EXPECT_EQ(a.image.key, b.image.key) << ctx << " image key";
    EXPECT_EQ(a.image.width, b.image.width) << ctx << " image width";
    EXPECT_EQ(a.image.height, b.image.height) << ctx << " image height";
  }
  // Hr and PageBreak have no additional data.

  EXPECT_EQ(a.spacing_before.has_value(), b.spacing_before.has_value()) << ctx << " spacing_before presence";
  if (a.spacing_before.has_value() && b.spacing_before.has_value())
    EXPECT_EQ(*a.spacing_before, *b.spacing_before) << ctx << " spacing_before";
}

}  // namespace

TEST_P(ChapterStreamingTest, ParagraphsMatch) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Cannot open " << filename;

  size_t total_paras = 0;
  for (size_t ci = 0; ci < book.chapter_count(); ++ci) {
    // Non-streaming
    Chapter ch;
    EpubError cerr = book.load_chapter(ci, ch);
    if (cerr != EpubError::Ok)
      continue;

    // Streaming (now includes image resolution + promotion, same as parse_chapter)
    CollectCtx streaming;
    EpubError serr = book.load_chapter_streaming(ci, collect_sink, &streaming);
    ASSERT_EQ(serr, EpubError::Ok) << filename << " ch" << ci << " streaming parse failed";

    std::string cctx = filename + " ch" + std::to_string(ci);
    ASSERT_EQ(ch.paragraphs.size(), streaming.paragraphs.size())
        << cctx << " paragraph count"
        << " (normal=" << ch.paragraphs.size() << " streaming=" << streaming.paragraphs.size() << ")";

    for (size_t pi = 0; pi < ch.paragraphs.size(); ++pi) {
      std::string pctx = cctx + " p" + std::to_string(pi);
      assert_paragraph_equal(ch.paragraphs[pi], streaming.paragraphs[pi], pctx);
    }

    total_paras += ch.paragraphs.size();
  }

  printf("  [%s] %zu chapters, %zu paragraphs — CHAPTER STREAMING OK\n", filename.c_str(), book.chapter_count(),
         total_paras);
}

INSTANTIATE_TEST_SUITE_P(AllBooks, ChapterStreamingTest, ::testing::ValuesIn(get_all_test_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           auto name = fs::path(info.param).stem().string();
                           for (auto& c : name)
                             if (!std::isalnum(static_cast<unsigned char>(c)))
                               c = '_';
                           return name;
                         });
