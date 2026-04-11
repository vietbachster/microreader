#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "microreader/content/CssParser.h"

using namespace microreader;

// ---------------------------------------------------------------------------
// CssRule::parse
// ---------------------------------------------------------------------------

TEST(CssRule, Empty) {
  auto rule = CssRule::parse("");
  EXPECT_FALSE(rule.has_any());
}

TEST(CssRule, TextAlign) {
  auto rule = CssRule::parse("text-align: center");
  ASSERT_TRUE(rule.alignment.has_value());
  EXPECT_EQ(*rule.alignment, Alignment::Center);
}

TEST(CssRule, FontWeight) {
  auto rule = CssRule::parse("font-weight: bold");
  ASSERT_TRUE(rule.bold.has_value());
  EXPECT_TRUE(*rule.bold);
}

TEST(CssRule, FontStyle) {
  auto rule = CssRule::parse("font-style: italic");
  ASSERT_TRUE(rule.italic.has_value());
  EXPECT_TRUE(*rule.italic);
}

TEST(CssRule, TextIndent) {
  auto rule = CssRule::parse("text-indent: 20px");
  ASSERT_TRUE(rule.indent.has_value());
  EXPECT_EQ(*rule.indent, 20);
}

TEST(CssRule, MultipleDeclarations) {
  auto rule = CssRule::parse("text-align: justify; font-weight: bold; font-style: italic; text-indent: 10px");
  ASSERT_TRUE(rule.alignment.has_value());
  EXPECT_EQ(*rule.alignment, Alignment::Justify);
  ASSERT_TRUE(rule.bold.has_value());
  EXPECT_TRUE(*rule.bold);
  ASSERT_TRUE(rule.italic.has_value());
  EXPECT_TRUE(*rule.italic);
  ASSERT_TRUE(rule.indent.has_value());
  EXPECT_EQ(*rule.indent, 10);
}

TEST(CssRule, AlignmentLeft) {
  EXPECT_EQ(*CssRule::parse("text-align: left").alignment, Alignment::Start);
}

TEST(CssRule, AlignmentRight) {
  EXPECT_EQ(*CssRule::parse("text-align: right").alignment, Alignment::End);
}

TEST(CssRule, AlignmentStart) {
  EXPECT_EQ(*CssRule::parse("text-align: start").alignment, Alignment::Start);
}

TEST(CssRule, NormalWeight) {
  auto rule = CssRule::parse("font-weight: normal");
  ASSERT_TRUE(rule.bold.has_value());
  EXPECT_FALSE(*rule.bold);
}

TEST(CssRule, NormalStyle) {
  auto rule = CssRule::parse("font-style: normal");
  ASSERT_TRUE(rule.italic.has_value());
  EXPECT_FALSE(*rule.italic);
}

TEST(CssRule, Plus) {
  CssRule a;
  a.alignment = Alignment::Center;
  CssRule b;
  b.bold = true;
  auto c = a + b;
  EXPECT_EQ(*c.alignment, Alignment::Center);
  EXPECT_TRUE(*c.bold);
}

TEST(CssRule, PlusOverrides) {
  CssRule a;
  a.alignment = Alignment::Center;
  a.bold = false;
  CssRule b;
  b.alignment = Alignment::Justify;
  auto c = a + b;
  EXPECT_EQ(*c.alignment, Alignment::Justify);
  EXPECT_FALSE(*c.bold);  // not overridden
}

// ---------------------------------------------------------------------------
// CssConfig: em and % conversion
// ---------------------------------------------------------------------------

TEST(CssRule, TextIndentEm) {
  CssConfig config{10, 400};  // 10px glyph, 400px content
  auto rule = CssRule::parse("text-indent: 1.25em", config);
  ASSERT_TRUE(rule.indent.has_value());
  EXPECT_EQ(*rule.indent, 13);  // 1.25 * 10 + 0.5 = 13
}

TEST(CssRule, TextIndentPercent) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("text-indent: 5%", config);
  ASSERT_TRUE(rule.indent.has_value());
  EXPECT_EQ(*rule.indent, 20);  // 5 * 400 / 100 = 20
}

