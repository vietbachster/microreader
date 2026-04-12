#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "microreader/content/BitmapFont.h"
#include "microreader/content/BitmapFontFormat.h"

using namespace microreader;

// ---------------------------------------------------------------------------
// Helper: build a minimal MBF binary in memory
// ---------------------------------------------------------------------------

// Tiny test font with 4 glyphs: '?' (0x3F), 'A' (0x41), 'i' (0x69), U+4E16 (世)
// Two ranges: [0x3F..0x69] mapped as 3 glyphs, [0x4E16..0x4E16] as 1 glyph
//
// Layout:
//   MbfHeader  (40 bytes)
//   Range 0    (8 bytes)  — ASCII 0x3F..0x6A (count=43, but we only fill 3 glyphs)
//   Range 1    (8 bytes)  — CJK  0x4E16..0x4E16 (count=1)
//   Glyph[0]   '?' idx=0   — 5×7 bitmap, advance=6
//   Glyph[1]   '@' idx=1   — unused placeholder (advance=0)
//   ...        (glyphs 1..42 are padding between '?' and 'i')
//   Glyph[2]   'A' idx=2   — 6×8 bitmap, advance=8
//   ...
//   Glyph[42]  'i' idx=42  — 2×7 bitmap, advance=4
//   Glyph[43]  U+4E16 (世) — 10×10 bitmap, advance=12
//   Bitmap data

static std::vector<uint8_t> build_test_font() {
  // We'll use a simpler range setup:
  // Range 0: codepoints 0x3F..0x69 (count=43), glyph_table_start=0
  //   glyph[0]  = '?' (0x3F)
  //   glyph[2]  = 'A' (0x41, offset 2 from range start)
  //   glyph[42] = 'i' (0x69, offset 42 from range start)
  //   other entries are empty (advance=0)
  // Range 1: codepoints 0x4E16..0x4E16 (count=1), glyph_table_start=43
  //   glyph[43] = 世

  const uint16_t num_ranges = 2;
  // Total glyphs: 43 (range 0) + 1 (range 1) = 44
  const uint16_t num_glyphs = 44;

  const size_t header_size = sizeof(MbfHeader);
  const size_t ranges_size = num_ranges * sizeof(MbfRange);
  const size_t glyphs_size = num_glyphs * sizeof(MbfGlyph);
  const size_t bitmap_data_offset = header_size + ranges_size + glyphs_size;

  // Bitmap sizes (row stride = ceil(w/8)):
  // '?': 5×7, stride=1, total=7 bytes
  // 'A': 6×8, stride=1, total=8 bytes
  // 'i': 2×7, stride=1, total=7 bytes
  // '世': 10×10, stride=2, total=20 bytes
  const size_t q_bmp_size = 7;
  const size_t a_bmp_size = 8;
  const size_t i_bmp_size = 7;
  const size_t cjk_bmp_size = 20;
  const size_t total_bmp = q_bmp_size + a_bmp_size + i_bmp_size + cjk_bmp_size;

  std::vector<uint8_t> buf(bitmap_data_offset + total_bmp, 0);

  // Header
  MbfHeader hdr{};
  hdr.magic = kMbfMagic;
  hdr.version = kMbfVersion;
  hdr.glyph_height = 10;
  hdr.baseline = 8;
  hdr.y_advance = 12;
  hdr.default_advance = 6;
  hdr.num_ranges = num_ranges;
  hdr.num_glyphs = num_glyphs;
  hdr.bitmap_data_offset = static_cast<uint32_t>(bitmap_data_offset);
  memcpy(buf.data(), &hdr, sizeof(hdr));

  // Ranges
  auto* ranges = reinterpret_cast<MbfRange*>(buf.data() + header_size);
  ranges[0].first_codepoint = 0x3F;  // '?'
  ranges[0].count = 43;              // 0x3F..0x69 inclusive
  ranges[0].glyph_table_start = 0;

  ranges[1].first_codepoint = 0x4E16;  // 世
  ranges[1].count = 1;
  ranges[1].glyph_table_start = 43;

  // Glyphs
  auto* glyphs = reinterpret_cast<MbfGlyph*>(buf.data() + header_size + ranges_size);

  // All glyphs default to 0 (memset from vector init)
  // Set the 4 actual glyphs:

  uint32_t bmp_off = 0;

  // glyph[0] = '?' (codepoint 0x3F, index 0 in range 0)
  glyphs[0].bitmap_offset = bmp_off;
  glyphs[0].advance_width = 6;
  glyphs[0].bitmap_width = 5;
  glyphs[0].bitmap_height = 7;
  glyphs[0].x_offset = 1;
  glyphs[0].y_offset = -7;  // 7 pixels above baseline
  bmp_off += q_bmp_size;

  // glyph[2] = 'A' (codepoint 0x41, index 2 in range 0 because 0x41-0x3F=2)
  glyphs[2].bitmap_offset = bmp_off;
  glyphs[2].advance_width = 8;
  glyphs[2].bitmap_width = 6;
  glyphs[2].bitmap_height = 8;
  glyphs[2].x_offset = 1;
  glyphs[2].y_offset = -8;
  bmp_off += a_bmp_size;

  // glyph[42] = 'i' (codepoint 0x69, index 42 in range 0 because 0x69-0x3F=42)
  glyphs[42].bitmap_offset = bmp_off;
  glyphs[42].advance_width = 4;
  glyphs[42].bitmap_width = 2;
  glyphs[42].bitmap_height = 7;
  glyphs[42].x_offset = 1;
  glyphs[42].y_offset = -7;
  bmp_off += i_bmp_size;

  // glyph[43] = 世 (codepoint 0x4E16)
  glyphs[43].bitmap_offset = bmp_off;
  glyphs[43].advance_width = 12;
  glyphs[43].bitmap_width = 10;
  glyphs[43].bitmap_height = 10;
  glyphs[43].x_offset = 1;
  glyphs[43].y_offset = -9;

  // Write some non-zero bitmap data so we can verify pointers are correct
  uint8_t* bmp = buf.data() + bitmap_data_offset;
  // '?' bitmap: rows of 0b01110000, 0b00010000, etc.
  bmp[0] = 0x70;  // .###....
  bmp[1] = 0x88;  // #...#...
  bmp[2] = 0x08;  // ....#...
  bmp[3] = 0x10;  // ...#....
  bmp[4] = 0x20;  // ..#.....
  bmp[5] = 0x00;  // ........
  bmp[6] = 0x20;  // ..#.....

  return buf;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class BitmapFontTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> data_;
  BitmapFont font_;

  void SetUp() override {
    data_ = build_test_font();
    font_.init(data_.data(), data_.size());
    ASSERT_TRUE(font_.valid());
  }
};

