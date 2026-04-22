#include <gtest/gtest.h>

#include <algorithm>

#include "TestChapterSource.h"
#include "microreader/content/TextLayout.h"
#include "microreader/display/DrawBuffer.h"

using namespace microreader;

// ===== Helper: extract word text =====
static std::string word_str(const LayoutWord& w) {
  return std::string(w.text, static_cast<size_t>(w.len));
}

// ===== Helper: extract text from a layout line =====
static std::string line_text(const LayoutLine& line) {
  std::string result;
  for (size_t i = 0; i < line.words.size(); ++i) {
    if (i > 0)
      result += ' ';
    result.append(line.words[i].text, line.words[i].len);
  }
  return result;
}

// ===== Helper: count total words across all lines =====
static size_t total_words(const std::vector<LayoutLine>& lines) {
  size_t n = 0;
  for (auto& l : lines)
    n += l.words.size();
  return n;
}

// ===== Helper: collect all text from lines =====
static std::string all_text(const std::vector<LayoutLine>& lines) {
  std::string result;
  for (auto& line : lines) {
    if (!result.empty())
      result += ' ';
    result += line_text(line);
  }
  return result;
}

// ===== Font: 8px per char, 16px line height =====
static FixedFont font8(8, 16);

// ===================================================================
// layout_paragraph() tests
// ===================================================================

TEST(TextLayout, SingleWordFitsOneLine) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello", FontStyle::Regular, false));

  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(line_text(lines[0]), "Hello");
  EXPECT_EQ(lines[0].words[0].x, 0);
}

TEST(TextLayout, MultipleWordsSingleLine) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello world", FontStyle::Regular, false));

  // "Hello" = 40px, " " = 8px, "world" = 40px = 88px < 100
  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(line_text(lines[0]), "Hello world");
}

TEST(TextLayout, WordWrapToTwoLines) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello world foo", FontStyle::Regular, false));

  // Width 80: "Hello" = 40, space=8, "world" = 40 → need 88 > 80 → wrap
  LayoutOptions opts{80, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 2);
  EXPECT_EQ(line_text(lines[0]), "Hello");
  EXPECT_EQ(line_text(lines[1]), "world foo");
}

TEST(TextLayout, MultipleWordsMultipleLines) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("aa bb cc dd ee ff", FontStyle::Regular, false));

  // Each word = 16px, space = 8px
  // Width 56: "aa" + " " + "bb" + " " + "cc" = 16+8+16+8+16 = 64 > 56
  //           "aa" + " " + "bb" = 16+8+16 = 40 < 56
  //           "aa" + " " + "bb" + " " = 48 + "cc" = 64 > 56 → wrap after bb
  LayoutOptions opts{56, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 2);
  // All words should be preserved
  EXPECT_EQ(all_text(lines), "aa bb cc dd ee ff");
}

TEST(TextLayout, EmptyParagraph) {
  TextParagraph para;
  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);
  EXPECT_TRUE(lines.empty());
}

TEST(TextLayout, WhitespaceOnlyRun) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("   ", FontStyle::Regular, false));

  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);
  EXPECT_TRUE(lines.empty());
}

TEST(TextLayout, MultipleRuns) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello ", FontStyle::Regular, false));
  para.runs.push_back(microreader::Run("bold", FontStyle::Bold, false));
  para.runs.push_back(microreader::Run(" world", FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  ASSERT_EQ(lines[0].words.size(), 3);
  EXPECT_EQ(std::string(lines[0].words[0].text, lines[0].words[0].len), "Hello");
  EXPECT_EQ(lines[0].words[0].style, FontStyle::Regular);
  EXPECT_EQ(std::string(lines[0].words[1].text, lines[0].words[1].len), "bold");
  EXPECT_EQ(lines[0].words[1].style, FontStyle::Bold);
  EXPECT_EQ(std::string(lines[0].words[2].text, lines[0].words[2].len), "world");
  EXPECT_EQ(lines[0].words[2].style, FontStyle::Regular);
}

TEST(TextLayout, BreakingRun) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Line one", FontStyle::Regular, true));
  para.runs.push_back(microreader::Run("Line two", FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 2);
  EXPECT_EQ(line_text(lines[0]), "Line one");
  EXPECT_EQ(line_text(lines[1]), "Line two");
}

TEST(TextLayout, MultipleBreakingRuns) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("A", FontStyle::Regular, true));
  para.runs.push_back(microreader::Run("B", FontStyle::Regular, true));
  para.runs.push_back(microreader::Run("C", FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 3);
  EXPECT_EQ(line_text(lines[0]), "A");
  EXPECT_EQ(line_text(lines[1]), "B");
  EXPECT_EQ(line_text(lines[2]), "C");
}

// ===== Alignment tests =====

TEST(TextLayout, AlignCenter) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  para.alignment = Alignment::Center;

  // "Hi" = 16px on width 100 → room = 84 → nudge = 42
  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(lines[0].words[0].x, 42);
}

TEST(TextLayout, AlignEnd) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  para.alignment = Alignment::End;

  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(lines[0].words[0].x, 84);
}

TEST(TextLayout, JustifyTwoWords) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("aa bb cc", FontStyle::Regular, false));

  // Width 80: "aa" + " " + "bb" + " " + "cc" = 16+8+16+8+16 = 64
  // But "aa" + " " + "bb" = 40 → fits
  // "aa" + " " + "bb" + " " + "cc" = 64 < 80 → all on one line
  // Last line → no justification for single-line paragraphs
  LayoutOptions opts{80, Alignment::Justify};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  // Last line - no justification
  EXPECT_EQ(lines[0].words[0].x, 0);
}

TEST(TextLayout, JustifyMultipleLines) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("aa bb cc dd", FontStyle::Regular, false));

  // Width 50: "aa"=16, space=8, "bb"=16 → 40 < 50
  //           "aa" + " " + "bb" + " " + "cc" = 16+8+16+8+16 = 64 > 50
  // Line 1: "aa bb" (40px), room = 10 → justified (one gap gets +10)
  // Line 2: "cc dd" (40px) — last line, no justification
  LayoutOptions opts{50, Alignment::Justify};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 2);
  // First line: justified. "aa" at 0, "bb" at 0+16+8+10 = 34 (gap stretch)
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_GT(lines[0].words[1].x, 24);  // More than non-justified position
  // Second line: last line, starts at 0
  EXPECT_EQ(lines[1].words[0].x, 0);
}

// ===== Indent test =====

TEST(TextLayout, ParagraphIndent) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello world", FontStyle::Regular, false));
  para.indent = 20;

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  // First word indented by 20px
  EXPECT_EQ(lines[0].words[0].x, 20);
}

TEST(TextLayout, DefaultAlignmentFromOptions) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  // No para.alignment set → uses opts.alignment

  LayoutOptions opts{100, Alignment::Center};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(lines[0].words[0].x, 42);  // centered
}

TEST(TextLayout, MarginLeftWithIndent) {
  // When margin_left is set, text-indent is skipped to keep all lines aligned.
  TextParagraph para;
  para.indent = 16;  // 2 chars indent — ignored when margin_left is set
  // Runs with margin_left set (simulating a poem with left margin)
  microreader::Run r1("Hello world this is a long line here", FontStyle::Regular, false);
  r1.margin_left = 40;  // 5 chars margin
  para.runs.push_back(r1);

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 2u);
  // All lines at margin_left = 40 (indent skipped when margin_left present)
  EXPECT_EQ(lines[0].words[0].x, 40);
  EXPECT_EQ(lines[1].words[0].x, 40);
}

TEST(TextLayout, MarginRightReducesLineWidth) {
  // With margin_right, text should wrap earlier (at max_width - margin_right).
  TextParagraph para;
  // "AAAA BBBB CCCC" = 3 words × 4 chars × 8px = 32px each, + 8px spaces
  microreader::Run r1("AAAA BBBB CCCC", FontStyle::Regular, false);
  r1.margin_right = 40;
  para.runs.push_back(r1);

  // Width = 100, effective = 100 - 40 = 60. First two words: 32+8+32 = 72 > 60
  // So only "AAAA" fits on line 1 (32px), "BBBB" starts line 2, "CCCC" starts line 3
  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(word_str(lines[0].words[0]), "AAAA");
  EXPECT_EQ(word_str(lines[1].words[0]), "BBBB");
  EXPECT_EQ(word_str(lines[2].words[0]), "CCCC");
}

