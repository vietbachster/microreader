#include <gtest/gtest.h>

#include <algorithm>

#include "TestChapterSource.h"
#include "microreader/content/TextLayout.h"
#include "microreader/display/DrawBuffer.h"

using namespace microreader;

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

// ===== Font: 8px per char, 16px line height =====
static FixedFont font8(8, 16);
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

  EXPECT_EQ(page.text_items().size(), 1);
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

  EXPECT_EQ(page.text_items().size(), 4);

  uint16_t standard_descent = font8.y_advance(FontSize::Normal) - font8.baseline(FontSize::Normal);
  uint16_t expected_baseline = opts.height - opts.padding_bottom - standard_descent;
  uint16_t actual_baseline = page.text_items().back().y_offset + font8.baseline(FontSize::Normal);
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

  ASSERT_EQ(page.text_items().size(), 5);
  uint16_t last_bottom = page.text_items().back().y_offset + page.text_items().back().height;
  EXPECT_EQ(last_bottom, opts.height - opts.padding_bottom);
}

// Spreading: page with a block image followed by two text lines.
// The gap between the image and the first text line should absorb most of the slack
// (weight 8), while the gap between the two text lines gets little (weight 1 if same
// paragraph, weight 3 if different paragraphs).
TEST(PageLayout, SpreadWithImagePrefersImageTextGap) {
  // Layout: image (50px) + text line + text line in the same paragraph.
  // page_height = 83, padded = 83 (no padding).
  // Items: image(50) + text(16) + text(16) = 82px. slack = 1.
  // Gaps: imageâ†’text1 (weight 8), text1â†’text2 (weight 1, same para). total_weight=9.
  // Extra space goes almost entirely to the imageâ†’text gap.
  // With slack=1, base_per_unit=0, leftover=1 â†’ last 1 unit gets +1.
  // gap[0]=0 (8 units, all 0 except last which gets +1 if unit index â‰¥ 8)
  // Actually units_used: for gap[0], w=0..7 â†’ units 0..7. leftover=1, total_weight-leftover=8.
  //   unit 0..7: units_used 0..7, all < 8 â†’ 0. gap[0]=0.
  //   for gap[1], w=0: units_used=8 â‰¥ 8 â†’ 1. gap[1]=1.
  // So imageâ†’text gap=0, textâ†’text gap=1. Slack goes to the intra-para gap!
  // That seems wrong, let me design a test where image boundary wins.
  // Use slack=9 so that: base_per_unit=1, leftover=0.
  //   gap[0] = 8*1 = 8, gap[1] = 1*1 = 1. Imageâ†’text gets 8, textâ†’text gets 1.
  // page_height: 50 + 16 + 16 + 8 + 1 = 91px of content needed to make slack=9 at height 91.
  // Actually we want y=82 (no spacing), padded_height=91. height=91+0padding=91.
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 200, 50));  // 200x50 image
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Line one two three", FontStyle::Regular, false));
  tp.runs.push_back(microreader::Run(" more words here x", FontStyle::Regular, true));  // breaking run â†’ 2 lines
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // page height=91 (no padding, no para_spacing). Content = image(50) + line(16) + line(16) = 82.
  // slack = 91 - 82 = 9. weights: gap[0]=8 (imageâ†’text), gap[1]=1 (same para).
  // Expected: gap[0]=8, gap[1]=1.
  TestChapterSource src(ch);
  PageOptions opts(200, 91, 0, 0);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 1u);
  ASSERT_GE(page.text_items().size(), 2u);

  // The image should be at y=0.
  EXPECT_EQ(page.image_items()[0].y_offset, 0u);

  // After spreading: imageâ†’text1 gap gets 8px, text1â†’text2 gap gets 1px.
  // text_item[0].y_offset = 50 + 8 = 58
  // text_item[1].y_offset = 58 + 16 + 1 = 75
  // Then fine-tune baseline: last text baseline = 75 + font8.baseline() = 75+12=87.
  // target_baseline = padded_height(91) - standard_descent(16-12=4) = 87. delta=0.
  EXPECT_EQ(page.text_items()[0].y_offset, 58u) << "Imageâ†’text gap should be 8px";
  EXPECT_EQ(page.text_items()[1].y_offset, 75u) << "Textâ†’text gap should be 1px";
}