TEST_F(BitmapFontTest, ValidatesMagic) {
  EXPECT_TRUE(font_.valid());
  EXPECT_EQ(font_.num_glyphs(), 44);
}

TEST_F(BitmapFontTest, RejectsBadMagic) {
  auto bad = data_;
  bad[0] = 'X';
  BitmapFont f(bad.data(), bad.size());
  EXPECT_FALSE(f.valid());
}

TEST_F(BitmapFontTest, RejectsTruncatedData) {
  BitmapFont f(data_.data(), 10);  // way too small
  EXPECT_FALSE(f.valid());
}

TEST_F(BitmapFontTest, RejectsNullptr) {
  BitmapFont f(nullptr, 100);
  EXPECT_FALSE(f.valid());
}

TEST_F(BitmapFontTest, CharWidthAscii) {
  EXPECT_EQ(font_.char_width('?', FontStyle::Regular), 6);
  EXPECT_EQ(font_.char_width('A', FontStyle::Regular), 8);
  EXPECT_EQ(font_.char_width('i', FontStyle::Regular), 4);
}

TEST_F(BitmapFontTest, CharWidthCJK) {
  EXPECT_EQ(font_.char_width(U'\u4E16', FontStyle::Regular), 12);
}

TEST_F(BitmapFontTest, CharWidthMissingFallsBackToQuestion) {
  // 'Z' (0x5A) is in range 0 but has advance_width=0 (empty glyph slot)
  // So it falls back to '?' advance = 6
  // Actually, glyph[0x5A-0x3F=27] has advance_width=0 which is valid.
  // For a codepoint completely outside all ranges, we get '?' fallback.
  EXPECT_EQ(font_.char_width(U'\u00FF', FontStyle::Regular), 6);  // '?' fallback
}

TEST_F(BitmapFontTest, CharWidthCompletelyMissing) {
  // If no '?' glyph were available, we'd get default_advance.
  // But we DO have '?', so any missing char returns its advance_width=6.
  EXPECT_EQ(font_.char_width(0x10000, FontStyle::Regular), 6);
}