TEST(CssRule, MarginLeftEm) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-left: 3em", config);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 30);  // 3 * 10 = 30
}

TEST(CssRule, MarginLeftPercentClamped) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-left: 30%", config);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 120);  // 30% of 400 = 120
}

TEST(CssRule, MarginLeftPercentUnderMax) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-left: 5%", config);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 20);  // 5% of 400 = 20, under max
}

TEST(CssRule, MarginRightEm) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-right: 2em", config);
  ASSERT_TRUE(rule.margin_right.has_value());
  EXPECT_EQ(*rule.margin_right, 20);  // 2 * 10 = 20
}

TEST(CssRule, MarginRightPercentClamped) {
  CssConfig config{10, 400};
  auto rule = CssRule::parse("margin-right: 20%", config);
  ASSERT_TRUE(rule.margin_right.has_value());
  EXPECT_EQ(*rule.margin_right, 80);  // 20% of 400 = 80
}

TEST(CssRule, BothMarginsClampedProportionally) {
  CssConfig config{10, 400, 15};  // 15% budget = 60px total
  auto rule = CssRule::parse("margin-left: 30%; margin-right: 10%", config);
  ASSERT_TRUE(rule.margin_left.has_value());
  ASSERT_TRUE(rule.margin_right.has_value());
  // 30% of 400 = 120, 10% of 400 = 40. Total 160 > 60. Scale = 60/160 = 0.375
  EXPECT_EQ(*rule.margin_left, 45);   // 120 * 0.375 = 45
  EXPECT_EQ(*rule.margin_right, 15);  // 40 * 0.375 = 15
}

TEST(CssRule, BothMarginsUnderBudgetNotClamped) {
  CssConfig config{10, 400, 15};  // 15% budget = 60px total
  auto rule = CssRule::parse("margin-left: 5%; margin-right: 5%", config);
  ASSERT_TRUE(rule.margin_left.has_value());
  ASSERT_TRUE(rule.margin_right.has_value());
  EXPECT_EQ(*rule.margin_left, 20);  // 5% of 400 = 20, under budget
  EXPECT_EQ(*rule.margin_right, 20);
}

TEST(CssRule, SingleMarginNotClamped) {
  CssConfig config{10, 400, 15};
  // Only margin-left set — no combined clamping
  auto rule = CssRule::parse("margin-left: 30%", config);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_FALSE(rule.margin_right.has_value());
  EXPECT_EQ(*rule.margin_left, 120);  // 30% of 400 = 120, unclamped
}

// ---------------------------------------------------------------------------
// CssStylesheet
// ---------------------------------------------------------------------------

TEST(CssStylesheet, BasicElementRule) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("p { text-align: justify; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.alignment.has_value());
  EXPECT_EQ(*rule.alignment, Alignment::Justify);
}

TEST(CssStylesheet, NoMatch) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("p { text-align: justify; }");
  auto rule = sheet.get("div", nullptr, nullptr);
  EXPECT_FALSE(rule.has_any());
}

TEST(CssStylesheet, ClassSelector) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(".bold { font-weight: bold; }");
  auto rule = sheet.get("span", nullptr, "bold");
  ASSERT_TRUE(rule.bold.has_value());
  EXPECT_TRUE(*rule.bold);
}

TEST(CssStylesheet, IdSelector) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("#title { text-align: center; }");
  auto rule = sheet.get("h1", "title", nullptr);
  ASSERT_TRUE(rule.alignment.has_value());
  EXPECT_EQ(*rule.alignment, Alignment::Center);
}

TEST(CssStylesheet, CompoundSelector) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("p.intro { font-style: italic; }");

  auto rule = sheet.get("p", nullptr, "intro");
  ASSERT_TRUE(rule.italic.has_value());
  EXPECT_TRUE(*rule.italic);

  // Should not match div
  auto no_match = sheet.get("div", nullptr, "intro");
  EXPECT_FALSE(no_match.has_any());
}