// ===== Unicode test =====

TEST(TextLayout, UnicodeText) {
  TextParagraph para;
  // 2 codepoints × 8px = 16px
  para.runs.push_back(microreader::Run(std::string("Gr\xc3\xbc\xc3\x9f"
                                                   "e"),
                                       FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  // "Grüße" = 5 codepoints × 8 = 40px
  EXPECT_EQ(lines[0].words[0].x, 0);
}

// ===== German text rendering pipeline test =====
// Simulates exactly what ReaderScreen does: layout → DrawWord → glyph iteration

TEST(TextLayout, GermanTextRenderingPipeline) {
  // Use the same font parameters as ReaderScreen: glyph_width=16, line_height=20
  FixedFont font16(16, 20);

  // German sentence with umlauts, guillemets, em-dash
  // "Rechtsgeschäft" = 14 codepoints (13 ASCII + 1 ä)
  // "für" = 3 codepoints (f + ü + r)
  // "»Prinz Eugen«" = 13 codepoints (» + P,r,i,n,z + space + E,u,g,e,n + «)
  // "1944–1945" = 9 codepoints (4 digits + – + 4 digits)
  TextParagraph para;
  para.runs.push_back(microreader::Run(std::string("Rechtsgesch\xc3\xa4"
                                                   "ft"                            // Rechtsgeschäft
                                                   " f\xc3\xbcr"                   // für
                                                   " \xc2\xbbPrinz Eugen\xc2\xab"  // »Prinz Eugen«
                                                   " 1944\xe2\x80\x93"             // 1944–
                                                   "1945"),                        // 1945
                                       FontStyle::Regular, false));

  LayoutOptions opts{DrawBuffer::kWidth, Alignment::Start};
  auto lines = TextLayout(font16).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 1u);

  // Verify word widths match codepoint counts × 16
  struct Expected {
    const char* text;
    size_t byte_len;
    int codepoints;
  };
  Expected expected[] = {
      {"Rechtsgesch\xc3\xa4"
       "ft",     15, 14}, // 11 ASCII + 2 bytes ä + 2 ASCII = 15 bytes, 14 codepoints
      {"f\xc3\xbcr",    4,  3 }, // f + ü(2b) + r = 4 bytes, 3 codepoints
      {"\xc2\xbbPrinz", 7,  6 }, // »(2b) + Prinz = 7 bytes, 6 codepoints
      {"Eugen\xc2\xab", 7,  6 }, // Eugen + «(2b) = 7 bytes, 6 codepoints
      {"1944\xe2\x80\x93"
       "1945",   11, 9 }, // 1944 + –(3b) + 1945 = 11 bytes, 9 codepoints
  };

  // Collect all words from all lines
  std::vector<LayoutWord> all_words;
  for (auto& line : lines)
    for (auto& w : line.words)
      all_words.push_back(w);

  ASSERT_EQ(all_words.size(), 5u) << "Expected 5 words";

  for (size_t i = 0; i < 5; ++i) {
    const auto& w = all_words[i];
    std::string word_text(w.text, w.len);
    SCOPED_TRACE("word[" + std::to_string(i) + "] = '" + word_text + "'");

    // Verify byte length
    EXPECT_EQ(w.len, expected[i].byte_len);

    // Verify word width = codepoints × glyph_width
    uint16_t measured = font16.word_width(w.text, w.len, FontStyle::Regular);
    EXPECT_EQ(measured, expected[i].codepoints * 16);

    // Count UTF-8 codepoints to verify FixedFont width calculation.
    const char* p = w.text;
    const char* end = w.text + w.len;
    int glyph_count = 0;
    while (p < end && *p) {
      const uint8_t b = static_cast<uint8_t>(*p);
      if (b < 0x80)
        p += 1;
      else if (b < 0xE0)
        p += 2;
      else if (b < 0xF0)
        p += 3;
      else
        p += 4;
      ++glyph_count;
    }
    EXPECT_EQ(glyph_count, expected[i].codepoints) << "Codepoint count doesn't match expected";

    // Verify rendered width = glyph_count × 8 × scale (where scale=2, so 16)
    // This matches the rendering: dw.x + ci * 8 * scale
    int rendered_width = glyph_count * 16;
    EXPECT_EQ(rendered_width, measured) << "Rendered width doesn't match layout-measured width";
  }
}

// ===== Word preservation across wrap =====

TEST(TextLayout, AllWordsPreservedAfterWrap) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("the quick brown fox jumps over the lazy dog", FontStyle::Regular, false));

  LayoutOptions opts{80, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  EXPECT_EQ(all_text(lines), "the quick brown fox jumps over the lazy dog");
}

// ===================================================================
// layout_page() tests
// ===================================================================

TEST(PageLayout, SingleParagraphFitsOnePage) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hello world", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items.size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
  EXPECT_EQ(page.start.paragraph, 0);
  EXPECT_EQ(page.end.paragraph, 1);
}

TEST(PageLayout, SpreadLinesToBottomPadding) {
  Chapter ch;
  for (int i = 0; i < 4; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Line", FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  TestChapterSource src(ch);
  PageOptions opts(200, 86, 10, 0);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items.size(), 4);
  EXPECT_EQ(page.vertical_offset, opts.padding_top);

  uint16_t padded_height = opts.height - opts.padding_top - opts.padding_bottom;
  uint16_t standard_descent = font8.y_advance(FontSize::Normal) - font8.baseline(FontSize::Normal);
  uint16_t expected_baseline = padded_height - standard_descent;
  uint16_t actual_baseline = page.text_items.back().y_offset + font8.baseline(FontSize::Normal);
  EXPECT_EQ(actual_baseline, expected_baseline);
}

TEST(PageLayout, SpreadLinesAlignsBottomWithMixedLineHeights) {
  Chapter ch;
  const uint8_t line_height_pct[] = {100, 110, 120, 130, 100};
  for (uint8_t pct : line_height_pct) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Line", FontStyle::Regular, false));
    tp.line_height_pct = pct;
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  // Total content height = 16 + 17 + 19 + 20 + 16 = 88px.
  // Make the page slightly taller so the branch is used, but not enough for full centering.
  TestChapterSource src(ch);
  PageOptions opts(200, 111, 10, 0);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.text_items.size(), 5);
  uint16_t padded_height = opts.height - opts.padding_top - opts.padding_bottom;
  uint16_t last_bottom = page.text_items.back().y_offset + page.text_items.back().height;
  EXPECT_EQ(last_bottom, padded_height);
}