TEST(PageLayout, SpreadWithHrPrefersHrTextGap) {
  // HR followed by two text lines. HR boundary gap (weight 8) should absorb most slack.
  // HR item uses default_y_advance=16px slot. Two text lines Ã— 16px = 32px.
  // Total = 48px. page_height = 57 â†’ slack = 9. weights: hrâ†’text1 (8), text1â†’text2 (3, diff para).
  // total_weight=11. base_per_unit=0, leftover=9.
  // gap[0]: 8 units, units 0..7 < 11-9=2 â†’ first 2 get 0, next 6 get 1 â†’ gap[0]=6.
  // gap[1]: 3 units, units 8..10 â‰¥ 2 â†’ all get 1 â†’ gap[1]=3.
  // Actually: total_weight=11, leftover=9, total_weight-leftover=2.
  //   unit 0: units_used=0 < 2 â†’ +0; unit 1: 1 < 2 â†’ +0; units 2..10 â‰¥ 2 â†’ +1.
  //   gap[0]: w=0..7 â†’ units 0..7 â†’ 0+0+1+1+1+1+1+1 = 6. gap[0]=6.
  //   gap[1]: w=0..2 â†’ units 8..10 â†’ 1+1+1 = 3. gap[1]=3.
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

  ASSERT_GE(page.text_items().size(), 2u);

  // hr_item[0].y_offset = slot top = 0 (render adds dy/2 to get center).
  // After spread: HR slot top stays at 0.
  EXPECT_EQ(page.hr_items()[0].y_offset, 0u) << "HR slot top should be at y=0";

  // text[0].y_offset = 16 + 6 = 22 (HR slot 16 + gap 6).
  EXPECT_EQ(page.text_items()[0].y_offset, 22u) << "First text should be at y=22";

  // text[1].y_offset = 22 + 16 + 3 = 41.
  EXPECT_EQ(page.text_items()[1].y_offset, 41u) << "Second text should be at y=41";
}

TEST(PageLayout, SpreadNoOpWhenTooMuchSlack) {
  // If slack >= one default line height, spreading should NOT activate.
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Short", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // 16px of text, 200px page â†’ slack = 184 >= 16 â†’ no spreading. Flush to top.
  TestChapterSource src(ch);
  PageOptions opts(200, 200, 0, 0);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.text_items().size(), 1u);
  EXPECT_EQ(page.text_items()[0].y_offset, 0u) << "No spreading when too much slack";
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

  ASSERT_EQ(page.text_items().size(), 4u);
  // y_offsets should be 10, 26, 42, 58 (no spreading, padding_top=10 baked in)
  EXPECT_EQ(page.text_items()[0].y_offset, 10u);
  EXPECT_EQ(page.text_items()[1].y_offset, 26u);
  EXPECT_EQ(page.text_items()[2].y_offset, 42u);
  EXPECT_EQ(page.text_items()[3].y_offset, 58u);
}

TEST(PageLayout, MultipleParagraphsFitOnePage) {
  Chapter ch;
  for (int i = 0; i < 3; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Paragraph " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }

  // 3 lines Ã— 16px = 48px, 2 gaps Ã— 8px = 16px â†’ 64px < 100px
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 0, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items().size(), 3);
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

  EXPECT_EQ(page.text_items().size(), 2);
  EXPECT_FALSE(page.at_chapter_end);
  EXPECT_EQ(page.end.paragraph, 2);
  EXPECT_EQ(page.end.offset, 0);
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
  EXPECT_GE(page2.text_items().size(), 1);
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
    ASSERT_TRUE(page.text_items().size() > 0 || page.image_items().size() > 0 || page.at_chapter_end);
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
    for (auto& t : p.text_items()) {
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

  // Width 40: each word = 16px, space = 8px â†’ "aa bb" = 40 fits, "aa bb cc" = 64 > 40
  // So each line fits ~2 words â†’ ~5 lines
  // Page height 48 â†’ fits 3 lines (48/16=3)
  TestChapterSource src(ch);
  PageOptions opts(40, 48, 0);
  TextLayout tl(font8, opts, src);

  auto page1 = tl.layout();
  EXPECT_EQ(page1.text_items().size(), 3);
  EXPECT_FALSE(page1.at_chapter_end);
  EXPECT_EQ(page1.end.paragraph, 0);  // Still in paragraph 0
  EXPECT_EQ(page1.end.offset, 3);     // After line 3

  // Second page continues
  tl.set_position(page1.end);
  auto page2 = tl.layout();
  EXPECT_GE(page2.text_items().size(), 1);
}

TEST(PageLayout, ImageParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(42, 100, 30));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items().size(), 1);
  EXPECT_EQ(page.image_items()[0].key, 42);
  // 100x30 scaled up to fill width 200 â†’ 200x60
  EXPECT_EQ(page.image_items()[0].width, 200);
  EXPECT_EQ(page.image_items()[0].height, 60);
  EXPECT_EQ(page.image_items()[0].y_offset, 20u);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageScaledToFitWidth) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 400, 200));

  // Content width = 200-0 = 200, image is 400 wide â†’ scaled to 200Ã—100
  TestChapterSource src(ch);
  PageOptions opts(200, 300, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_EQ(page.image_items()[0].width, 200);
  EXPECT_EQ(page.image_items()[0].height, 100);
}

TEST(PageLayout, ImageDoesntFitAfterText) {
  // Image is clamped to page height, then min_slice_h threshold applied.
  // 60x80 image on 200x32 page â†’ clamped to 24x32 (block_height=32).
  // Text uses 16px, leaving 16px. font8 y_advance=16 â†’
  // min_slice_h = max(32/4=8, 16*3/2=24) = 24. 16 < 24 â†’ image deferred to page 2.
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hello", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  ch.paragraphs.push_back(Paragraph::make_image(1, 60, 80));

  TestChapterSource src(ch);
  PageOptions opts(200, 32, 0, 0);
  TextLayout tl(font8, opts, src);
  auto page1 = tl.layout();

  // Page 1: text only â€” image deferred because available slice < min_slice_h
  EXPECT_EQ(page1.text_items().size(), 1u);
  EXPECT_EQ(page1.image_items().size(), 0u);
  EXPECT_FALSE(page1.at_chapter_end);
  EXPECT_EQ(page1.end.paragraph, 1);
  EXPECT_EQ(page1.end.offset, 0);

  // Page 2: full image
  tl.set_position(page1.end);
  auto page2 = tl.layout();
  ASSERT_EQ(page2.image_items().size(), 1u);
  EXPECT_EQ(page2.image_items()[0].y_crop, 0);
  EXPECT_EQ(page2.image_items()[0].height, 32);
  EXPECT_TRUE(page2.at_chapter_end);
}