TEST(CssStylesheet, MultipleRules) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "p { text-align: justify; }\n"
      "h1 { font-weight: bold; text-align: center; }\n"
      ".italic { font-style: italic; }\n");

  auto p_rule = sheet.get("p", nullptr, nullptr);
  EXPECT_EQ(*p_rule.alignment, Alignment::Justify);

  auto h1_rule = sheet.get("h1", nullptr, nullptr);
  EXPECT_EQ(*h1_rule.alignment, Alignment::Center);
  EXPECT_TRUE(*h1_rule.bold);
}

TEST(CssStylesheet, Comments) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("/* comment */ p { /* inline */ text-align: center; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.alignment.has_value());
  EXPECT_EQ(*rule.alignment, Alignment::Center);
}

TEST(CssStylesheet, AtRuleSkipped) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "@charset \"utf-8\";\n"
      "@import url(\"styles.css\");\n"
      "p { text-align: justify; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.alignment.has_value());
  EXPECT_EQ(*rule.alignment, Alignment::Justify);
}

TEST(CssStylesheet, MediaQuerySkipped) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "@media screen { body { margin: 0; } }\n"
      "p { font-weight: bold; }");
  auto rule = sheet.get("p", nullptr, nullptr);
  ASSERT_TRUE(rule.bold.has_value());
  EXPECT_TRUE(*rule.bold);
}

TEST(CssStylesheet, GroupedSelectors) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("h1, h2, h3 { font-weight: bold; }");

  EXPECT_TRUE(*sheet.get("h1", nullptr, nullptr).bold);
  EXPECT_TRUE(*sheet.get("h2", nullptr, nullptr).bold);
  EXPECT_TRUE(*sheet.get("h3", nullptr, nullptr).bold);
  EXPECT_FALSE(sheet.get("h4", nullptr, nullptr).has_any());
}

TEST(CssStylesheet, Specificity) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(
      "p { text-align: left; }\n"
      ".center { text-align: center; }\n"
      "#main { text-align: right; }\n");

  // #main has highest specificity
  auto rule = sheet.get("p", "main", "center");
  EXPECT_EQ(*rule.alignment, Alignment::End);  // right
}

TEST(CssStylesheet, MultipleClasses) {
  CssStylesheet sheet;
  sheet.extend_from_sheet(".bold { font-weight: bold; } .italic { font-style: italic; }");

  auto rule = sheet.get("span", nullptr, "bold italic");
  EXPECT_TRUE(*rule.bold);
  EXPECT_TRUE(*rule.italic);
}

// ---------------------------------------------------------------------------
// Font-size parsing
// ---------------------------------------------------------------------------

TEST(CssParserTest, ParseFontSizeSmall) {
  auto rule = CssRule::parse("font-size: small");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, ParseFontSizeLarge) {
  auto rule = CssRule::parse("font-size: large");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Large);
}

TEST(CssParserTest, ParseFontSizeXLarge) {
  auto rule = CssRule::parse("font-size: x-large");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XLarge);
}

TEST(CssParserTest, ParseFontSizeSmaller) {
  auto rule = CssRule::parse("font-size: smaller");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, ParseFontSizeMedium) {
  auto rule = CssRule::parse("font-size: medium");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Normal);
}

TEST(CssParserTest, ParseFontSizeUnknownIgnored) {
  auto rule = CssRule::parse("font-size: 14px");
  EXPECT_FALSE(rule.font_size.has_value());
}

TEST(CssParserTest, FontSizeMerge) {
  CssRule a;
  a.font_size = FontSize::Small;
  CssRule b;
  b.font_size = FontSize::Large;

  auto merged = a + b;
  ASSERT_TRUE(merged.font_size.has_value());
  EXPECT_EQ(*merged.font_size, FontSize::Large);  // rhs wins
}

TEST(CssParserTest, FontSizeMergePreservesLhs) {
  CssRule a;
  a.font_size = FontSize::Small;
  CssRule b;

  auto merged = a + b;
  ASSERT_TRUE(merged.font_size.has_value());
  EXPECT_EQ(*merged.font_size, FontSize::Small);  // lhs preserved when rhs empty
}

