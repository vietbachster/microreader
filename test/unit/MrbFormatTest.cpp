#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/content/mrb/MrbWriter.h"

namespace fs = std::filesystem;
using namespace microreader;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

class MrbFormatTest : public ::testing::Test {
 protected:
  std::string tmp_path_;

  void SetUp() override {
    tmp_path_ = (fs::temp_directory_path() / "mrb_test.mrb").string();
  }

  void TearDown() override {
    std::remove(tmp_path_.c_str());
  }

  // Build a simple text paragraph with one run.
  static Paragraph make_text(const std::string& text, FontStyle style = FontStyle::Regular) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run{text, style});
    return Paragraph::make_text(std::move(tp));
  }

  // Compare two runs field by field.
  static void expect_run_eq(const microreader::Run& a, const microreader::Run& b, const std::string& ctx) {
    EXPECT_EQ(a.text, b.text) << ctx;
    EXPECT_EQ(a.style, b.style) << ctx;
    EXPECT_EQ(a.size, b.size) << ctx;
    EXPECT_EQ(a.vertical_align, b.vertical_align) << ctx;
    EXPECT_EQ(a.breaking, b.breaking) << ctx;
    EXPECT_EQ(a.margin_left, b.margin_left) << ctx;
    EXPECT_EQ(a.margin_right, b.margin_right) << ctx;
  }

  // Compare two paragraphs field by field.
  static void expect_paragraph_eq(const Paragraph& a, const Paragraph& b, const std::string& ctx) {
    EXPECT_EQ(a.type, b.type) << ctx;
    EXPECT_EQ(a.spacing_before, b.spacing_before) << ctx;

    if (a.type == ParagraphType::Text) {
      EXPECT_EQ(a.text.alignment, b.text.alignment) << ctx;
      EXPECT_EQ(a.text.indent, b.text.indent) << ctx;
      EXPECT_EQ(a.text.line_height_pct, b.text.line_height_pct) << ctx;
      EXPECT_EQ(a.text.inline_image.has_value(), b.text.inline_image.has_value()) << ctx;
      if (a.text.inline_image.has_value() && b.text.inline_image.has_value()) {
        EXPECT_EQ(a.text.inline_image->key, b.text.inline_image->key) << ctx;
        EXPECT_EQ(a.text.inline_image->attr_width, b.text.inline_image->attr_width) << ctx;
        EXPECT_EQ(a.text.inline_image->attr_height, b.text.inline_image->attr_height) << ctx;
      }
      ASSERT_EQ(a.text.runs.size(), b.text.runs.size()) << ctx;
      for (size_t i = 0; i < a.text.runs.size(); ++i) {
        expect_run_eq(a.text.runs[i], b.text.runs[i], ctx + " run[" + std::to_string(i) + "]");
      }
    } else if (a.type == ParagraphType::Image) {
      EXPECT_EQ(a.image.key, b.image.key) << ctx;
    }
  }

  // Load the nth paragraph of a chapter by traversing the linked list.
  static bool load_chapter_para(MrbReader& reader, uint16_t chapter_idx, uint32_t para_idx, Paragraph& out) {
    uint32_t offset = reader.chapter_first_offset(chapter_idx);
    for (uint32_t i = 0; i < para_idx && offset != 0; ++i) {
      Paragraph tmp;
      auto lr = reader.load_paragraph(offset, tmp);
      if (!lr.ok)
        return false;
      offset = lr.next_offset;
    }
    if (offset == 0)
      return false;
    auto lr = reader.load_paragraph(offset, out);
    return lr.ok;
  }
};

// ---------------------------------------------------------------------------
// Round-trip: write paragraphs → read back, compare
// ---------------------------------------------------------------------------