// Spreading: page with a block image followed by two text lines.
// The gap between the image and the first text line should absorb most of the slack
// (weight 8), while the gap between the two text lines gets little (weight 1 if same
// paragraph, weight 3 if different paragraphs).
TEST(PageLayout, SpreadWithImagePrefersImageTextGap) {
  // Layout: image (50px) + text line + text line in the same paragraph.
  // page_height = 83, padded = 83 (no padding).
  // Items: image(50) + text(16) + text(16) = 82px. slack = 1.
  // Gaps: image→text1 (weight 8), text1→text2 (weight 1, same para). total_weight=9.
  // Extra space goes almost entirely to the image→text gap.
  // With slack=1, base_per_unit=0, leftover=1 → last 1 unit gets +1.
  // gap[0]=0 (8 units, all 0 except last which gets +1 if unit index ≥ 8)
  // Actually units_used: for gap[0], w=0..7 → units 0..7. leftover=1, total_weight-leftover=8.
  //   unit 0..7: units_used 0..7, all < 8 → 0. gap[0]=0.
  //   for gap[1], w=0: units_used=8 ≥ 8 → 1. gap[1]=1.
  // So image→text gap=0, text→text gap=1. Slack goes to the intra-para gap!
  // That seems wrong, let me design a test where image boundary wins.
  // Use slack=9 so that: base_per_unit=1, leftover=0.
  //   gap[0] = 8*1 = 8, gap[1] = 1*1 = 1. Image→text gets 8, text→text gets 1.
  // page_height: 50 + 16 + 16 + 8 + 1 = 91px of content needed to make slack=9 at height 91.
  // Actually we want y=82 (no spacing), padded_height=91. height=91+0padding=91.
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 200, 50));  // 200x50 image
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Line one two three", FontStyle::Regular, false));
  tp.runs.push_back(microreader::Run(" more words here x", FontStyle::Regular, true));  // breaking run → 2 lines
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // page height=91 (no padding, no para_spacing). Content = image(50) + line(16) + line(16) = 82.
  // slack = 91 - 82 = 9. weights: gap[0]=8 (image→text), gap[1]=1 (same para).
  // Expected: gap[0]=8, gap[1]=1.
  TestChapterSource src(ch);
  PageOptions opts(200, 91, 0, 0);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1u);
  ASSERT_GE(page.text_items.size(), 2u);

  // The image should be at y=0.
  EXPECT_EQ(page.image_items[0].y_offset, 0u);

  // After spreading: image→text1 gap gets 8px, text1→text2 gap gets 1px.
  // text_item[0].y_offset = 50 + 8 = 58
  // text_item[1].y_offset = 58 + 16 + 1 = 75
  // Then fine-tune baseline: last text baseline = 75 + font8.baseline() = 75+12=87.
  // target_baseline = padded_height(91) - standard_descent(16-12=4) = 87. delta=0.
  EXPECT_EQ(page.text_items[0].y_offset, 58u) << "Image→text gap should be 8px";
  EXPECT_EQ(page.text_items[1].y_offset, 75u) << "Text→text gap should be 1px";
}

TEST(PageLayout, SpreadWithHrPrefersHrTextGap) {
  // HR followed by two text lines. HR boundary gap (weight 8) should absorb most slack.
  // HR item uses default_y_advance=16px slot. Two text lines × 16px = 32px.
  // Total = 48px. page_height = 57 → slack = 9. weights: hr→text1 (8), text1→text2 (3, diff para).
  // total_weight=11. base_per_unit=0, leftover=9.
  // gap[0]: 8 units, units 0..7 < 11-9=2 → first 2 get 0, next 6 get 1 → gap[0]=6.
  // gap[1]: 3 units, units 8..10 ≥ 2 → all get 1 → gap[1]=3.
  // Actually: total_weight=11, leftover=9, total_weight-leftover=2.
  //   unit 0: units_used=0 < 2 → +0; unit 1: 1 < 2 → +0; units 2..10 ≥ 2 → +1.
  //   gap[0]: w=0..7 → units 0..7 → 0+0+1+1+1+1+1+1 = 6. gap[0]=6.
  //   gap[1]: w=0..2 → units 8..10 → 1+1+1 = 3. gap[1]=3.
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_hr());
  TextParagraph tp1;
  tp1.runs.push_back(microreader::Run("First text", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp1)));
  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("Second text", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));

  // HR slot = 16px, text1 = 16px, text2 = 16px. Total = 48. page_height = 57. slack = 9.
  TestChapterSource src(ch);
  PageOptions opts(200, 57, 0, 0);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_GE(page.text_items.size(), 2u);

  // hr_item[0].y_offset (center of HR slot) = 0 + 16/2 = 8.
  // After spread: HR slot top stays at 0, center = 8.
  EXPECT_EQ(page.hr_items[0].y_offset, 8u) << "HR center should be at y=8";

  // text[0].y_offset = 16 + 6 = 22 (HR slot 16 + gap 6).
  EXPECT_EQ(page.text_items[0].y_offset, 22u) << "First text should be at y=22";

  // text[1].y_offset = 22 + 16 + 3 = 41.
  EXPECT_EQ(page.text_items[1].y_offset, 41u) << "Second text should be at y=41";
}

TEST(PageLayout, SpreadNoOpWhenTooMuchSlack) {
  // If slack >= one default line height, spreading should NOT activate.
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Short", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // 16px of text, 200px page → slack = 184 >= 16 → no spreading. Flush to top.
  TestChapterSource src(ch);
  PageOptions opts(200, 200, 0, 0);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.text_items.size(), 1u);
  EXPECT_EQ(page.text_items[0].y_offset, 0u) << "No spreading when too much slack";
}

TEST(PageLayout, SpreadDisabledWhenCenterTextFalse) {
  // When center_text=false, spreading never fires even if page is nearly full.
  Chapter ch;
  for (int i = 0; i < 4; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Line", FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  // Same setup as SpreadLinesToBottomPadding but center_text=false.
  TestChapterSource src(ch);
  PageOptions opts(200, 86, 10, 0);
  opts.center_text = false;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.text_items.size(), 4u);
  // y_offsets should be 0, 16, 32, 48 (no spreading)
  EXPECT_EQ(page.text_items[0].y_offset, 0u);
  EXPECT_EQ(page.text_items[1].y_offset, 16u);
  EXPECT_EQ(page.text_items[2].y_offset, 32u);
  EXPECT_EQ(page.text_items[3].y_offset, 48u);
}

TEST(PageLayout, MultipleParagraphsFitOnePage) {
  Chapter ch;
  for (int i = 0; i < 3; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Paragraph " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  // 3 lines × 16px = 48px, 2 gaps × 8px = 16px → 64px < 100px
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 0, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items.size(), 3);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, PageBreakBetweenParagraphs) {
  Chapter ch;
  for (int i = 0; i < 5; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Line " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  // Each line = 16px, spacing = 4px. Page height = 50, padding = 0
  // Line 0: 0+16=16, Line 1: 16+4+16=36, Line 2: 36+4+16=56 > 50
  TestChapterSource src(ch);
  PageOptions opts(200, 50, 0, 4);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items.size(), 2);
  EXPECT_FALSE(page.at_chapter_end);
  EXPECT_EQ(page.end.paragraph, 2);
  EXPECT_EQ(page.end.line, 0);
}

TEST(PageLayout, ContinueFromMiddle) {
  Chapter ch;
  for (int i = 0; i < 5; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Line " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  TestChapterSource src(ch);
  PageOptions opts(200, 50, 0, 4);
  TextLayout tl(font8, opts, src);

  // First page
  auto page1 = tl.layout();
  ASSERT_FALSE(page1.at_chapter_end);

  // Continue from where page 1 ended
  tl.set_position(page1.end);
  auto page2 = tl.layout();
  EXPECT_EQ(page2.start, page1.end);
  EXPECT_GE(page2.text_items.size(), 1);
}

TEST(PageLayout, PageChaining) {
  Chapter ch;
  for (int i = 0; i < 10; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Paragraph " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  TestChapterSource src(ch);
  PageOptions opts(200, 40, 0, 4);
  TextLayout tl(font8, opts, src);

  // Chain through all pages
  std::vector<PageContent> pages;
  while (true) {
    auto page = tl.layout();
    ASSERT_TRUE(page.text_items.size() > 0 || page.image_items.size() > 0 || page.at_chapter_end);
    pages.push_back(std::move(page));
    if (pages.back().at_chapter_end)
      break;
    tl.set_position(pages.back().end);
  }

  // All 10 paragraphs should be covered
  EXPECT_GE(pages.size(), 3);  // 10 paras with spacing won't fit in 2 pages of 40px
  EXPECT_TRUE(pages.back().at_chapter_end);

  // Collect all paragraph indices
  std::vector<uint16_t> covered;
  for (auto& p : pages) {
    for (auto& t : p.text_items) {
      if (covered.empty() || covered.back() != t.paragraph_index)
        covered.push_back(t.paragraph_index);
    }
  }
  EXPECT_EQ(covered.size(), 10);
}

TEST(PageLayout, MultiLineParagraphSplitAcrossPages) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("aa bb cc dd ee ff gg hh ii jj", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // Width 40: each word = 16px, space = 8px → "aa bb" = 40 fits, "aa bb cc" = 64 > 40
  // So each line fits ~2 words → ~5 lines
  // Page height 48 → fits 3 lines (48/16=3)
  TestChapterSource src(ch);
  PageOptions opts(40, 48, 0);
  TextLayout tl(font8, opts, src);

  auto page1 = tl.layout();
  EXPECT_EQ(page1.text_items.size(), 3);
  EXPECT_FALSE(page1.at_chapter_end);
  EXPECT_EQ(page1.end.paragraph, 0);  // Still in paragraph 0
  EXPECT_EQ(page1.end.line, 3);       // After line 3

  // Second page continues
  tl.set_position(page1.end);
  auto page2 = tl.layout();
  EXPECT_GE(page2.text_items.size(), 1);
}

TEST(PageLayout, ImageParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(42, 100, 30));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items.size(), 1);
  EXPECT_EQ(page.image_items[0].key, 42);
  // 100x30 scaled up to fill width 200 → 200x60
  EXPECT_EQ(page.image_items[0].width, 200);
  EXPECT_EQ(page.image_items[0].height, 60);
  EXPECT_EQ(page.image_items[0].y_offset, 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageScaledToFitWidth) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 400, 200));

  // Content width = 200-0 = 200, image is 400 wide → scaled to 200×100
  TestChapterSource src(ch);
  PageOptions opts(200, 300, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_EQ(page.image_items[0].width, 200);
  EXPECT_EQ(page.image_items[0].height, 100);
}