TEST(PageLayout, ImageCappedToFitPage) {
  // Image taller than the page is proportionally scaled down to fit (no slicing).
  // Image: 200Ã—600. Page: width=200, height=300, padding=0.
  // After clamping: 100Ã—300 (scaled proportionally to fit height).
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 200, 600));

  TestChapterSource src(ch);
  PageOptions opts(200, 300, 0);
  TextLayout tl(font8, opts, src);
  auto page = tl.layout();

  ASSERT_EQ(page.image_items().size(), 1u);
  EXPECT_EQ(page.image_items()[0].height, 300);  // scaled to page height
  EXPECT_EQ(page.image_items()[0].y_crop, 0);    // no crop
  EXPECT_EQ(page.image_items()[0].width, 100);   // proportionally narrowed
  EXPECT_TRUE(page.at_chapter_end);              // fits on one page
}

TEST(PageLayout, ImageSplitWithTextBefore) {
  // Text fills part of the page; the clamped image is sliced to fill the rest.
  // 200x200 image on 200x100 page â†’ clamped to 100x100 (block_height=100).
  // Text uses 16px, leaving 84px available. font8 y_advance=16, min_cut_h=24.
  // Cut-off would be 16px < 24 â†’ slice reduced to 100-24=76px so cut is obvious.
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Line", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  ch.paragraphs.push_back(Paragraph::make_image(1, 200, 200));

  TestChapterSource src(ch);
  PageOptions opts(200, 100, 0, 0);
  TextLayout tl(font8, opts, src);
  auto page1 = tl.layout();

  // Page 1: text + image slice (reduced to ensure visible cut-off)
  ASSERT_EQ(page1.text_items().size(), 1u);
  ASSERT_EQ(page1.image_items().size(), 1u);
  EXPECT_EQ(page1.image_items()[0].y_crop, 0);
  EXPECT_EQ(page1.image_items()[0].height, 76);
  EXPECT_FALSE(page1.at_chapter_end);
  EXPECT_EQ(page1.end.paragraph, 1);
  EXPECT_EQ(page1.end.offset, 76);

  // Page 2: remaining 24px slice
  tl.set_position(page1.end);
  auto page2 = tl.layout();
  EXPECT_EQ(page2.text_items().size(), 0u);
  ASSERT_EQ(page2.image_items().size(), 1u);
  EXPECT_EQ(page2.image_items()[0].y_crop, 76);
  EXPECT_EQ(page2.image_items()[0].height, 24);
  EXPECT_TRUE(page2.at_chapter_end);
}

TEST(PageLayout, TallImageFitsOnSinglePage) {
  // An image taller than the page is clamped to page height and fits on one page.
  // Image: 200Ã—200 â†’ clamped to 100Ã—100 on a 200Ã—100 page.
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 200, 200));

  TestChapterSource src(ch);
  PageOptions opts(200, 100, 0, 0);
  TextLayout tl(font8, opts, src);
  auto page = tl.layout();

  ASSERT_EQ(page.image_items().size(), 1u);
  EXPECT_EQ(page.image_items()[0].height, 100);       // clamped to page height
  EXPECT_EQ(page.image_items()[0].y_crop, 0);         // no crop
  EXPECT_EQ(page.image_items()[0].full_height, 100);  // full_height == block_height
  EXPECT_TRUE(page.at_chapter_end);                   // single page
}