TEST_F(MrbFormatTest, RoundTrip_TextParagraph) {
  TextParagraph tp;
  tp.alignment = Alignment::Center;
  tp.indent = -24;
  tp.line_height_pct = 120;
  tp.runs.push_back(microreader::Run{"Hello, ", FontStyle::Regular});
  tp.runs.push_back(microreader::Run{"world!", FontStyle::Bold});
  Paragraph para = Paragraph::make_text(std::move(tp));
  para.spacing_before = 16;

  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));
  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(para));
  writer.end_chapter();

  EpubMetadata meta;
  meta.title = "Test Book";
  meta.author = "Author";
  meta.language = "en";
  TableOfContents toc;
  ASSERT_TRUE(writer.finish(meta, toc));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_EQ(reader.paragraph_count(), 1u);
  EXPECT_EQ(reader.chapter_count(), 1);

  Paragraph out;
  ASSERT_TRUE(load_chapter_para(reader, 0, 0, out));
  expect_paragraph_eq(para, out, "text paragraph");
}

TEST_F(MrbFormatTest, RoundTrip_ImageParagraph) {
  Paragraph para = Paragraph::make_image(42, 320, 240);
  para.spacing_before = 8;

  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));
  writer.begin_chapter();
  // Register image in image ref table (converter would do this).
  writer.add_image_ref(42, 320, 240);
  ASSERT_TRUE(writer.write_paragraph(para));
  writer.end_chapter();

  EpubMetadata meta;
  meta.title = "Img Book";
  ASSERT_TRUE(writer.finish(meta, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));

  Paragraph out;
  ASSERT_TRUE(load_chapter_para(reader, 0, 0, out));
  EXPECT_EQ(out.type, ParagraphType::Image);
  // Image key in the written paragraph was 42 (raw key, not remapped here).
  // The reader looks up dimensions from the image ref table by key index.
  // Since key=42 is out of range for image table (only 1 entry at index 0),
  // dimensions come from the stored 4-byte body which just has key+spacing.
  // For a proper round-trip the converter remaps keys to image table indices.
  EXPECT_EQ(out.spacing_before, para.spacing_before);
}

TEST_F(MrbFormatTest, RoundTrip_HrAndPageBreak) {
  Paragraph hr = Paragraph::make_hr();
  hr.spacing_before = 20;
  Paragraph pb = Paragraph::make_page_break();

  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));
  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(hr));
  ASSERT_TRUE(writer.write_paragraph(pb));
  writer.end_chapter();

  ASSERT_TRUE(writer.finish({}, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_EQ(reader.paragraph_count(), 2u);

  Paragraph out_hr, out_pb;
  ASSERT_TRUE(load_chapter_para(reader, 0, 0, out_hr));
  ASSERT_TRUE(load_chapter_para(reader, 0, 1, out_pb));

  EXPECT_EQ(out_hr.type, ParagraphType::Hr);
  EXPECT_EQ(out_hr.spacing_before, hr.spacing_before);
  EXPECT_EQ(out_pb.type, ParagraphType::PageBreak);
}

TEST_F(MrbFormatTest, RoundTrip_MultipleChapters) {
  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));

  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(make_text("Chapter 1 para 1")));
  ASSERT_TRUE(writer.write_paragraph(make_text("Chapter 1 para 2")));
  writer.end_chapter();

  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(make_text("Chapter 2 para 1")));
  writer.end_chapter();

  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(make_text("Chapter 3 para 1")));
  ASSERT_TRUE(writer.write_paragraph(make_text("Chapter 3 para 2")));
  ASSERT_TRUE(writer.write_paragraph(make_text("Chapter 3 para 3")));
  writer.end_chapter();

  ASSERT_TRUE(writer.finish({}, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_EQ(reader.chapter_count(), 3);
  EXPECT_EQ(reader.paragraph_count(), 6u);

  EXPECT_NE(reader.chapter_first_offset(0), 0u);
  EXPECT_EQ(reader.chapter_paragraph_count(0), 2);
  EXPECT_NE(reader.chapter_first_offset(1), 0u);
  EXPECT_EQ(reader.chapter_paragraph_count(1), 1);
  EXPECT_NE(reader.chapter_first_offset(2), 0u);
  EXPECT_EQ(reader.chapter_paragraph_count(2), 3);

  Paragraph out;
  ASSERT_TRUE(load_chapter_para(reader, 1, 0, out));
  EXPECT_EQ(out.type, ParagraphType::Text);
  ASSERT_EQ(out.text.runs.size(), 1u);
  EXPECT_EQ(out.text.runs[0].text, "Chapter 2 para 1");
}

TEST_F(MrbFormatTest, RoundTrip_AllRunStyles) {
  TextParagraph tp;
  tp.alignment = Alignment::Justify;
  tp.indent = 30;

  microreader::Run r1{"Regular text ", FontStyle::Regular, FontSize::Normal};
  microreader::Run r2{"Bold text ", FontStyle::Bold, FontSize::Large};
  r2.margin_left = 12;
  microreader::Run r3{"Italic small", FontStyle::Italic, FontSize::Small};
  r3.margin_right = 8;
  r3.vertical_align = VerticalAlign::Super;
  microreader::Run r4{"BoldItalic break", FontStyle::BoldItalic, FontSize::Normal, true};
  r4.vertical_align = VerticalAlign::Sub;

  tp.runs = {r1, r2, r3, r4};
  Paragraph para = Paragraph::make_text(std::move(tp));

  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));
  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(para));
  writer.end_chapter();
  ASSERT_TRUE(writer.finish({}, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));

  Paragraph out;
  ASSERT_TRUE(load_chapter_para(reader, 0, 0, out));
  expect_paragraph_eq(para, out, "all styles");
}

