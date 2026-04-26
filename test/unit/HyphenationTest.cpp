#include <gtest/gtest.h>

#include "microreader/content/Font.h"
#include "microreader/content/hyphenation/Hyphenation.h"

using namespace microreader;

// FixedFont: every glyph is `glyph_width` pixels wide (styles/sizes ignored).
// word_width(ptr, n) = number of UTF-8 codepoints * glyph_width.
// char_width('-') = glyph_width.
// Using glyph_width=8 so 1 char = 8px, easy mental arithmetic.
static FixedFont font8(8, 16);

// ---------------------------------------------------------------------------
// Basic: no split possible
// ---------------------------------------------------------------------------

TEST(FindHyphenBreak, ShortWordNoSplit) {
  // len < 6 → Liang skipped, no '-' → returns 0
  bool has_hyphen = true;
  size_t r =
      find_hyphen_break(font8, "Hi", 2, FontStyle::Regular, FontSize::Normal, HyphenationLang::German, 100, has_hyphen);
  EXPECT_EQ(r, 0u);
  EXPECT_FALSE(has_hyphen);
}

TEST(FindHyphenBreak, AvilZeroReturnsZero) {
  bool has_hyphen = true;
  size_t r = find_hyphen_break(font8, "Abendessen", 10, FontStyle::Regular, FontSize::Normal, HyphenationLang::German,
                               0, has_hyphen);
  EXPECT_EQ(r, 0u);
}

TEST(FindHyphenBreak, NoLangNoSplit) {
  // HyphenationLang::None disables Liang; no existing '-' → 0
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, "Abendessen", 10, FontStyle::Regular, FontSize::Normal, HyphenationLang::None,
                               200, has_hyphen);
  EXPECT_EQ(r, 0u);
}

// ---------------------------------------------------------------------------
// Liang: split a word that fits with a synthetic hyphen
// ---------------------------------------------------------------------------

TEST(FindHyphenBreak, LiangGermanSplit) {
  // "Abendessen" (10 chars = 80px). Liang DE breaks at "Abend|essen" (pos 5).
  // prefix "Abend" = 5 * 8 = 40px. Hyphen = 8px. Total = 48px.
  // Give avail = 50px → prefix + hyphen (48) fits.
  bool has_hyphen = true;  // should be set to false (no existing '-')
  size_t r = find_hyphen_break(font8, "Abendessen", 10, FontStyle::Regular, FontSize::Normal, HyphenationLang::German,
                               50, has_hyphen);
  EXPECT_GT(r, 0u);
  EXPECT_LE(r, 10u);
  EXPECT_FALSE(has_hyphen);  // synthetic hyphen needed
}

TEST(FindHyphenBreak, LiangSplitPrefixTooWide) {
  // "Abendessen" = 80px. avail = 10px: no prefix + hyphen can fit → 0
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, "Abendessen", 10, FontStyle::Regular, FontSize::Normal, HyphenationLang::German,
                               10, has_hyphen);
  EXPECT_EQ(r, 0u);
}

// ---------------------------------------------------------------------------
// Existing '-': prefer over Liang, no synthetic hyphen added
// ---------------------------------------------------------------------------

TEST(FindHyphenBreak, PrefersExistingHyphen) {
  // "sehr-lang" (9 chars). '-' is at index 4, so prefix "sehr-" is 5 chars = 40px.
  // avail = 40px: fits without any extra synthetic hyphen.
  const char* word = "sehr-lang";
  size_t len = 9;
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::German, 40,
                               has_hyphen);
  EXPECT_EQ(r, 5u);         // break after "sehr-"
  EXPECT_TRUE(has_hyphen);  // prefix ends with '-', no synthetic '-' needed
}

TEST(FindHyphenBreak, ExistingHyphenWhenLiangWouldAlsoWork) {
  // Word with both an existing '-' and Liang positions.
  // "Abend-essen": '-' at index 5 → prefix "Abend-" = 6 * 8 = 48px.
  // Liang would give "Abend" at pos 5 too (but prefix is "Abend-" here).
  // Give avail = 50px: existing '-' break at pos 6 (prefix "Abend-", 48px) fits.
  const char* word = "Abend-essen";
  size_t len = 11;
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::German, 50,
                               has_hyphen);
  EXPECT_EQ(r, 6u);
  EXPECT_TRUE(has_hyphen);
}

TEST(FindHyphenBreak, ExistingHyphenTooWide_FallsBackToLiang) {
  // "very-longword" (13 chars). '-' at index 4, prefix "very-" = 40px.
  // avail = 30px → prefix "very-" (40px) does NOT fit.
  // Liang EN may not break "longword" usefully in 30px either → result 0 or some small pos.
  // Main assertion: does NOT use the '-' split (40px > 30px).
  const char* word = "very-longword";
  size_t len = 13;
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::English, 30,
                               has_hyphen);
  // If a split was found, it must not be at the '-' position (5), since that's 40px > 30px.
  if (r > 0) {
    EXPECT_NE(r, 5u) << "Should not break at existing '-' when prefix doesn't fit";
  }
}

