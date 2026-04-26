#include "Hyphenation.h"

#include <cstring>

#include "Liang/hyph-de.h"
#include "Liang/hyph-en-us.h"
#include "Liang/liang_hyphenation_patterns.h"

// ---------------------------------------------------------------------------
// Liang hyphenation algorithm (stack-only, no heap allocation)
// ---------------------------------------------------------------------------

#ifndef MAX_WORD_LEN
#define MAX_WORD_LEN 128
#endif

static int compare_pattern_segment(const std::uint8_t* pat, int patlen, const char* seg, int seglen) {
  int len = patlen < seglen ? patlen : seglen;
  for (int i = 0; i < len; ++i) {
    unsigned char a = (unsigned char)pat[i];
    unsigned char b = (unsigned char)seg[i];
    if (a != b)
      return (int)a - (int)b;
  }
  if (patlen < seglen)
    return -1;
  if (patlen > seglen)
    return 1;
  return 0;
}

static int find_pattern_index(const char* seg, int seglen, const HyphenationPatterns& pats) {
  if (pats.count == 0)
    return -1;
  int lo = 0, hi = (int)pats.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    int cmp = compare_pattern_segment(pats.patterns[mid].letters, pats.patterns[mid].letters_len, seg, seglen);
    if (cmp == 0)
      return mid;
    if (cmp < 0)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return -1;
}

static int liang_hyphenate(const char* word, size_t leftmin, size_t rightmin, char boundary_char, size_t* out_positions,
                           int max_positions, const HyphenationPatterns& pats) {
  if (!word)
    return 0;
  int word_len = (int)std::strlen(word);
  if (word_len <= 0)
    return 0;
  if (word_len > MAX_WORD_LEN)
    word_len = MAX_WORD_LEN;

  int M = word_len + 2;
  char ext[MAX_WORD_LEN + 3];
  ext[0] = boundary_char;
  std::memcpy(ext + 1, word, word_len);
  ext[1 + word_len] = boundary_char;
  ext[1 + word_len + 1] = '\0';

  uint8_t H[MAX_WORD_LEN + 3];
  std::memset(H, 0, sizeof(H));

  for (int i = 0; i < M; ++i) {
    for (int j = i + 1; j <= M; ++j) {
      int len = j - i;
      int idx = find_pattern_index(ext + i, len, pats);
      if (idx >= 0) {
        const uint8_t* vals = pats.patterns[idx].values;
        int vlen = pats.patterns[idx].values_len;
        for (int l = 0; l < vlen && (i + l) < (int)sizeof(H); ++l)
          if (H[i + l] < vals[l])
            H[i + l] = vals[l];
      }
    }
  }

  int leftmin_i = (leftmin > (size_t)word_len) ? word_len : static_cast<int>(leftmin);
  int rightmin_i = (rightmin > (size_t)word_len) ? word_len : static_cast<int>(rightmin);

  int count = 0;
  for (int k = 1; k < word_len; ++k) {
    int ext_pos = k + 1;
    if ((H[ext_pos] & 1) && k >= leftmin_i && (word_len - k) >= rightmin_i) {
      if (count < max_positions)
        out_positions[count] = static_cast<size_t>(k);
      ++count;
    }
  }
  return count;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace microreader {

int hyphenate_word(const char* word, size_t /*len*/, HyphenationLang lang, size_t* out_positions, int max_positions) {
  if (lang == HyphenationLang::None)
    return 0;
  const HyphenationPatterns& pats = (lang == HyphenationLang::German) ? de_patterns : en_us_patterns;
  return liang_hyphenate(word, 2, 2, '.', out_positions, max_positions, pats);
}

HyphenationLang detect_language(const std::optional<std::string>& lang_tag) {
  if (!lang_tag || lang_tag->size() < 2)
    return HyphenationLang::None;
  const char a = (*lang_tag)[0];
  const char b = (*lang_tag)[1];
  if ((a == 'd' || a == 'D') && (b == 'e' || b == 'E'))
    return HyphenationLang::German;
  if ((a == 'e' || a == 'E') && (b == 'n' || b == 'N'))
    return HyphenationLang::English;
  return HyphenationLang::None;
}

}  // namespace microreader