TEST_F(MrbFormatTest, RoundTrip_InlineImage) {
  TextParagraph tp;
  tp.inline_image = ImageRef{5, 100, 80};
  tp.runs.push_back(microreader::Run{"Text after image"});
  Paragraph para = Paragraph::make_text(std::move(tp));

  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));
  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(para));
  writer.end_chapter();
  ASSERT_TRUE(writer.finish({}, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));

  Paragraph out;
  ASSERT_TRUE(load_chapter_para(reader, 0, 0, out));
  ASSERT_TRUE(out.text.inline_image.has_value());
  EXPECT_EQ(out.text.inline_image->key, 5);
  EXPECT_EQ(out.text.inline_image->attr_width, 100);
  EXPECT_EQ(out.text.inline_image->attr_height, 80);
}

TEST_F(MrbFormatTest, RoundTrip_Metadata) {
  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));
  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(make_text("hello")));
  writer.end_chapter();

  EpubMetadata meta;
  meta.title = "My Great Book";
  meta.author = "Jane Doe";
  meta.language = "de";
  TableOfContents toc;
  toc.entries.push_back({"Chapter One", 0});
  toc.entries.push_back({"Chapter Two", 5});
  ASSERT_TRUE(writer.finish(meta, toc));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_EQ(reader.metadata().title, "My Great Book");
  EXPECT_EQ(reader.metadata().author.value_or(""), "Jane Doe");
  EXPECT_EQ(reader.metadata().language.value_or(""), "de");

  ASSERT_EQ(reader.toc().entries.size(), 2u);
  EXPECT_EQ(reader.toc().entries[0].label, "Chapter One");
  EXPECT_EQ(reader.toc().entries[0].file_idx, 0);
  EXPECT_EQ(reader.toc().entries[1].label, "Chapter Two");
  EXPECT_EQ(reader.toc().entries[1].file_idx, 5);
}