TEST(PageLayout, ImageDoesntFitPushedToNextPage) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hello", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 80));

  // Page height 32 → text takes 16px, image needs 80px → doesn't fit
  TestChapterSource src(ch);
  PageOptions opts(200, 32, 0);
  TextLayout tl(font8, opts, src);
  auto page = tl.layout();

  EXPECT_EQ(page.text_items.size(), 1);
  EXPECT_EQ(page.image_items.size(), 0);
  EXPECT_FALSE(page.at_chapter_end);
  EXPECT_EQ(page.end.paragraph, 1);

  // Next page shows the image
  tl.set_position(page.end);
  auto page2 = tl.layout();
  EXPECT_EQ(page2.image_items.size(), 1);
}

TEST(PageLayout, HrParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_hr());
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_TRUE(page.at_chapter_end);
  EXPECT_EQ(page.text_items.size(), 0);
  EXPECT_EQ(page.image_items.size(), 0);
}

TEST(PageLayout, MixedContent) {
  Chapter ch;
  TextParagraph tp1;
  tp1.runs.push_back(microreader::Run("Before image", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp1)));
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 20));
  ch.paragraphs.push_back(Paragraph::make_hr());
  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("After hr", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0, 4);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items.size(), 2);
  EXPECT_EQ(page.image_items.size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, PaddingReducesContentArea) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hello", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // Page 200×100 with padding 20 → content area 160×60
  // Line height 16 → fits floor(60/16) = 3 lines
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 20, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items.size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, EmptyChapter) {
  Chapter ch;
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_TRUE(page.at_chapter_end);
  EXPECT_EQ(page.text_items.size(), 0);
}

// ===================================================================
// Stress: long text pagination
// ===================================================================

TEST(PageLayout, LongTextPaginatesCorrectly) {
  Chapter ch;
  TextParagraph tp;
  // 100 words = long paragraph
  std::string long_text;
  for (int i = 0; i < 100; ++i) {
    if (i > 0)
      long_text += " ";
    long_text += "word" + std::to_string(i);
  }
  tp.runs.push_back(microreader::Run(long_text, FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  TestChapterSource src(ch);
  PageOptions opts(200, 80, 0);
  TextLayout tl(font8, opts, src);

  // Paginate through all content
  std::vector<PageContent> pages;
  int safety = 0;
  while (safety++ < 100) {
    auto page = tl.layout();
    bool advanced = page.at_chapter_end || page.end.line > 0 || page.end.paragraph > 0;
    pages.push_back(std::move(page));
    if (pages.back().at_chapter_end)
      break;
    tl.set_position(pages.back().end);
    ASSERT_TRUE(pages.back().end.line > 0 || pages.back().end.paragraph > 0) << "Page didn't advance position";
  }

  EXPECT_TRUE(pages.back().at_chapter_end);
  EXPECT_GE(pages.size(), 3);  // 100 words should need multiple pages

  // Verify all words are accounted for
  size_t total = 0;
  for (auto& p : pages) {
    for (auto& t : p.text_items) {
      total += t.line.words.size();
    }
  }
  EXPECT_EQ(total, 100);
}

TEST(PageLayout, YOffsetsIncreaseMonotonically) {
  Chapter ch;
  for (int i = 0; i < 5; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Paragraph " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  TestChapterSource src(ch);
  PageOptions opts(200, 300, 0, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  for (size_t i = 1; i < page.text_items.size(); ++i) {
    EXPECT_GT(page.text_items[i].y_offset, page.text_items[i - 1].y_offset) << "Y offsets must increase monotonically";
  }
}

TEST(PageLayout, XPositionsIncreaseWithinLine) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("one two three four five", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  TestChapterSource src(ch);

  PageOptions opts(300, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  for (auto& item : page.text_items) {
    for (size_t i = 1; i < item.line.words.size(); ++i) {
      EXPECT_GT(item.line.words[i].x, item.line.words[i - 1].x) << "X positions must increase within a line";
    }
  }
}

// ===================================================================
// Font size tests
// ===================================================================

TEST(TextLayout, LargeWordsSameWidth) {
  // Large and Normal words have the same width (bitmap font can't resize glyphs).
  TextParagraph para_normal;
  para_normal.runs.push_back(microreader::Run("Hello", FontStyle::Regular, FontSize::Normal));

  TextParagraph para_large;
  para_large.runs.push_back(microreader::Run("Hello", FontStyle::Regular, FontSize::Large));

  LayoutOptions opts{300, Alignment::Start};
  auto lines_n = TextLayout(font8).layout_paragraph(opts, para_normal);
  auto lines_l = TextLayout(font8).layout_paragraph(opts, para_large);

  ASSERT_EQ(lines_n.size(), 1u);
  ASSERT_EQ(lines_l.size(), 1u);

  uint16_t end_n = lines_n[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, FontSize::Normal);
  uint16_t end_l = lines_l[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, FontSize::Large);
  EXPECT_EQ(end_l, end_n);
}

TEST(TextLayout, SmallWordsSameWidth) {
  // Small and Normal words have the same width (bitmap font can't resize glyphs).
  TextParagraph para_normal;
  para_normal.runs.push_back(microreader::Run("Hello", FontStyle::Regular, FontSize::Normal));

  TextParagraph para_small;
  para_small.runs.push_back(microreader::Run("Hello", FontStyle::Regular, FontSize::Small));

  LayoutOptions opts{300, Alignment::Start};
  auto lines_n = TextLayout(font8).layout_paragraph(opts, para_normal);
  auto lines_s = TextLayout(font8).layout_paragraph(opts, para_small);

  ASSERT_EQ(lines_n.size(), 1u);
  ASSERT_EQ(lines_s.size(), 1u);

  uint16_t end_n = lines_n[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, FontSize::Normal);
  uint16_t end_s = lines_s[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, FontSize::Small);
  EXPECT_EQ(end_s, end_n);
}

TEST(TextLayout, LargeTextSameWrapping) {
  // With fixed glyph width, Large and Normal produce the same line count.
  std::string text = "The quick brown fox jumps over the lazy dog";

  TextParagraph para_normal;
  para_normal.runs.push_back(microreader::Run(text, FontStyle::Regular, FontSize::Normal));

  TextParagraph para_large;
  para_large.runs.push_back(microreader::Run(text, FontStyle::Regular, FontSize::Large));

  LayoutOptions opts{200, Alignment::Start};
  auto lines_n = TextLayout(font8).layout_paragraph(opts, para_normal);
  auto lines_l = TextLayout(font8).layout_paragraph(opts, para_large);

  EXPECT_EQ(lines_l.size(), lines_n.size());
}

TEST(TextLayout, LayoutWordCarriesFontSize) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Normal", FontStyle::Regular, FontSize::Normal));
  para.runs.push_back(microreader::Run(" Large", FontStyle::Bold, FontSize::Large));
  para.runs.push_back(microreader::Run(" Small", FontStyle::Italic, FontSize::Small));

  LayoutOptions opts{500, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 3u);

  EXPECT_EQ(lines[0].words[0].size, FontSize::Normal);
  EXPECT_EQ(lines[0].words[1].size, FontSize::Large);
  EXPECT_EQ(lines[0].words[2].size, FontSize::Small);
}

TEST(TextLayout, MixedSizesLineHeight) {
  // A page with a Large-text line should use a taller y_advance for that line
  Chapter ch;
  TextParagraph tp_large;
  tp_large.runs.push_back(microreader::Run("Heading", FontStyle::Bold, FontSize::Large));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp_large)));

  TextParagraph tp_normal;
  tp_normal.runs.push_back(microreader::Run("Body text paragraph.", FontStyle::Regular, FontSize::Normal));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp_normal)));
  TestChapterSource src(ch);

  PageOptions opts(300, 400, 0, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_GE(page.text_items.size(), 2u);

  // First line (Large) should have a greater y_advance than normal
  uint16_t y0 = page.text_items[0].y_offset;
  uint16_t y1 = page.text_items[1].y_offset;
  uint16_t gap = y1 - y0;

  // Large line height = 16 * 5/4 = 20, plus para_spacing = 8 → gap = 28
  // Normal line height would be 16 + 8 = 24
  EXPECT_GT(gap, font8.y_advance(FontSize::Normal));
}