TEST(CssParserTest, FontSizeInStylesheet) {
  CssStylesheet sheet;
  sheet.extend_from_sheet("h1 { font-size: large; } .footnote { font-size: small; }");

  auto h1 = sheet.get("h1", nullptr, nullptr);
  ASSERT_TRUE(h1.font_size.has_value());
  EXPECT_EQ(*h1.font_size, FontSize::Large);

  auto fn = sheet.get("span", nullptr, "footnote");
  ASSERT_TRUE(fn.font_size.has_value());
  EXPECT_EQ(*fn.font_size, FontSize::Small);
}

// ---------------------------------------------------------------------------
// Font-size: percentage values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizePercent90IsNormal) {
  // 90% is within the normal band (90-105%)
  auto rule = CssRule::parse("font-size: 90%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Normal);
}

TEST(CssParserTest, FontSizePercent60IsSmall) {
  auto rule = CssRule::parse("font-size: 60%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, FontSizePercent100IsNormal) {
  auto rule = CssRule::parse("font-size: 100%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Normal);
}

TEST(CssParserTest, FontSizePercent110IsLarge) {
  auto rule = CssRule::parse("font-size: 110%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Large);
}

TEST(CssParserTest, FontSizePercent120IsXLarge) {
  auto rule = CssRule::parse("font-size: 120%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XLarge);
}

TEST(CssParserTest, FontSizePercent150IsXXLarge) {
  auto rule = CssRule::parse("font-size: 150%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XXLarge);
}

TEST(CssParserTest, FontSizePercent300IsXXLarge) {
  auto rule = CssRule::parse("font-size: 300%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XXLarge);
}

// ---------------------------------------------------------------------------
// Font-size: em values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizeEm09IsNormal) {
  // 0.9em is within the normal band (0.90-1.05)
  auto rule = CssRule::parse("font-size: 0.9em");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Normal);
}

TEST(CssParserTest, FontSizeEm075IsSmall) {
  auto rule = CssRule::parse("font-size: 0.75em");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, FontSizeEm1IsNormal) {
  auto rule = CssRule::parse("font-size: 1em");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Normal);
}

TEST(CssParserTest, FontSizeEm125IsXLarge) {
  auto rule = CssRule::parse("font-size: 1.25em");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XLarge);
}

TEST(CssParserTest, FontSizeEm175IsXXLarge) {
  auto rule = CssRule::parse("font-size: 1.75em");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XXLarge);
}

// ---------------------------------------------------------------------------
// Font-size: boundary values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizePercent85IsSmall) {
  auto rule = CssRule::parse("font-size: 85%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, FontSizePercent84IsSmall) {
  auto rule = CssRule::parse("font-size: 84%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, FontSizePercent115IsLarge) {
  auto rule = CssRule::parse("font-size: 115%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Large);
}

TEST(CssParserTest, FontSizePercent116IsXLarge) {
  auto rule = CssRule::parse("font-size: 116%");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XLarge);
}

TEST(CssParserTest, FontSizePxIgnored) {
  auto rule = CssRule::parse("font-size: 14px");
  EXPECT_FALSE(rule.font_size.has_value());
}

// ---------------------------------------------------------------------------
// Font-size: pt and rem values
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontSizePt10IsSmall) {
  // 10pt / 12pt base = 0.833, below 0.90 threshold
  auto rule = CssRule::parse("font-size: 10pt");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, FontSizePt12IsNormal) {
  // 12pt / 12pt base = 1.0, within normal band
  auto rule = CssRule::parse("font-size: 12pt");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Normal);
}

TEST(CssParserTest, FontSizePt16IsXXLarge) {
  // 16pt / 12pt base = 1.333, above 1.30 threshold
  auto rule = CssRule::parse("font-size: 16pt");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XXLarge);
}