TEST_F(MrbFormatTest, RoundTrip_EmptyChapter) {
  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));

  writer.begin_chapter();
  writer.end_chapter();  // empty chapter

  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(make_text("content")));
  writer.end_chapter();

  ASSERT_TRUE(writer.finish({}, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_EQ(reader.chapter_count(), 2);
  EXPECT_EQ(reader.chapter_paragraph_count(0), 0);
  EXPECT_EQ(reader.chapter_paragraph_count(1), 1);
}

TEST_F(MrbFormatTest, RoundTrip_UnicodeText) {
  auto para = make_text("Ünïcödë Hëllö Wörld — 日本語テスト");

  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));
  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(para));
  writer.end_chapter();
  ASSERT_TRUE(writer.finish({}, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  Paragraph out;
  ASSERT_TRUE(load_chapter_para(reader, 0, 0, out));
  EXPECT_EQ(out.text.runs[0].text, "Ünïcödë Hëllö Wörld — 日本語テスト");
}

TEST_F(MrbFormatTest, RoundTrip_ImageRefTable) {
  MrbWriter writer;
  ASSERT_TRUE(writer.open(tmp_path_.c_str()));

  uint16_t idx0 = writer.add_image_ref(1000, 640, 480);
  uint16_t idx1 = writer.add_image_ref(2500, 100, 100);
  uint16_t idx2 = writer.add_image_ref(30000, 800, 600);
  EXPECT_EQ(idx0, 0);
  EXPECT_EQ(idx1, 1);
  EXPECT_EQ(idx2, 2);

  writer.begin_chapter();
  ASSERT_TRUE(writer.write_paragraph(make_text("placeholder")));
  writer.end_chapter();
  ASSERT_TRUE(writer.finish({}, {}));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_EQ(reader.image_count(), 3);

  EXPECT_EQ(reader.image_ref(0).local_header_offset, 1000u);
  EXPECT_EQ(reader.image_ref(0).width, 640);
  EXPECT_EQ(reader.image_ref(0).height, 480);
  EXPECT_EQ(reader.image_ref(1).local_header_offset, 2500u);
  EXPECT_EQ(reader.image_ref(2).width, 800);
}

// ---------------------------------------------------------------------------
// EPUB → MRB conversion test (using fixture EPUBs)
// ---------------------------------------------------------------------------

#ifdef TEST_FIXTURES_DIR
static std::string fixture_path(const char* name) {
  return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

TEST_F(MrbFormatTest, ConvertBasicEpub) {
  Book book;
  auto err = book.open(fixture_path("basic.epub").c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "basic.epub not found";

  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, tmp_path_.c_str()));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_GT(reader.paragraph_count(), 0u);
  EXPECT_GT(reader.chapter_count(), 0);

  // Verify we can load every paragraph by traversing chapter linked lists.
  for (uint16_t ci = 0; ci < reader.chapter_count(); ++ci) {
    uint32_t offset = reader.chapter_first_offset(ci);
    for (uint16_t pi = 0; pi < reader.chapter_paragraph_count(ci); ++pi) {
      Paragraph p;
      auto lr = reader.load_paragraph(offset, p);
      ASSERT_TRUE(lr.ok) << "Failed to load ch " << ci << " para " << pi;
      offset = lr.next_offset;
    }
  }
}

TEST_F(MrbFormatTest, ConvertMultiChapterEpub) {
  Book book;
  auto err = book.open(fixture_path("multi_chapter.epub").c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "multi_chapter.epub not found";

  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, tmp_path_.c_str()));

  MrbReader reader;
  ASSERT_TRUE(reader.open(tmp_path_.c_str()));
  EXPECT_EQ(reader.chapter_count(), static_cast<uint16_t>(book.chapter_count()));

  // Verify paragraph counts match per chapter.
  for (uint16_t ci = 0; ci < reader.chapter_count(); ++ci) {
    Chapter ch;
    book.load_chapter(ci, ch);

    uint16_t mrb_count = reader.chapter_paragraph_count(ci);
    EXPECT_EQ(mrb_count, static_cast<uint16_t>(ch.paragraphs.size()))
        << "Chapter " << ci << " paragraph count mismatch";
  }
}
#endif

// ---------------------------------------------------------------------------
// Verify that Book + MrbReader can be re-opened without leaking memory.
// Simulates the ReaderScreen flow: open book A → convert → read MRB → close
// → open book B → convert → read MRB.
// ---------------------------------------------------------------------------