TEST(TextLayout, FixedFontSizeScaling) {
  // Width is constant regardless of FontSize (bitmap font can't resize glyphs).
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::Small),
            font8.char_width('A', FontStyle::Regular, FontSize::Normal));
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::Large),
            font8.char_width('A', FontStyle::Regular, FontSize::Normal));
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::XLarge),
            font8.char_width('A', FontStyle::Regular, FontSize::Normal));
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::XXLarge),
            font8.char_width('A', FontStyle::Regular, FontSize::Normal));

  // y_advance still scales (line heights can vary without causing overlap).
  EXPECT_LT(font8.y_advance(FontSize::Small), font8.y_advance(FontSize::Normal));
  EXPECT_GT(font8.y_advance(FontSize::Large), font8.y_advance(FontSize::Normal));
  EXPECT_GT(font8.y_advance(FontSize::XLarge), font8.y_advance(FontSize::Large));
  EXPECT_GT(font8.y_advance(FontSize::XXLarge), font8.y_advance(FontSize::XLarge));

  // Width: always 8 regardless of size
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::Small), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::Normal), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::Large), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::XLarge), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, FontSize::XXLarge), 8);

  // Line heights: 16*90%=14, 16, 16*110%=17, 16*120%=19, 16*130%=20
  EXPECT_EQ(font8.y_advance(FontSize::Small), 14);
  EXPECT_EQ(font8.y_advance(FontSize::Normal), 16);
  EXPECT_EQ(font8.y_advance(FontSize::Large), 17);
  EXPECT_EQ(font8.y_advance(FontSize::XLarge), 19);
  EXPECT_EQ(font8.y_advance(FontSize::XXLarge), 20);
}

// ---------------------------------------------------------------------------
// Cross-run word continuation (no space between adjacent runs without trailing space)
// ---------------------------------------------------------------------------

TEST(TextLayout, CrossRunWordContinuation) {
  // Two runs: "A" (no trailing space) + "LICE" (no leading space)
  // Should be placed immediately adjacent, not with a word space.
  TextParagraph para;
  para.runs.push_back(microreader::Run("A", FontStyle::Regular));
  para.runs.push_back(microreader::Run("LICE was here", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 560;
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 1u);
  // "A" is at x=0, width=8. "LICE" should be at x=8 (no space gap).
  ASSERT_GE(lines[0].words.size(), 2u);
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_EQ(std::string(lines[0].words[0].text, lines[0].words[0].len), "A");
  EXPECT_EQ(lines[0].words[1].x, 8);  // immediately after "A", no space
  EXPECT_EQ(std::string(lines[0].words[1].text, lines[0].words[1].len), "LICE");
}

TEST(TextLayout, CrossRunWithTrailingSpace) {
  // Run ending with space: "Hello " + "world" → normal word spacing
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello ", FontStyle::Regular));
  para.runs.push_back(microreader::Run("world", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 560;
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 1u);
  ASSERT_GE(lines[0].words.size(), 2u);
  // "Hello" at x=0 (40px), then space (8px), then "world" at 48px
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_EQ(lines[0].words[1].x, 48);  // 40 + 8 space
}

TEST(TextLayout, CrossRunDifferentStyles) {
  // "A" in bold + "LICE" in regular, no spaces → adjacent
  TextParagraph para;
  para.runs.push_back(microreader::Run("A", FontStyle::Bold));
  para.runs.push_back(microreader::Run("LICE", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 560;
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 2u);
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_EQ(lines[0].words[1].x, 8);  // no space between them
}

// ===================================================================
// Image edge cases
// ===================================================================

TEST(PageLayout, ImageZeroDimensionsSkipped) {
  // Images with 0x0 dimensions should be skipped entirely
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 0, 0));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items.size(), 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageZeroWidthSkipped) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 0, 50));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items.size(), 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageZeroHeightSkipped) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 0));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items.size(), 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageScaledToFitPageHeight) {
  // Image taller than page height should be capped
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 2000));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);  // content area 200x100
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_LE(page.image_items[0].height, 100);  // capped to page height
  EXPECT_GT(page.image_items[0].height, 0);
  EXPECT_LE(page.image_items[0].width, 200);  // preserved ratio
}

TEST(PageLayout, ImageScaledBothDimensions) {
  // Image larger than page in both dimensions
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 1000, 2000));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_LE(page.image_items[0].width, 200);
  EXPECT_LE(page.image_items[0].height, 100);
  EXPECT_GT(page.image_items[0].width, 0);
  EXPECT_GT(page.image_items[0].height, 0);
}

TEST(PageLayout, ImageFillsEntirePage) {
  // Image exactly fills page height — should be placed as first item
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 200, 100));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_EQ(page.image_items[0].width, 200);
  EXPECT_EQ(page.image_items[0].height, 100);
  EXPECT_EQ(page.image_items[0].y_offset, 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ZeroImageBetweenTextContinues) {
  // A 0x0 image between text paragraphs shouldn't break pagination
  Chapter ch;
  TextParagraph tp1;
  tp1.runs.push_back(microreader::Run("Before", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp1)));
  ch.paragraphs.push_back(Paragraph::make_image(1, 0, 0));
  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("After", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0, 4);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items.size(), 0);  // 0x0 image skipped
  EXPECT_EQ(page.text_items.size(), 2);   // both text paragraphs present
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageWithPaddingUsesFullWidth) {
  // Images use full page width (no padding margins), centered
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 800, 600));
  TestChapterSource src(ch);

  // Page 600x800 with padding 20 → images scale to full 600x800
  PageOptions opts(600, 800, 20, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_LE(page.image_items[0].width, 600);   // ≤ full page width
  EXPECT_LE(page.image_items[0].height, 800);  // ≤ full page height
  // Should be centered: x_offset = (600 - actual_w) / 2
  EXPECT_EQ(page.image_items[0].x_offset, (600 - page.image_items[0].width) / 2);
}