TEST(CssParserTest, FontSizeRem09IsNormal) {
  auto rule = CssRule::parse("font-size: 0.9rem");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Normal);
}

TEST(CssParserTest, FontSizeRem075IsSmall) {
  auto rule = CssRule::parse("font-size: 0.75rem");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, FontSizeRem15IsXXLarge) {
  auto rule = CssRule::parse("font-size: 1.5rem");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::XXLarge);
}

// ---------------------------------------------------------------------------
// Margin shorthand
// ---------------------------------------------------------------------------

TEST(CssParserTest, MarginShorthand1Value) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 24px", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  ASSERT_TRUE(rule.margin_right.has_value());
  EXPECT_EQ(*rule.margin_left, 24);
  EXPECT_EQ(*rule.margin_right, 24);
}

TEST(CssParserTest, MarginShorthand2Values) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 10px 36px", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  ASSERT_TRUE(rule.margin_right.has_value());
  // Total 72px > budget 66px (15% of 440), clamped proportionally: 36*66/72=33
  EXPECT_EQ(*rule.margin_left, 33);
  EXPECT_EQ(*rule.margin_right, 33);
}

TEST(CssParserTest, MarginShorthand4Values) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 5px 48px 5px 24px", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  ASSERT_TRUE(rule.margin_right.has_value());
  // Total 72px > budget 66px, clamped: L=24*66/72=22, R=48*66/72=44
  EXPECT_EQ(*rule.margin_left, 22);
  EXPECT_EQ(*rule.margin_right, 44);
}

TEST(CssParserTest, MarginShorthandEm) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 2em", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 24);
}

TEST(CssParserTest, MarginShorthandPercent) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 0 10%", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  // 10% of 440 = 44px each side, total 88 > budget 66, clamped: 44*66/88=33
  EXPECT_EQ(*rule.margin_left, 33);
}

TEST(CssParserTest, MarginShorthandZero) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 0", cfg);
  EXPECT_FALSE(rule.margin_left.has_value());
  EXPECT_FALSE(rule.margin_right.has_value());
}

TEST(CssParserTest, MarginShorthandDoesNotOverrideExplicit) {
  CssConfig cfg{12, 440, 15};
  // margin-left explicit should take precedence when merged
  auto shorthand = CssRule::parse("margin: 24px", cfg);
  auto explicit_left = CssRule::parse("margin-left: 48px", cfg);
  auto merged = shorthand + explicit_left;
  ASSERT_TRUE(merged.margin_left.has_value());
  EXPECT_EQ(*merged.margin_left, 48);
}

// ---------------------------------------------------------------------------
// pt and rem unit support
// ---------------------------------------------------------------------------

TEST(CssParserTest, MarginLeftPt) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-left: 12pt", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  // 12pt * 4/3 = 16px
  EXPECT_EQ(*rule.margin_left, 16);
}

TEST(CssParserTest, TextIndentPt) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("text-indent: 18pt", cfg);
  ASSERT_TRUE(rule.indent.has_value());
  // 18pt * 4/3 = 24px
  EXPECT_EQ(*rule.indent, 24);
}

TEST(CssParserTest, MarginLeftRem) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-left: 2rem", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 24);  // 2 * 12 = 24px
}

TEST(CssParserTest, MarginShorthandPt) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 0 9pt", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  // 9pt * 4/3 = 12px
  EXPECT_EQ(*rule.margin_left, 12);
}

// ---------------------------------------------------------------------------
// margin-top and margin-bottom
// ---------------------------------------------------------------------------

TEST(CssParserTest, MarginTopPx) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-top: 16px", cfg);
  ASSERT_TRUE(rule.margin_top.has_value());
  EXPECT_EQ(*rule.margin_top, 16);
}

TEST(CssParserTest, MarginBottomEm) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-bottom: 2em", cfg);
  ASSERT_TRUE(rule.margin_bottom.has_value());
  EXPECT_EQ(*rule.margin_bottom, 24);
}

TEST(CssParserTest, MarginTopZero) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-top: 0", cfg);
  ASSERT_TRUE(rule.margin_top.has_value());
  EXPECT_EQ(*rule.margin_top, 0);
}