#ifdef TEST_FIXTURES_DIR
TEST_F(MrbFormatTest, ReopenBookReleasesResources) {
  auto path_a = fixture_path("basic.epub");
  auto path_b = fixture_path("multi_chapter.epub");
  auto mrb_a = (fs::temp_directory_path() / "reopen_a.mrb").string();
  auto mrb_b = (fs::temp_directory_path() / "reopen_b.mrb").string();

  // ---- Round 1: open book A ----
  {
    Book book;
    auto err = book.open(path_a.c_str());
    if (err != EpubError::Ok)
      GTEST_SKIP() << "basic.epub not found";
    ASSERT_GT(book.chapter_count(), 0u);

    ASSERT_TRUE(convert_epub_to_mrb_streaming(book, mrb_a.c_str()));

    MrbReader reader;
    ASSERT_TRUE(reader.open(mrb_a.c_str()));
    EXPECT_GT(reader.paragraph_count(), 0u);
    uint16_t ch_count_a = reader.chapter_count();
    EXPECT_GT(ch_count_a, 0);
  }
  // Book + MrbReader destructors run here — all resources released.

  // ---- Round 2: open book B ----
  {
    Book book;
    auto err = book.open(path_b.c_str());
    if (err != EpubError::Ok)
      GTEST_SKIP() << "multi_chapter.epub not found";
    ASSERT_GT(book.chapter_count(), 0u);

    ASSERT_TRUE(convert_epub_to_mrb_streaming(book, mrb_b.c_str()));

    MrbReader reader;
    ASSERT_TRUE(reader.open(mrb_b.c_str()));
    EXPECT_GT(reader.paragraph_count(), 0u);
    EXPECT_GT(reader.chapter_count(), 0);

    // Verify we can actually read paragraphs from book B.
    Paragraph p;
    auto lr = reader.load_paragraph(reader.chapter_first_offset(0), p);
    ASSERT_TRUE(lr.ok);
    EXPECT_EQ(p.type, ParagraphType::Text);
  }

  std::remove(mrb_a.c_str());
  std::remove(mrb_b.c_str());
}

// Verify Book::open can be called twice on the same instance (re-open).
TEST_F(MrbFormatTest, BookReopenSameInstance) {
  auto path_a = fixture_path("basic.epub");
  auto path_b = fixture_path("multi_chapter.epub");

  Book book;
  auto err = book.open(path_a.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "basic.epub not found";
  size_t count_a = book.chapter_count();
  ASSERT_GT(count_a, 0u);

  // Re-open with a different book on the same Book instance.
  err = book.open(path_b.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "multi_chapter.epub not found";
  size_t count_b = book.chapter_count();
  ASSERT_GT(count_b, 0u);

  // Verify chapter count changed (these are different books).
  // The important thing is that it doesn't crash.
  EXPECT_NE(count_a, count_b);
}
#endif

// ---------------------------------------------------------------------------
// Corruption / robustness: MrbReader::open() must fail gracefully.
// ---------------------------------------------------------------------------

// Opening a nonexistent file should return false.
TEST_F(MrbFormatTest, OpenNonexistentFile) {
  MrbReader reader;
  EXPECT_FALSE(reader.open("/nonexistent/path/does_not_exist.mrb"));
}

// Opening an empty file (0 bytes) should return false.
TEST_F(MrbFormatTest, OpenEmptyFile) {
  // Create a zero-byte file at tmp_path_.
  { std::ofstream f(tmp_path_, std::ios::binary); }

  MrbReader reader;
  EXPECT_FALSE(reader.open(tmp_path_.c_str()));
}

// Opening a file with wrong magic bytes should return false.
TEST_F(MrbFormatTest, OpenWrongMagic) {
  {
    std::ofstream f(tmp_path_, std::ios::binary);
    // Write 32 bytes of garbage (wrong magic).
    const char junk[32] = "THIS IS NOT A VALID MRB FILE!!!";
    f.write(junk, 32);
  }

  MrbReader reader;
  EXPECT_FALSE(reader.open(tmp_path_.c_str()));
}

// Opening a truncated-but-valid-header file should return false.
TEST_F(MrbFormatTest, OpenTruncatedAfterHeader) {
  // First write a valid MRB with one paragraph.
  {
    MrbWriter writer;
    ASSERT_TRUE(writer.open(tmp_path_.c_str()));
    writer.begin_chapter();
    ASSERT_TRUE(writer.write_paragraph(make_text("hello")));
    writer.end_chapter();
    ASSERT_TRUE(writer.finish({}, {}));
  }

  // Read the file into memory, then write it back truncated to just the header.
  {
    std::ifstream in(tmp_path_, std::ios::binary);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    ASSERT_GT(buf.size(), 32u);  // must be larger than just the header

    // Truncate to 32 bytes (just the MrbHeader).
    buf.resize(32);
    std::ofstream out(tmp_path_, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
  }

  MrbReader reader;
  EXPECT_FALSE(reader.open(tmp_path_.c_str()));
}