TEST(PageLayout, MultipleImagesStack) {
  // Two images should stack on the same page when they fit
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 15));
  ch.paragraphs.push_back(Paragraph::make_image(2, 100, 15));

  // 100x15 scales up to 200x30 each. Two images + spacing: 30 + 4 + 30 = 64 ≤ 100
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 0, 4);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 2);
  EXPECT_EQ(page.image_items[0].y_offset, 0);
  EXPECT_EQ(page.image_items[1].y_offset, 34);  // 30 + 4 para_spacing
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageNeverExceedsPageBounds) {
  // Stress test: various extreme image sizes should never exceed full page
  PageOptions opts(600, 800, 20, 8);
  uint16_t page_w = opts.width;   // 600 — images use full width
  uint16_t page_h = opts.height;  // 800 — images use full height

  struct TestCase {
    uint16_t w, h;
  } cases[] = {
      {10000, 10000},
      {1,     10000},
      {10000, 1    },
      {600,   801  },
      {601,   800  },
      {1920,  1080 },
      {1080,  1920 },
      {65535, 65535},
      {100,   100  },
      {599,   799  },
      {600,   800  },
  };

  for (auto& tc : cases) {
    Chapter ch;
    ch.paragraphs.push_back(Paragraph::make_image(1, tc.w, tc.h));
    TestChapterSource src(ch);

    auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

    ASSERT_EQ(page.image_items.size(), 1) << "Image " << tc.w << "x" << tc.h << " should be placed";
    EXPECT_LE(page.image_items[0].width, page_w) << "Image " << tc.w << "x" << tc.h << " width overflow";
    EXPECT_LE(page.image_items[0].height, page_h) << "Image " << tc.w << "x" << tc.h << " height overflow";
    EXPECT_GT(page.image_items[0].width, 0) << "Image " << tc.w << "x" << tc.h << " collapsed to 0 width";
    EXPECT_GT(page.image_items[0].height, 0) << "Image " << tc.w << "x" << tc.h << " collapsed to 0 height";
    // Images must be centered
    EXPECT_EQ(page.image_items[0].x_offset, (page_w - page.image_items[0].width) / 2)
        << "Image " << tc.w << "x" << tc.h << " not centered";
  }
}

// ===================================================================
// Vertical centering
// ===================================================================

TEST(PageLayout, ImageOnlyPageVerticallyCentered) {
  // A single image on its own page should be vertically centered in full page
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 50));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
  // 100x50 scaled up to 200x100 on 200px page → vertical_offset = (200-100)/2 = 50
  EXPECT_EQ(page.vertical_offset, 50);
}

TEST(PageLayout, ImageOnlyWithPaddingCenteredOnFullScreen) {
  // Image centering uses full screen height, not just the content area
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 50));
  TestChapterSource src(ch);

  // Page 200x200, padding 20 → content area 160x160
  PageOptions opts(200, 200, 20, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
  // 100x50 scaled up to 200x100 → centered on full 200px screen: (200-100)/2 = 50
  EXPECT_EQ(page.vertical_offset, 50);
}

TEST(PageLayout, SparseTextVerticallyCentered) {
  // A single short line on a tall page — no centering (centering was removed)
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  TestChapterSource src(ch);

  // 16px of text on 200px page → NOT centered (flush to top)
  PageOptions opts(200, 200, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.text_items.size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
  EXPECT_EQ(page.vertical_offset, opts.padding_top);
}

TEST(PageLayout, FirstParagraphTopMarginRespected) {
  // First paragraph with explicit spacing_before (from CSS margin-top)
  // should push content down at the start of a chapter.
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Dedication text", FontStyle::Regular, false));
  auto para = Paragraph::make_text(std::move(tp));
  para.spacing_before = 100;  // 100px top margin
  ch.paragraphs.push_back(std::move(para));
  TestChapterSource src(ch);

  PageOptions opts(200, 400, 10, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.text_items.size(), 1);
  // Text should start at y=100 (the spacing_before), not y=0
  EXPECT_EQ(page.text_items[0].y_offset, 100);
}

TEST(PageLayout, FirstParagraphTopMarginNotAppliedMidChapter) {
  // spacing_before on a paragraph that's NOT at the start of a chapter
  // should only apply default para_spacing when starting mid-chapter.
  Chapter ch;
  TextParagraph tp1;
  tp1.runs.push_back(
      microreader::Run("First paragraph with lots of text to fill a page and force a page break somewhere in the "
                       "middle of the content flow",
                       FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp1)));

  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("Second paragraph", FontStyle::Regular, false));
  auto para2 = Paragraph::make_text(std::move(tp2));
  para2.spacing_before = 100;
  ch.paragraphs.push_back(std::move(para2));
  TestChapterSource src(ch);

  // Start from paragraph 1 (not chapter start)
  PageOptions opts(200, 400, 10, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(1, 0)).layout();

  ASSERT_EQ(page.text_items.size(), 1);
  // Should NOT have the 100px top margin since we're mid-chapter
  EXPECT_EQ(page.text_items[0].y_offset, 0);
}

TEST(PageLayout, FullPageTextNotVerticallyCentered) {
  // A page that's more than half full should NOT be centered
  Chapter ch;
  TextParagraph tp;
  // Create enough text to fill more than half the page
  std::string text;
  for (int i = 0; i < 50; ++i) {
    if (i > 0)
      text += " ";
    text += "word" + std::to_string(i);
  }
  tp.runs.push_back(microreader::Run(text, FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  TestChapterSource src(ch);

  PageOptions opts(200, 80, 0);  // small page that will be mostly full
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  // Full page → vertical_offset is normal top padding (no extra centering)
  EXPECT_EQ(page.vertical_offset, opts.padding_top);
}

TEST(PageLayout, MixedTextAndImageNoVerticalCenter) {
  // Pages with both text and images should not be vertically centered
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Caption", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  ch.paragraphs.push_back(Paragraph::make_image(1, 50, 20));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0, 4);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  // Has both text and image → vertical_offset is normal top padding (no extra centering)
  EXPECT_EQ(page.vertical_offset, opts.padding_top);
}

TEST(PageLayout, NoCenteringOnMultiPageChapter) {
  // Even if the last page is sparse, no centering because the chapter spans multiple pages
  Chapter ch;
  // Fill first page with lots of text
  for (int p = 0; p < 10; ++p) {
    TextParagraph tp;
    std::string text;
    for (int i = 0; i < 30; ++i) {
      if (i > 0)
        text += " ";
      text += "word" + std::to_string(i);
    }
    tp.runs.push_back(microreader::Run(text, FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }
  // Add a short final paragraph that will land on a later page
  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("end", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 10, 4);
  // Layout first page
  TextLayout tl(font8, opts, src);
  auto page1 = tl.layout();
  EXPECT_FALSE(page1.at_chapter_end);
  // Layout remaining pages until chapter end
  PagePosition pos = page1.end;
  PageContent last_page;
  for (int i = 0; i < 100; ++i) {
    tl.set_position(pos);
    last_page = tl.layout();
    if (last_page.at_chapter_end)
      break;
    pos = last_page.end;
  }
  EXPECT_TRUE(last_page.at_chapter_end);
  // Last page should NOT be extra-centered — vertical_offset equals normal top padding
  EXPECT_EQ(last_page.vertical_offset, opts.padding_top);
}

TEST(PageLayout, ImageDoesNotOverlapText) {
  // When an image follows text, the image y_offset must be >= text y_offset + line_height
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Some text before image", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 40));
  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("Text after image", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));
  TestChapterSource src(ch);

  PageOptions opts(200, 400, 10, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_FALSE(page.text_items.empty());
  ASSERT_FALSE(page.image_items.empty());

  // Check no image overlaps any text item
  for (const auto& img : page.image_items) {
    for (const auto& txt : page.text_items) {
      uint16_t txt_bottom = txt.y_offset + font8.y_advance();
      uint16_t img_bottom = img.y_offset + img.height;
      // Image must not overlap text: either image is fully below text or fully above
      bool image_below_text = img.y_offset >= txt_bottom;
      bool image_above_text = img_bottom <= txt.y_offset;
      EXPECT_TRUE(image_below_text || image_above_text)
          << "Image at y=" << img.y_offset << " h=" << img.height << " overlaps text at y=" << txt.y_offset;
    }
  }

  // Also verify y_offsets are monotonically increasing across all items
  std::vector<std::pair<uint16_t, uint16_t>> ranges;  // {y_offset, y_offset + height}
  for (const auto& txt : page.text_items) {
    ranges.push_back({txt.y_offset, static_cast<uint16_t>(txt.y_offset + font8.y_advance())});
  }
  for (const auto& img : page.image_items) {
    ranges.push_back({img.y_offset, static_cast<uint16_t>(img.y_offset + img.height)});
  }
  std::sort(ranges.begin(), ranges.end());
  for (size_t i = 1; i < ranges.size(); ++i) {
    EXPECT_GE(ranges[i].first, ranges[i - 1].second)
        << "Items overlap: [" << ranges[i - 1].first << "," << ranges[i - 1].second << ") vs [" << ranges[i].first
        << "," << ranges[i].second << ")";
  }
}

// ===================================================================
// Inline image (drop-cap style) tests
// ===================================================================

// Helper: build a chapter with one text paragraph that has an inline image
static Chapter make_inline_image_chapter(uint16_t img_w, uint16_t img_h, const std::string& text) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run(text, FontStyle::Regular, false));
  tp.inline_image = ImageRef(42, img_w, img_h);
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  return ch;
}

TEST(PageLayout, InlineImageBottomAlignsWithBaseline) {
  // 80x75 image (like Alice initial cap). Font: 8px wide, 16px line height, baseline=12.
  // Image bottom should align with the baseline of the first text line.
  auto ch = make_inline_image_chapter(80, 75, "URIOUSER and curiouser cried Alice she was so much surprised");
  TestChapterSource src(ch);
  PageOptions opts(DrawBuffer::kWidth, DrawBuffer::kHeight, 0, 20);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1u);
  const auto& img = page.image_items[0];

  EXPECT_EQ(img.width, 80);
  EXPECT_EQ(img.height, 75);

  // First text line y_offset
  ASSERT_FALSE(page.text_items.empty());
  uint16_t first_line_top = page.text_items[0].y_offset;
  // ti.baseline includes the inline_extra space above the text within the item
  uint16_t baseline_y = first_line_top + page.text_items[0].baseline;

  // Image bottom should equal baseline
  uint16_t img_bottom = img.y_offset + img.height;
  EXPECT_EQ(img_bottom, baseline_y) << "Image bottom should align with baseline";
}

TEST(PageLayout, InlineImageFirstLineIndented) {
  // First line text should be indented by image width + 4px gap
  auto ch = make_inline_image_chapter(80, 75, "URIOUSER and curiouser cried Alice she was so much surprised");
  TestChapterSource src(ch);
  PageOptions opts(DrawBuffer::kWidth, DrawBuffer::kHeight, 0, 20);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_GE(page.text_items.size(), 2u);

  // First line: words should start at image_width + gap
  uint16_t first_word_x = page.text_items[0].line.words[0].x;
  EXPECT_EQ(first_word_x, 84u) << "First line should be indented by 80 + 4 gap";

  // Second line: words should start at 0 (full width)
  uint16_t second_word_x = page.text_items[1].line.words[0].x;
  EXPECT_EQ(second_word_x, 0u) << "Second line should use full width";
}

TEST(PageLayout, SmallInlineImageSameAsBaseline) {
  // 12x12 image — same height as baseline (12px). Bottom aligns with baseline,
  // so image top = first line top.
  auto ch = make_inline_image_chapter(12, 12, "Hello world this is a test of a small inline icon");
  TestChapterSource src(ch);
  PageOptions opts(300, 400, 0, 10);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1u);
  const auto& img = page.image_items[0];

  EXPECT_EQ(img.width, 12);
  EXPECT_EQ(img.height, 12);

  uint16_t first_line_top = page.text_items[0].y_offset;
  // For a 12px image with 12px baseline: image top == first line top
  EXPECT_EQ(img.y_offset, first_line_top) << "12px image should start at first line top";

  // First word indented by 12 + 4 = 16
  EXPECT_EQ(page.text_items[0].line.words[0].x, 16u);
}