TEST(CssParserTest, MarginShorthandExtractsTopBottom) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 16px 24px", cfg);
  ASSERT_TRUE(rule.margin_top.has_value());
  ASSERT_TRUE(rule.margin_bottom.has_value());
  EXPECT_EQ(*rule.margin_top, 16);
  EXPECT_EQ(*rule.margin_bottom, 16);
}

TEST(CssParserTest, MarginShorthand4ValuesTopBottom) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin: 10px 20px 30px 40px", cfg);
  ASSERT_TRUE(rule.margin_top.has_value());
  ASSERT_TRUE(rule.margin_bottom.has_value());
  EXPECT_EQ(*rule.margin_top, 10);
  EXPECT_EQ(*rule.margin_bottom, 30);
}

TEST(CssParserTest, MarginTopBottomMerge) {
  CssConfig cfg{12, 440, 15};
  auto shorthand = CssRule::parse("margin: 10px", cfg);
  auto explicit_top = CssRule::parse("margin-top: 20px", cfg);
  auto merged = shorthand + explicit_top;
  ASSERT_TRUE(merged.margin_top.has_value());
  EXPECT_EQ(*merged.margin_top, 20);  // explicit overrides
  ASSERT_TRUE(merged.margin_bottom.has_value());
  EXPECT_EQ(*merged.margin_bottom, 10);  // from shorthand
}

// ---------------------------------------------------------------------------
// padding (treated as additional margin)
// ---------------------------------------------------------------------------

TEST(CssParserTest, PaddingLeftAddsToMargin) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("padding-left: 12px", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 12);
}

TEST(CssParserTest, PaddingLeftAdditive) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("margin-left: 10px; padding-left: 8px", cfg);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 18);  // 10 + 8
}

TEST(CssParserTest, PaddingShorthand) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("padding: 10px 20px", cfg);
  ASSERT_TRUE(rule.margin_top.has_value());
  EXPECT_EQ(*rule.margin_top, 10);
  ASSERT_TRUE(rule.margin_left.has_value());
  EXPECT_EQ(*rule.margin_left, 20);
}

TEST(CssParserTest, PaddingTopSetsMarginTop) {
  CssConfig cfg{12, 440, 15};
  auto rule = CssRule::parse("padding-top: 16px", cfg);
  ASSERT_TRUE(rule.margin_top.has_value());
  EXPECT_EQ(*rule.margin_top, 16);
}

// ---------------------------------------------------------------------------
// page-break-after
// ---------------------------------------------------------------------------

TEST(CssParserTest, PageBreakAfterAlways) {
  auto rule = CssRule::parse("page-break-after: always");
  ASSERT_TRUE(rule.page_break_after.has_value());
  EXPECT_TRUE(*rule.page_break_after);
}

TEST(CssParserTest, PageBreakAfterAvoid) {
  auto rule = CssRule::parse("page-break-after: avoid");
  ASSERT_TRUE(rule.page_break_after.has_value());
  EXPECT_FALSE(*rule.page_break_after);
}

// ---------------------------------------------------------------------------
// text-transform
// ---------------------------------------------------------------------------

TEST(CssParserTest, TextTransformUppercase) {
  auto rule = CssRule::parse("text-transform: uppercase");
  ASSERT_TRUE(rule.text_transform.has_value());
  EXPECT_EQ(*rule.text_transform, TextTransform::Uppercase);
}

TEST(CssParserTest, TextTransformLowercase) {
  auto rule = CssRule::parse("text-transform: lowercase");
  ASSERT_TRUE(rule.text_transform.has_value());
  EXPECT_EQ(*rule.text_transform, TextTransform::Lowercase);
}

TEST(CssParserTest, TextTransformCapitalize) {
  auto rule = CssRule::parse("text-transform: capitalize");
  ASSERT_TRUE(rule.text_transform.has_value());
  EXPECT_EQ(*rule.text_transform, TextTransform::Capitalize);
}

