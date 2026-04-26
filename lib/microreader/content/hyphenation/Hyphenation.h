#pragma once

#include <optional>
#include <string>

namespace microreader {

enum class HyphenationLang { None, English, German };

// Hyphenate a word. Returns the number of valid hyphen positions written to
// out_positions (byte offsets into word). Positions are byte offsets k such
// that a hyphen may be inserted between word[k-1] and word[k].
// leftmin/rightmin are enforced by the Liang algorithm.
int hyphenate_word(const char* word, size_t len, HyphenationLang lang, size_t* out_positions, int max_positions);

// Detect hyphenation language from an IETF language tag (e.g. "de", "de-DE",
// "en", "en-US"). Case-insensitive prefix match. Returns None if unrecognised.
HyphenationLang detect_language(const std::optional<std::string>& lang_tag);

}  // namespace microreader