TEST(PageLayout, HrParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_hr());
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_TRUE(page.at_chapter_end);
  EXPECT_EQ(page.text_items().size(), 0);
  EXPECT_EQ(page.image_items().size(), 0);
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

  EXPECT_EQ(page.text_items().size(), 2);
  EXPECT_EQ(page.image_items().size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, PaddingReducesContentArea) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hello", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // Page 200Ã—100 with padding 20 â†’ content area 160Ã—60
  // Line height 16 â†’ fits floor(60/16) = 3 lines
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 20, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.text_items().size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, EmptyChapter) {
  Chapter ch;
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_TRUE(page.at_chapter_end);
  EXPECT_EQ(page.text_items().size(), 0);
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
    bool advanced = page.at_chapter_end || page.end.offset > 0 || page.end.paragraph > 0;
    pages.push_back(std::move(page));
    if (pages.back().at_chapter_end)
      break;
    tl.set_position(pages.back().end);
    ASSERT_TRUE(pages.back().end.offset > 0 || pages.back().end.paragraph > 0) << "Page didn't advance position";
  }

  EXPECT_TRUE(pages.back().at_chapter_end);
  EXPECT_GE(pages.size(), 3);  // 100 words should need multiple pages

  // Verify all words are accounted for
  size_t total = 0;
  for (auto& p : pages) {
    for (auto& t : p.text_items()) {
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

  for (size_t i = 1; i < page.text_items().size(); ++i) {
    EXPECT_GT(page.text_items()[i].y_offset, page.text_items()[i - 1].y_offset)
        << "Y offsets must increase monotonically";
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

  for (auto& item : page.text_items()) {
    for (size_t i = 1; i < item.line.words.size(); ++i) {
      EXPECT_GT(item.line.words[i].x, item.line.words[i - 1].x) << "X positions must increase within a line";
    }
  }
}
TEST(PageLayout, ImageZeroDimensionsSkipped) {
  // Images with 0x0 dimensions should be skipped entirely
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 0, 0));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items().size(), 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageZeroWidthSkipped) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 0, 50));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items().size(), 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageZeroHeightSkipped) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 0));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  EXPECT_EQ(page.image_items().size(), 0);
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageScaledToFitPageHeight) {
  // Image taller than page height is proportionally scaled to fit on one page.
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 2000));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);  // content area 200x100
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_EQ(page.image_items()[0].height, 100);  // scaled to page height
  EXPECT_EQ(page.image_items()[0].y_crop, 0);
  EXPECT_GT(page.image_items()[0].width, 0);
  EXPECT_TRUE(page.at_chapter_end);  // fits on one page
}

TEST(PageLayout, ImageScaledBothDimensions) {
  // Image larger than page in both dimensions â€” width-scaled first, then sliced vertically
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 1000, 2000));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_LE(page.image_items()[0].width, 200);
  EXPECT_EQ(page.image_items()[0].height, 100);  // sliced to page height
  EXPECT_GT(page.image_items()[0].width, 0);
}

TEST(PageLayout, ImageFillsEntirePage) {
  // Image exactly fills page height â€” should be placed as first item
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 200, 100));
  TestChapterSource src(ch);

  PageOptions opts(200, 100, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_EQ(page.image_items()[0].width, 200);
  EXPECT_EQ(page.image_items()[0].height, 100);
  EXPECT_EQ(page.image_items()[0].y_offset, 0u);
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

  EXPECT_EQ(page.image_items().size(), 0);  // 0x0 image skipped
  EXPECT_EQ(page.text_items().size(), 2);   // both text paragraphs present
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageWithPaddingUsesFullWidth) {
  // Images use full page width (no padding margins), centered
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 800, 600));
  TestChapterSource src(ch);

  // Page 600x800 with padding 20 â†’ images scale to full 600x800
  PageOptions opts(600, 800, 20, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_LE(page.image_items()[0].width, 600);   // â‰¤ full page width
  EXPECT_LE(page.image_items()[0].height, 800);  // â‰¤ full page height
  // Should be centered: x_offset = (600 - actual_w) / 2
  EXPECT_EQ(page.image_items()[0].x_offset, (600 - page.image_items()[0].width) / 2);
}

TEST(PageLayout, MultipleImagesStack) {
  // Two images should stack on the same page when they fit
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 15));
  ch.paragraphs.push_back(Paragraph::make_image(2, 100, 15));

  // 100x15 scales up to 200x30 each. Two images + spacing: 30 + 4 + 30 = 64 â‰¤ 100
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 0, 4);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 2);
  // Two 200x30 images + 4 spacing = 64px on 100px page; center = (100-64)/2 = 18
  EXPECT_EQ(page.image_items()[0].y_offset, 18u);
  EXPECT_EQ(page.image_items()[1].y_offset, 52u);  // 18 + 30 + 4
  EXPECT_TRUE(page.at_chapter_end);
}

TEST(PageLayout, ImageNeverExceedsPageBounds) {
  // Stress test: various extreme image sizes should never exceed full page
  PageOptions opts(600, 800, 20, 8);
  uint16_t page_w = opts.width;   // 600 â€” images use full width
  uint16_t page_h = opts.height;  // 800 â€” images use full height

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

    ASSERT_EQ(page.image_items().size(), 1) << "Image " << tc.w << "x" << tc.h << " should be placed";
    EXPECT_LE(page.image_items()[0].width, page_w) << "Image " << tc.w << "x" << tc.h << " width overflow";
    EXPECT_LE(page.image_items()[0].height, page_h) << "Image " << tc.w << "x" << tc.h << " height overflow";
    EXPECT_GT(page.image_items()[0].width, 0) << "Image " << tc.w << "x" << tc.h << " collapsed to 0 width";
    EXPECT_GT(page.image_items()[0].height, 0) << "Image " << tc.w << "x" << tc.h << " collapsed to 0 height";
    // Images must be centered
    EXPECT_EQ(page.image_items()[0].x_offset, (page_w - page.image_items()[0].width) / 2)
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

  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
  // 100x50 scaled up to 200x100 on 200px page â†’ vertical_offset = (200-100)/2 = 50
  EXPECT_EQ(page.image_items()[0].y_offset, 50u);
}