TEST(CssParserTest, TextTransformNone) {
  auto rule = CssRule::parse("text-transform: none");
  ASSERT_TRUE(rule.text_transform.has_value());
  EXPECT_EQ(*rule.text_transform, TextTransform::None);
}

// ---------------------------------------------------------------------------
// font-variant: small-caps
// ---------------------------------------------------------------------------

TEST(CssParserTest, FontVariantSmallCapsAsUppercase) {
  auto rule = CssRule::parse("font-variant: small-caps");
  ASSERT_TRUE(rule.text_transform.has_value());
  EXPECT_EQ(*rule.text_transform, TextTransform::Uppercase);
}

TEST(CssParserTest, FontVariantSmallCapsDoesNotOverrideExplicitTransform) {
  auto rule = CssRule::parse("text-transform: capitalize; font-variant: small-caps");
  ASSERT_TRUE(rule.text_transform.has_value());
  EXPECT_EQ(*rule.text_transform, TextTransform::Capitalize);
}

// ---------------------------------------------------------------------------
// vertical-align: super/sub
// ---------------------------------------------------------------------------

TEST(CssParserTest, VerticalAlignSuperSmall) {
  auto rule = CssRule::parse("vertical-align: super");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, VerticalAlignSubSmall) {
  auto rule = CssRule::parse("vertical-align: sub");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Small);
}

TEST(CssParserTest, VerticalAlignDoesNotOverrideFontSize) {
  auto rule = CssRule::parse("font-size: large; vertical-align: super");
  ASSERT_TRUE(rule.font_size.has_value());
  EXPECT_EQ(*rule.font_size, FontSize::Large);
}

// ---------------------------------------------------------------------------
// line-height
// ---------------------------------------------------------------------------

TEST(CssParserTest, LineHeightNormal) {
  auto rule = CssRule::parse("line-height: normal");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightInherit) {
  auto rule = CssRule::parse("line-height: inherit");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightPercent120IsDefault) {
  // 120% is browser default, maps to our 100%
  auto rule = CssRule::parse("line-height: 120%");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightPercent150) {
  // 150% / 120% * 100 = 125
  auto rule = CssRule::parse("line-height: 150%");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 125);
}

TEST(CssParserTest, LineHeightPercent100) {
  // 100% / 120% * 100 = ~83
  auto rule = CssRule::parse("line-height: 100%");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 83);
}

TEST(CssParserTest, LineHeightUnitless1_2IsDefault) {
  // 1.2 is browser default, maps to our 100%
  auto rule = CssRule::parse("line-height: 1.2");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightUnitless1_5) {
  // 1.5 / 1.2 * 100 = 124 (float truncation)
  auto rule = CssRule::parse("line-height: 1.5");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 124);
}

TEST(CssParserTest, LineHeightEm1_5) {
  // 1.5em / 1.2 * 100 = 124 (float truncation)
  auto rule = CssRule::parse("line-height: 1.5em");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 124);
}

TEST(CssParserTest, LineHeightClampLow) {
  // Very small value should clamp to 70
  auto rule = CssRule::parse("line-height: 0.5");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 70);
}

TEST(CssParserTest, LineHeightClampHigh) {
  // Very large value should clamp to 200
  auto rule = CssRule::parse("line-height: 5.0");
  ASSERT_TRUE(rule.line_height_pct.has_value());
  EXPECT_EQ(*rule.line_height_pct, 200);
}

TEST(CssParserTest, LineHeightMerge) {
  auto lhs = CssRule::parse("line-height: 1.5");
  auto rhs = CssRule::parse("line-height: 1.2");
  auto merged = lhs + rhs;
  ASSERT_TRUE(merged.line_height_pct.has_value());
  // rhs wins in merge (operator+ prefers rhs)
  EXPECT_EQ(*merged.line_height_pct, 100);
}

TEST(CssParserTest, LineHeightNoValue) {
  auto rule = CssRule::parse("font-size: large");
  EXPECT_FALSE(rule.line_height_pct.has_value());
}