TEST(PageLayout, TallInlineImageExtendsAboveFirstLine) {
  // 40x48 image — taller than baseline (12px). Image extends above the first line.
  auto ch = make_inline_image_chapter(40, 48, "Hello world this is a test of a tall inline image");
  TestChapterSource src(ch);
  PageOptions opts(300, 400, 0, 10);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1u);
  const auto& img = page.image_items[0];

  uint16_t first_line_top = page.text_items[0].y_offset;
  // ti.baseline includes the inline_extra space above the text within the item
  uint16_t baseline_y = first_line_top + page.text_items[0].baseline;

  // Image bottom aligns with baseline
  EXPECT_EQ(img.y_offset + img.height, baseline_y);
  EXPECT_EQ(img.y_offset, baseline_y - 48);

  // First line indented by 40 + 4 = 44
  EXPECT_EQ(page.text_items[0].line.words[0].x, 44u);
}

TEST(PageLayout, InlineImageAtLeftEdge) {
  // Image x should be at padding offset, not centered
  auto ch = make_inline_image_chapter(60, 30, "Some text after the image goes here");
  TestChapterSource src(ch);
  PageOptions opts(400, 400, 15, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1u);
  // x_offset should be padding value
  EXPECT_EQ(page.image_items[0].x_offset, 15u) << "Inline image should be at left padding edge";
}

TEST(PageLayout, InlineImageNoOverlapWithText) {
  // Verify no text line occupies the same vertical+horizontal space as the image
  auto ch = make_inline_image_chapter(
      80, 75,
      "URIOUSER and curiouser cried Alice she was so much surprised that for a moment she quite forgot how to speak");
  TestChapterSource src(ch);
  PageOptions opts(DrawBuffer::kWidth, DrawBuffer::kHeight, 0, 20);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1u);
  const auto& img = page.image_items[0];
  uint16_t img_left = img.x_offset;
  uint16_t img_right = img.x_offset + img.width;
  uint16_t img_top = img.y_offset;
  uint16_t img_bot = img.y_offset + img.height;

  for (const auto& ti : page.text_items) {
    uint16_t text_top = ti.y_offset;
    uint16_t text_bot = ti.y_offset + font8.y_advance();
    bool vertically_overlaps = text_top < img_bot && text_bot > img_top;
    if (vertically_overlaps) {
      // All words on this line must start beyond the image
      for (const auto& w : ti.line.words) {
        // Word left edge (in page coords) = padding + w.x
        uint16_t word_left = opts.padding_left + w.x;
        EXPECT_GE(word_left, img_right) << "Word '" << std::string(w.text, w.len) << "' at x=" << word_left
                                        << " overlaps image ending at x=" << img_right << " on line y=" << text_top;
      }
    }
  }
}

TEST(PageLayout, InlineImageDoesNotIntersectPreviousTextLines) {
  Chapter ch;
  TextParagraph tp1;
  tp1.runs.push_back(microreader::Run("Previous line with some text", FontStyle::Regular, false));
  tp1.runs.push_back(microreader::Run(" more text", FontStyle::Regular, microreader::FontSize::Large));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp1)));

  TextParagraph tp2;
  tp2.runs.push_back(
      microreader::Run("Inline image paragraph starts here", FontStyle::Regular, microreader::FontSize::Normal));
  tp2.inline_image = ImageRef(42, 80, 75);
  tp2.runs.push_back(microreader::Run(" and continues with more text to wrap onto the next line", FontStyle::Regular,
                                      microreader::FontSize::Large));
  tp2.line_height_pct = 130;
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));
  TestChapterSource src(ch);

  PageOptions opts(300, 160, 0, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items.size(), 1u);
  const auto& img = page.image_items[0];
  uint16_t img_top = img.y_offset;
  uint16_t img_bot = img.y_offset + img.height;

  EXPECT_GE(img_top, 0u) << "Inline image must not start above the page top";

  for (const auto& ti : page.text_items) {
    if (ti.paragraph_index >= img.paragraph_index)
      continue;

    uint16_t text_top = ti.y_offset;
    uint16_t text_bot = static_cast<uint16_t>(ti.y_offset + ti.height);
    EXPECT_LE(text_bot, img_top) << "Inline image at y=" << img_top << " overlaps previous text line spanning ["
                                 << text_top << ", " << text_bot << ")";
  }
}

// ===================================================================
// layout_page_backward() tests
// ===================================================================

TEST(PageLayoutBackward, EmptyChapter) {
  Chapter ch;
  TestChapterSource src(ch);
  PageOptions opts(200, 200, 10, 8);
  TextLayout tl(font8, opts, src, PagePosition(0, 0));
  auto page = tl.layout_backward();
  EXPECT_EQ(page.start, PagePosition(0, 0));
  EXPECT_EQ(page.end, PagePosition(0, 0));
  EXPECT_TRUE(page.text_items.empty());
}