TEST_F(BitmapFontTest, YAdvanceAndBaseline) {
  EXPECT_EQ(font_.y_advance(), 12);
  EXPECT_EQ(font_.baseline(), 8);
}

TEST_F(BitmapFontTest, GlyphHeight) {
  EXPECT_EQ(font_.glyph_height(), 10);
}

TEST_F(BitmapFontTest, WordWidthAscii) {
  const char* text = "Ai";
  EXPECT_EQ(font_.word_width(text, 2, FontStyle::Regular), 8 + 4);
}

TEST_F(BitmapFontTest, WordWidthUtf8) {
  // U+4E16 in UTF-8 is E4 B8 96
  const char text[] = "A\xE4\xB8\x96i";
  EXPECT_EQ(font_.word_width(text, 5, FontStyle::Regular), 8 + 12 + 4);
}

TEST_F(BitmapFontTest, WordWidthEmpty) {
  EXPECT_EQ(font_.word_width("", 0, FontStyle::Regular), 0);
}

TEST_F(BitmapFontTest, GlyphDataAscii) {
  auto g = font_.glyph_data('A');
  EXPECT_NE(g.bits, nullptr);
  EXPECT_EQ(g.bitmap_width, 6);
  EXPECT_EQ(g.bitmap_height, 8);
  EXPECT_EQ(g.advance_width, 8);
  EXPECT_EQ(g.x_offset, 1);
  EXPECT_EQ(g.y_offset, -8);
}

TEST_F(BitmapFontTest, GlyphDataQuestion) {
  auto g = font_.glyph_data('?');
  EXPECT_NE(g.bits, nullptr);
  EXPECT_EQ(g.bitmap_width, 5);
  EXPECT_EQ(g.bitmap_height, 7);
  EXPECT_EQ(g.advance_width, 6);
  // Verify first bitmap byte
  EXPECT_EQ(g.bits[0], 0x70);
}

TEST_F(BitmapFontTest, GlyphDataCJK) {
  auto g = font_.glyph_data(U'\u4E16');
  EXPECT_NE(g.bits, nullptr);
  EXPECT_EQ(g.bitmap_width, 10);
  EXPECT_EQ(g.bitmap_height, 10);
  EXPECT_EQ(g.advance_width, 12);
}

TEST_F(BitmapFontTest, GlyphDataMissingFallback) {
  // Unknown codepoint → fallback to '?' glyph data
  auto g = font_.glyph_data(0x10000);
  EXPECT_NE(g.bits, nullptr);
  EXPECT_EQ(g.bitmap_width, 5);   // '?' bitmap
  EXPECT_EQ(g.advance_width, 6);  // '?' advance
}

TEST_F(BitmapFontTest, GlyphDataSpaceLike) {
  // Glyph at 0x40 (index 1 in range 0) has bitmap_width=0, bitmap_height=0
  auto g = font_.glyph_data('@');
  EXPECT_EQ(g.bits, nullptr);  // no bitmap for empty glyph
  EXPECT_EQ(g.bitmap_width, 0);
  EXPECT_EQ(g.advance_width, 0);
}

TEST_F(BitmapFontTest, IFontInterfaceWorks) {
  // BitmapFont should work through IFont pointer
  const IFont* ifont = &font_;
  EXPECT_EQ(ifont->char_width('A', FontStyle::Regular), 8);
  EXPECT_EQ(ifont->y_advance(), 12);
  EXPECT_EQ(ifont->baseline(), 8);
  EXPECT_EQ(ifont->word_width("Ai", 2, FontStyle::Regular), 12);
}

TEST_F(BitmapFontTest, DefaultConstructorInvalid) {
  BitmapFont f;
  EXPECT_FALSE(f.valid());
  EXPECT_EQ(f.char_width('A', FontStyle::Regular), 0);
  EXPECT_EQ(f.y_advance(), 0);
}

TEST_F(BitmapFontTest, RangesAreSorted) {
  // Range 0 starts at 0x3F, Range 1 at 0x4E16 — binary search should work
  EXPECT_EQ(font_.char_width('?', FontStyle::Regular), 6);         // range 0 start
  EXPECT_EQ(font_.char_width('i', FontStyle::Regular), 4);         // range 0 end
  EXPECT_EQ(font_.char_width(U'\u4E16', FontStyle::Regular), 12);  // range 1
}