TEST(FindHyphenBreak, CompoundWordRightmostHyphen) {
  // "a-b-c-longword": hyphens at 1, 3, 5. avail = 50px.
  // Rightmost fitting '-' suffix: "a-b-c-" = 6 * 8 = 48px ≤ 50px → break at pos 6.
  const char* word = "a-b-c-longword";
  size_t len = 14;
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::English, 50,
                               has_hyphen);
  EXPECT_EQ(r, 6u);
  EXPECT_TRUE(has_hyphen);
}

// ---------------------------------------------------------------------------
// The full regression compound word from the EPUB fixture (ch_basic_text):
//   "very-long-hyphenated-compound-word-that-should-break-somehow"
//
// Hyphen positions (glyph_width=8, so 1 char = 8px):
//   i= 5  -> prefix "very-"                                    40px
//   i=10  -> prefix "very-long-"                               80px
//   i=21  -> prefix "very-long-hyphenated-"                   168px
//   i=30  -> prefix "very-long-hyphenated-compound-"          240px
//   i=35  -> prefix "very-long-hyphenated-compound-word-"     280px
//   i=40  -> prefix "very-long-hyphenated-compound-word-that-" 320px
//   i=47  -> prefix "...that-should-"                         376px
//   i=53  -> prefix "...should-break-"                        424px
// ---------------------------------------------------------------------------

// Rightmost split that fits in 300px is i=35 (280px <= 300px; i=40 is 320px > 300px).
TEST(FindHyphenBreak, CompoundWordFull_300px) {
  const char* word = "very-long-hyphenated-compound-word-that-should-break-somehow";
  size_t len = 60;
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::English, 300,
                               has_hyphen);
  EXPECT_EQ(r, 35u);        // "very-long-hyphenated-compound-word-"
  EXPECT_TRUE(has_hyphen);  // prefix ends with '-', no synthetic hyphen
  EXPECT_EQ(word[r - 1], '-');
}

// With only 80px available, only "very-long-" (80px) fits.
TEST(FindHyphenBreak, CompoundWordFull_80px) {
  const char* word = "very-long-hyphenated-compound-word-that-should-break-somehow";
  size_t len = 60;
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::English, 80,
                               has_hyphen);
  EXPECT_EQ(r, 10u);  // "very-long-"
  EXPECT_TRUE(has_hyphen);
}

// With only 39px available, even the smallest prefix "very-" (40px) does not fit -> 0.
TEST(FindHyphenBreak, CompoundWordFull_TooNarrow) {
  const char* word = "very-long-hyphenated-compound-word-that-should-break-somehow";
  size_t len = 60;
  bool has_hyphen = true;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::English, 39,
                               has_hyphen);
  EXPECT_EQ(r, 0u);
  EXPECT_FALSE(has_hyphen);
}

// No double-hyphen: the returned prefix already ends with '-', so callers must
// NOT append an extra '-'. Verify word[r-1] == '-' for any split found.
TEST(FindHyphenBreak, CompoundWordNeverDoubleHyphen) {
  const char* word = "very-long-hyphenated-compound-word-that-should-break-somehow";
  size_t len = 60;
  for (uint16_t avail = 10; avail <= 480; avail += 10) {
    bool has_hyphen = false;
    size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::English,
                                 avail, has_hyphen);
    if (r > 0) {
      EXPECT_EQ(word[r - 1], '-') << "prefix should end with '-' for avail=" << avail;
      EXPECT_TRUE(has_hyphen) << "has_hyphen should be true for avail=" << avail;
    }
  }
}

// Trailing punctuation must not produce a split where the suffix is only
// punctuation characters. E.g. "befördert." must not split as "befördert-|."
TEST(FindHyphenBreak, TrailingPunctuationNotSuffix) {
  // "befördert." in UTF-8: 'ö' = 2 bytes, total 11 bytes
  const char* word = "bef\xc3\xb6rdert.";
  size_t len = std::strlen(word);  // 11
  // avail just too small for the whole word, forcing a split candidate search
  // glyph_width=8, hyphen=8: "befördert-" = 10 codepoints + '-' = 11 * 8 = 88px
  // avail=80 means the whole word doesn't fit but most of it does
  bool has_hyphen = false;
  size_t r = find_hyphen_break(font8, word, len, FontStyle::Regular, FontSize::Normal, HyphenationLang::German, 80,
                               has_hyphen);
  // Whatever split point is chosen, the suffix (word+r) must NOT be purely punctuation
  if (r > 0 && r < len) {
    const char* suffix = word + r;
    bool suffix_is_only_punct = true;
    for (size_t i = 0; i < len - r; ++i) {
      unsigned char c = (unsigned char)suffix[i];
      if (c >= 0x80 || std::isalpha(c)) {
        suffix_is_only_punct = false;
        break;
      }
    }
    EXPECT_FALSE(suffix_is_only_punct) << "Split at pos=" << r << " leaves suffix=\"" << suffix
                                       << "\" which is only punctuation";
  }
}