TEST(PageLayoutBackward, SingleParagraphFromEnd) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hello world", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0, 8);
  // End = {1, 0} means "end of chapter" (1 paragraph, past the last)
  TextLayout tl(font8, opts, src, PagePosition(1, 0));
  auto page = tl.layout_backward();

  EXPECT_EQ(page.start, PagePosition(0, 0));
  EXPECT_EQ(page.end, PagePosition(1, 0));
  ASSERT_EQ(page.text_items.size(), 1);
  EXPECT_EQ(line_text(page.text_items[0].line), "Hello world");
}

TEST(PageLayoutBackward, MultiParagraphAllFit) {
  Chapter ch;
  for (const char* text : {"First paragraph", "Second paragraph", "Third paragraph"}) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run(text, FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  TestChapterSource src(ch);
  PageOptions opts(200, 200, 0, 8);
  TextLayout tl(font8, opts, src, PagePosition(3, 0));
  auto page = tl.layout_backward();

  EXPECT_EQ(page.start, PagePosition(0, 0));
  ASSERT_EQ(page.text_items.size(), 3);
  EXPECT_EQ(line_text(page.text_items[0].line), "First paragraph");
  EXPECT_EQ(line_text(page.text_items[1].line), "Second paragraph");
  EXPECT_EQ(line_text(page.text_items[2].line), "Third paragraph");
}

TEST(PageLayoutBackward, PageFullCutsFromTop) {
  // 3 paragraphs on a page that can only fit 2. Backward from end should
  // give the last 2.
  Chapter ch;
  for (const char* text : {"First", "Second", "Third"}) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run(text, FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  // Page height = 16 (line) + 8 (spacing) + 16 (line) = 40. Third para won't fit.
  TestChapterSource src(ch);
  PageOptions opts(200, 40, 0, 8);
  TextLayout tl(font8, opts, src, PagePosition(3, 0));
  auto page = tl.layout_backward();

  EXPECT_EQ(page.start, PagePosition(1, 0));
  EXPECT_EQ(page.end, PagePosition(3, 0));
  ASSERT_EQ(page.text_items.size(), 2);
  EXPECT_EQ(line_text(page.text_items[0].line), "Second");
  EXPECT_EQ(line_text(page.text_items[1].line), "Third");
}

TEST(PageLayoutBackward, MultiLineParagraphSplit) {
  // A paragraph with multiple lines. Backward should pick trailing lines.
  Chapter ch;
  TextParagraph tp;
  // 10 words × 8px = 80px per word. Width 100px → ~1 word per line.
  tp.runs.push_back(microreader::Run("aaa bbb ccc ddd eee", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // Width 32px: each 3-letter word = 24px, fits one per line. 5 lines × 16px = 80px.
  // Page height 32 → fits 2 lines.
  TestChapterSource src(ch);
  PageOptions opts(32, 32, 0, 0);
  TextLayout tl(font8, opts, src, PagePosition(1, 0));
  auto page = tl.layout_backward();

  ASSERT_EQ(page.text_items.size(), 2);
  // Should get the last 2 lines (ddd, eee)
  EXPECT_EQ(page.start.paragraph, 0);
  EXPECT_EQ(page.start.line, 3);  // lines 3 and 4
  EXPECT_EQ(line_text(page.text_items[0].line), "ddd");
  EXPECT_EQ(line_text(page.text_items[1].line), "eee");
}

TEST(PageLayoutBackward, PartialEndPosition) {
  // End position mid-paragraph: {0, 3} means include lines 0, 1, 2 of paragraph 0.
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("aaa bbb ccc ddd eee", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // Page is tall enough for all lines
  TestChapterSource src(ch);
  PageOptions opts(32, 200, 0, 0);
  TextLayout tl(font8, opts, src, PagePosition(0, 3));
  auto page = tl.layout_backward();

  // Should include lines 0, 1, 2 (aaa, bbb, ccc)
  ASSERT_EQ(page.text_items.size(), 3);
  EXPECT_EQ(page.start, PagePosition(0, 0));
  EXPECT_EQ(page.end, PagePosition(0, 3));
  EXPECT_EQ(line_text(page.text_items[0].line), "aaa");
  EXPECT_EQ(line_text(page.text_items[1].line), "bbb");
  EXPECT_EQ(line_text(page.text_items[2].line), "ccc");
}

TEST(PageLayoutBackward, ImageParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 50));
  TestChapterSource src_bwd(ch);

  PageOptions opts(200, 200, 10, 8);
  TextLayout tl(font8, opts, src_bwd, PagePosition(1, 0));
  auto page = tl.layout_backward();

  EXPECT_EQ(page.start, PagePosition(0, 0));
  ASSERT_EQ(page.image_items.size(), 1);
  EXPECT_EQ(page.image_items[0].key, 1);
}

TEST(PageLayoutBackward, HrParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_hr());
  TestChapterSource src_hr(ch);

  PageOptions opts(200, 200, 10, 8);
  TextLayout tl(font8, opts, src_hr, PagePosition(1, 0));
  auto page = tl.layout_backward();

  EXPECT_EQ(page.start, PagePosition(0, 0));
  ASSERT_EQ(page.hr_items.size(), 1);
}

TEST(PageLayoutBackward, PageBreakStopsBackward) {
  // Content before a page break should not be included
  Chapter ch;
  TextParagraph tp1;
  tp1.runs.push_back(microreader::Run("Before break", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp1)));
  ch.paragraphs.push_back(Paragraph::make_page_break());
  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("After break", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0, 8);
  TextLayout tl(font8, opts, src, PagePosition(3, 0));
  auto page = tl.layout_backward();

  // Should only get "After break" — page break stops backward scan
  ASSERT_EQ(page.text_items.size(), 1);
  EXPECT_EQ(line_text(page.text_items[0].line), "After break");
}

TEST(PageLayoutBackward, ForwardBackwardRoundTrip) {
  // For a multi-page chapter, forward then backward from the end position
  // should yield the same page content.
  Chapter ch;
  for (int i = 0; i < 5; ++i) {
    TextParagraph tp;
    std::string text;
    for (int j = 0; j < 10; ++j) {
      if (j > 0)
        text += " ";
      text += "word" + std::to_string(i * 10 + j);
    }
    tp.runs.push_back(microreader::Run(text, FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  TestChapterSource src(ch);
  PageOptions opts(200, 80, 0, 8);
  TextLayout tl(font8, opts, src);

  // Layout forward to get page boundaries
  auto page1 = tl.layout();
  ASSERT_FALSE(page1.at_chapter_end);

  tl.set_position(page1.end);
  auto page2 = tl.layout();

  // Layout backward from page2's end should produce the same start/end
  tl.set_position(page2.end);
  auto back2 = tl.layout_backward();
  EXPECT_EQ(back2.start, page2.start);
  EXPECT_EQ(back2.end, page2.end);
  EXPECT_EQ(back2.text_items.size(), page2.text_items.size());
}

TEST(PageLayoutBackward, ChapterEndFlagSet) {
  // When backward from end covers the whole chapter, at_chapter_end should be set
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Short", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0, 8);
  TextLayout tl(font8, opts, src, PagePosition(1, 0));
  auto page = tl.layout_backward();

  EXPECT_TRUE(page.at_chapter_end);
  EXPECT_EQ(page.start, PagePosition(0, 0));
}

TEST(PageLayoutBackward, ParagraphSpacingMatches) {
  // Verify that backward layout produces the same y-offsets as forward
  Chapter ch;
  for (const char* text : {"Alpha", "Beta"}) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run(text, FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  TestChapterSource src(ch);
  PageOptions opts(200, 200, 0, 12);  // 12px para spacing
  TextLayout fwd_tl(font8, opts, src);
  auto fwd = fwd_tl.layout();
  TextLayout bwd_tl(font8, opts, src, PagePosition(2, 0));
  auto bwd = bwd_tl.layout_backward();

  ASSERT_EQ(fwd.text_items.size(), bwd.text_items.size());
  for (size_t i = 0; i < fwd.text_items.size(); ++i) {
    EXPECT_EQ(fwd.text_items[i].y_offset, bwd.text_items[i].y_offset) << "y_offset mismatch at item " << i;
  }
}