TEST(PageLayout, ImageOnlyWithPaddingCenteredOnFullScreen) {
  // Image centering uses full screen height, not just the content area
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 50));
  TestChapterSource src(ch);

  // Page 200x200, padding 20 â†’ content area 160x160
  PageOptions opts(200, 200, 20, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
  // 100x50 scaled up to 200x100 â†’ centered on full 200px screen: (200-100)/2 = 50
  EXPECT_EQ(page.image_items()[0].y_offset, 50u);
}

TEST(PageLayout, SparseTextVerticallyCentered) {
  // A single short line on a tall page â€” no centering (centering was removed)
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  TestChapterSource src(ch);

  // 16px of text on 200px page â†’ NOT centered (flush to top)
  PageOptions opts(200, 200, 0);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_EQ(page.text_items().size(), 1);
  EXPECT_TRUE(page.at_chapter_end);
  // padding_top is now baked into y_offsets
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

  ASSERT_EQ(page.text_items().size(), 1);
  // Text should start at y=110 (spacing_before=100 + padding_top=10 baked in), not y=0
  EXPECT_EQ(page.text_items()[0].y_offset, 110);
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

  ASSERT_EQ(page.text_items().size(), 1);
  // Should NOT have the 100px top margin since we're mid-chapter
  // y = opts.padding_top (baked in)
  EXPECT_EQ(page.text_items()[0].y_offset, opts.padding_top);
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

  // Full page â†’ vertical_offset is normal top padding (no extra centering)
  // padding_top is now baked into y_offsets
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

  // Has both text and image â†’ vertical_offset is normal top padding (no extra centering)
  // padding_top is now baked into y_offsets
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
  // Last page should NOT be extra-centered â€” vertical_offset equals normal top padding
  // padding_top is now baked into y_offsets
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

  ASSERT_FALSE(page.text_items().empty());
  ASSERT_FALSE(page.image_items().empty());

  // Check no image overlaps any text item
  for (const auto& img : page.image_items()) {
    for (const auto& txt : page.text_items()) {
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
  for (const auto& txt : page.text_items()) {
    ranges.push_back({txt.y_offset, static_cast<uint16_t>(txt.y_offset + font8.y_advance())});
  }
  for (const auto& img : page.image_items()) {
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

  ASSERT_TRUE(page.text_items()[0].inline_image.has_value());
  const auto img = page.text_items()[0].inline_image.value();  // copy before temp vector dies

  EXPECT_EQ(img.width, 80);
  EXPECT_EQ(img.height, 75);

  // First text line y_offset
  ASSERT_FALSE(page.text_items().empty());
  uint16_t first_line_top = page.text_items()[0].y_offset;
  // ti.baseline includes the inline_extra space above the text within the item
  uint16_t baseline_y = first_line_top + page.text_items()[0].baseline;

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

  ASSERT_GE(page.text_items().size(), 2u);

  // First line: words should start at image_width + gap
  uint16_t first_word_x = page.text_items()[0].line.words[0].x;
  EXPECT_EQ(first_word_x, 84u) << "First line should be indented by 80 + 4 gap";

  // Second line: words should start at 0 (full width)
  uint16_t second_word_x = page.text_items()[1].line.words[0].x;
  EXPECT_EQ(second_word_x, 0u) << "Second line should use full width";
}

TEST(PageLayout, SmallInlineImageSameAsBaseline) {
  // 12x12 image â€” same height as baseline (12px). Bottom aligns with baseline,
  // so image top = first line top.
  auto ch = make_inline_image_chapter(12, 12, "Hello world this is a test of a small inline icon");
  TestChapterSource src(ch);
  PageOptions opts(300, 400, 0, 10);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_TRUE(page.text_items()[0].inline_image.has_value());
  const auto img = page.text_items()[0].inline_image.value();  // copy before temp vector dies

  EXPECT_EQ(img.width, 12);
  EXPECT_EQ(img.height, 12);

  uint16_t first_line_top = page.text_items()[0].y_offset;
  // For a 12px image with 12px baseline: image top == first line top
  EXPECT_EQ(img.y_offset, first_line_top) << "12px image should start at first line top";

  // First word indented by 12 + 4 = 16
  EXPECT_EQ(page.text_items()[0].line.words[0].x, 16u);
}

TEST(PageLayout, TallInlineImageExtendsAboveFirstLine) {
  // 40x48 image â€” taller than baseline (12px). Image extends above the first line.
  auto ch = make_inline_image_chapter(40, 48, "Hello world this is a test of a tall inline image");
  TestChapterSource src(ch);
  PageOptions opts(300, 400, 0, 10);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_TRUE(page.text_items()[0].inline_image.has_value());
  const auto img = page.text_items()[0].inline_image.value();  // copy before temp vector dies

  uint16_t first_line_top = page.text_items()[0].y_offset;
  // ti.baseline includes the inline_extra space above the text within the item
  uint16_t baseline_y = first_line_top + page.text_items()[0].baseline;

  // Image bottom aligns with baseline
  EXPECT_EQ(img.y_offset + img.height, baseline_y);
  EXPECT_EQ(img.y_offset, baseline_y - 48);

  // First line indented by 40 + 4 = 44
  EXPECT_EQ(page.text_items()[0].line.words[0].x, 44u);
}

TEST(PageLayout, InlineImageAtLeftEdge) {
  // Image x should be at padding offset, not centered
  auto ch = make_inline_image_chapter(60, 30, "Some text after the image goes here");
  TestChapterSource src(ch);
  PageOptions opts(400, 400, 15, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_TRUE(page.text_items()[0].inline_image.has_value());
  // x_offset should be padding value
  EXPECT_EQ(page.text_items()[0].inline_image.value().x_offset, 15u) << "Inline image should be at left padding edge";
}

TEST(PageLayout, InlineImageNoOverlapWithText) {
  // Verify no text line occupies the same vertical+horizontal space as the image
  auto ch = make_inline_image_chapter(
      80, 75,
      "URIOUSER and curiouser cried Alice she was so much surprised that for a moment she quite forgot how to speak");
  TestChapterSource src(ch);
  PageOptions opts(DrawBuffer::kWidth, DrawBuffer::kHeight, 0, 20);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_TRUE(page.text_items()[0].inline_image.has_value());
  const auto img = page.text_items()[0].inline_image.value();  // copy before temp vector dies
  uint16_t img_left = img.x_offset;
  uint16_t img_right = img.x_offset + img.width;
  uint16_t img_top = img.y_offset;
  uint16_t img_bot = img.y_offset + img.height;

  for (const auto& ti : page.text_items()) {
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

  // Find the text item for paragraph 1 (which has the inline image)
  const PageTextItem* inline_para = nullptr;
  for (const auto& ti : page.text_items())
    if (ti.paragraph_index == 1 && ti.line_index == 0) {
      inline_para = &ti;
      break;
    }
  ASSERT_NE(inline_para, nullptr);
  ASSERT_TRUE(inline_para->inline_image.has_value());
  const auto img = inline_para->inline_image.value();
  uint16_t img_top = img.y_offset;
  uint16_t img_bot = img.y_offset + img.height;

  EXPECT_GE(img_top, 0u) << "Inline image must not start above the page top";

  for (const auto& ti : page.text_items()) {
    if (ti.paragraph_index >= img.paragraph_index)
      continue;

    uint16_t text_top = ti.y_offset;
    uint16_t text_bot = static_cast<uint16_t>(ti.y_offset + ti.height);
    EXPECT_LE(text_bot, img_top) << "Inline image at y=" << img_top << " overlaps previous text line spanning ["
                                 << text_top << ", " << text_bot << ")";
  }
}

TEST(PageLayout, SpreadWithInlineImageDoesNotDistortLineGaps) {
  // Regression: non-promoted inline images were included as slots in the
  // spreading algorithm, causing distorted gaps between text lines. The inline
  // image must be invisible to the spreading logic ï¿½ line gaps for lines 1+
  // should be uniform ï¿½ and the image must stay anchored to the first line's
  // baseline after spreading.
  auto ch = make_inline_image_chapter(
      80, 75,
      "URIOUSER and curiouser cried Alice she was so much surprised that for a moment she quite forgot how to speak "
      "good gracious how she had grown");
  {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("A second paragraph with some words.", FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }
  TestChapterSource src(ch);

  PageOptions opts(300, 200, 0, 8);
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_GE(page.text_items().size(), 4u);
  ASSERT_TRUE(page.text_items()[0].inline_image.has_value());

  // Collect y-offsets for lines in paragraph 0.
  std::vector<uint16_t> tops;
  for (const auto& ti : page.text_items())
    if (ti.paragraph_index == 0)
      tops.push_back(ti.y_offset);
  ASSERT_GE(tops.size(), 4u) << "Need at least 4 lines in paragraph 0 to check gaps";

  // Lines 1, 2, 3, ... must have approximately equal spacing (Â±1px rounding allowed).
  // Line 0 is taller due to inline_extra.
  const uint16_t gap12 = tops[2] - tops[1];
  for (size_t i = 3; i < tops.size(); ++i) {
    uint16_t gap = tops[i] - tops[i - 1];
    EXPECT_LE(gap, gap12 + 1) << "Line gap between lines " << i - 1 << " and " << i << " too large vs gap12 (" << gap12
                              << "px)";
    EXPECT_GE(gap, gap12 > 0 ? gap12 - 1 : 0u)
        << "Line gap between lines " << i - 1 << " and " << i << " too small vs gap12 (" << gap12 << "px)";
  }

  // Image must remain anchored to the baseline of the first text line.
  const auto img = page.text_items()[0].inline_image.value();  // copy before temp vector dies
  uint16_t baseline_y = page.text_items()[0].y_offset + page.text_items()[0].baseline;
  EXPECT_EQ(img.y_offset + img.height, baseline_y)
      << "After spreading, image bottom must still align with first-line baseline";
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
  EXPECT_TRUE(page.text_items().empty());
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
  ASSERT_EQ(page.text_items().size(), 1);
  EXPECT_EQ(line_text(page.text_items()[0].line), "Hello world");
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
  ASSERT_EQ(page.text_items().size(), 3);
  EXPECT_EQ(line_text(page.text_items()[0].line), "First paragraph");
  EXPECT_EQ(line_text(page.text_items()[1].line), "Second paragraph");
  EXPECT_EQ(line_text(page.text_items()[2].line), "Third paragraph");
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
  ASSERT_EQ(page.text_items().size(), 2);
  EXPECT_EQ(line_text(page.text_items()[0].line), "Second");
  EXPECT_EQ(line_text(page.text_items()[1].line), "Third");
}

TEST(PageLayoutBackward, MultiLineParagraphSplit) {
  // A paragraph with multiple lines. Backward should pick trailing lines.
  Chapter ch;
  TextParagraph tp;
  // 10 words Ã— 8px = 80px per word. Width 100px â†’ ~1 word per line.
  tp.runs.push_back(microreader::Run("aaa bbb ccc ddd eee", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));

  // Width 32px: each 3-letter word = 24px, fits one per line. 5 lines Ã— 16px = 80px.
  // Page height 32 â†’ fits 2 lines.
  TestChapterSource src(ch);
  PageOptions opts(32, 32, 0, 0);
  TextLayout tl(font8, opts, src, PagePosition(1, 0));
  auto page = tl.layout_backward();

  ASSERT_EQ(page.text_items().size(), 2);
  // Should get the last 2 lines (ddd, eee)
  EXPECT_EQ(page.start.paragraph, 0);
  EXPECT_EQ(page.start.offset, 3);  // lines 3 and 4
  EXPECT_EQ(line_text(page.text_items()[0].line), "ddd");
  EXPECT_EQ(line_text(page.text_items()[1].line), "eee");
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
  ASSERT_EQ(page.text_items().size(), 3);
  EXPECT_EQ(page.start, PagePosition(0, 0));
  EXPECT_EQ(page.end, PagePosition(0, 3));
  EXPECT_EQ(line_text(page.text_items()[0].line), "aaa");
  EXPECT_EQ(line_text(page.text_items()[1].line), "bbb");
  EXPECT_EQ(line_text(page.text_items()[2].line), "ccc");
}

TEST(PageLayoutBackward, ImageParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_image(1, 100, 50));
  TestChapterSource src_bwd(ch);

  PageOptions opts(200, 200, 10, 8);
  TextLayout tl(font8, opts, src_bwd, PagePosition(1, 0));
  auto page = tl.layout_backward();

  EXPECT_EQ(page.start, PagePosition(0, 0));
  ASSERT_EQ(page.image_items().size(), 1);
  EXPECT_EQ(page.image_items()[0].key, 1);
}

TEST(PageLayoutBackward, HrParagraph) {
  Chapter ch;
  ch.paragraphs.push_back(Paragraph::make_hr());
  TestChapterSource src_hr(ch);

  PageOptions opts(200, 200, 10, 8);
  TextLayout tl(font8, opts, src_hr, PagePosition(1, 0));
  auto page = tl.layout_backward();

  EXPECT_EQ(page.start, PagePosition(0, 0));
  ASSERT_EQ(page.hr_items().size(), 1);
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

  // Should only get "After break" â€” page break stops backward scan
  ASSERT_EQ(page.text_items().size(), 1);
  EXPECT_EQ(line_text(page.text_items()[0].line), "After break");
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
  EXPECT_EQ(back2.text_items().size(), page2.text_items().size());
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

  ASSERT_EQ(fwd.text_items().size(), bwd.text_items().size());
  for (size_t i = 0; i < fwd.text_items().size(); ++i) {
    EXPECT_EQ(fwd.text_items()[i].y_offset, bwd.text_items()[i].y_offset) << "y_offset mismatch at item " << i;
  }
}

TEST(PageLayout, PromotedInlineImageSlicedWhenNoSpace) {
  // A text paragraph with a promoted inline image is sliced across pages,
  // just like a standalone image paragraph.
  //
  // Page: 200x200, no padding, para_spacing=0. font8: y_advance=16.
  // Para 0: 3 lines -> 48px used.
  // Para 1: 3 lines -> 48px used. Total: 96px used, 104px remaining.
  // Para 2: text with promoted inline image (200x150 -> promoted_h=150 > 104px avail).
  //         Page 1: 6 text lines + 104px image slice.
  //         Page 2: remaining 46px image slice + 1 text line ("Caption").

  Chapter ch;

  // Para 0: 3 lines via breaking runs
  TextParagraph tp0;
  tp0.runs.push_back(microreader::Run("Alpha", FontStyle::Regular, false));
  tp0.runs.back().breaking = true;
  tp0.runs.push_back(microreader::Run("Beta", FontStyle::Regular, false));
  tp0.runs.back().breaking = true;
  tp0.runs.push_back(microreader::Run("Gamma", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp0)));

  // Para 1: 3 lines
  TextParagraph tp1;
  tp1.runs.push_back(microreader::Run("Delta", FontStyle::Regular, false));
  tp1.runs.back().breaking = true;
  tp1.runs.push_back(microreader::Run("Epsilon", FontStyle::Regular, false));
  tp1.runs.back().breaking = true;
  tp1.runs.push_back(microreader::Run("Zeta", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp1)));

  // Para 2: text with a large promoted inline image (width=200 > cw/3 -> promoted)
  TextParagraph tp2;
  tp2.runs.push_back(microreader::Run("Caption", FontStyle::Regular, false));
  tp2.inline_image = ImageRef(99, 200, 150);  // 150px > 104px remaining -> sliced
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp2)));

  TestChapterSource src(ch);
  PageOptions opts(200, 200, 0, 0);
  TextLayout tl(font8, opts, src);
  auto page1 = tl.layout();

  // Page 1: 6 text lines + first 104px slice of the image
  EXPECT_EQ(page1.text_items().size(), 6u);
  ASSERT_EQ(page1.image_items().size(), 1u) << "first image slice must appear on page 1";
  EXPECT_EQ(page1.image_items()[0].height, 104u) << "slice height = 104px (200-96)";
  EXPECT_EQ(page1.image_items()[0].y_crop, 0u);
  EXPECT_EQ(page1.image_items()[0].full_height, 150u);
  EXPECT_FALSE(page1.at_chapter_end);

  // Page 2: remaining 46px slice + caption text line
  tl.set_position(page1.end);
  auto page2 = tl.layout();
  ASSERT_GE(page2.image_items().size(), 1u) << "second image slice must appear on page 2";
  EXPECT_EQ(page2.image_items()[0].height, 46u) << "remaining slice = 46px";
  EXPECT_EQ(page2.image_items()[0].y_crop, 104u);
  EXPECT_GE(page2.text_items().size(), 1u) << "caption text must follow on page 2";
  EXPECT_TRUE(page2.at_chapter_end);
}

// ---------------------------------------------------------------------------
// Bounds checks — items must stay within the padded area
// ---------------------------------------------------------------------------

// Verify that every item on `page` has y_offset + height <= opts.height - opts.padding_bottom.
static void assert_items_in_bounds(const PageContent& page, const PageOptions& opts, const std::string& context = "") {
  const uint16_t bottom_bound = opts.height - opts.padding_bottom;
  for (const auto& ci : page.items) {
    std::visit(
        [&](const auto& it) {
          uint16_t item_bottom = it.y_offset + it.height;
          EXPECT_LE(item_bottom, bottom_bound)
              << context << " item bottom=" << item_bottom << " exceeds bound=" << bottom_bound;
        },
        ci);
  }
}

TEST(PageLayout, ItemsNeverExceedBottomPadding) {
  // A page full of single-line paragraphs: all items must stay inside the padded area.
  Chapter ch;
  for (int i = 0; i < 20; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Line " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 6, 0, Alignment::Start);
  opts.padding_bottom = 14;
  opts.center_text = true;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();
  assert_items_in_bounds(page, opts, "ItemsNeverExceedBottomPadding");
}

TEST(PageLayout, ChapterStartSpacingBeforeDoesNotOverflow) {
  // Regression: when the first paragraph of a chapter has a large spacing_before,
  // collected items must still fit within the padded content area after bake_y.
  // Previously, build_page_items added spacing_before to y without the collection
  // having budgeted for it, causing the last items to overflow padding_bottom.
  Chapter ch;
  // First paragraph: spacing_before=20px (simulates CSS margin-top from a heading style).
  TextParagraph tp0;
  tp0.runs.push_back(microreader::Run("Chapter title", FontStyle::Regular, false));
  auto para0 = Paragraph::make_text(std::move(tp0));
  para0.spacing_before = 20;
  ch.paragraphs.push_back(std::move(para0));
  // Fill remaining space with more paragraphs so the page is nearly full.
  for (int i = 0; i < 4; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Body line " + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }
  TestChapterSource src(ch);
  // Page: 100px padded area (padding_top=6, padding_bottom=14 → padded_h = 100-6-14 = 80).
  // Items: 5 lines × 16px + 4 gaps × 8px = 80+32 = doesn't all fit — tests whatever fits.
  PageOptions opts(200, 100, 6, 8, Alignment::Start);
  opts.padding_bottom = 14;
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();
  assert_items_in_bounds(page, opts, "ChapterStartSpacingBeforeDoesNotOverflow");
  // Also verify the first item has the top margin applied.
  ASSERT_FALSE(page.text_items().empty());
  EXPECT_EQ(page.text_items()[0].y_offset, uint16_t(opts.padding_top + 20))
      << "First item should be below padding_top + spacing_before";
}

TEST(PageLayout, AllPagesInBoundsMultiPage) {
  // Walk all pages of a chapter with varied paragraph spacings and verify bounds on each.
  Chapter ch;
  for (int i = 0; i < 30; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run("Paragraph " + std::to_string(i), FontStyle::Regular, false));
    auto para = Paragraph::make_text(std::move(tp));
    para.spacing_before = static_cast<uint16_t>((i % 3) * 4);  // 0, 4, or 8px spacing
    ch.paragraphs.push_back(std::move(para));
  }
  TestChapterSource src(ch);
  PageOptions opts(200, 100, 6, 4, Alignment::Start);
  opts.padding_bottom = 14;
  opts.center_text = true;

  TextLayout tl(font8, opts, src);
  int page_num = 0;
  while (true) {
    auto page = tl.layout();
    assert_items_in_bounds(page, opts, "page " + std::to_string(++page_num));
    if (page.at_chapter_end)
      break;
    tl.set_position(page.end);
    if (page_num > 100)
      break;  // safety
  }
}
