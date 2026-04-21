#pragma once
// FontLoader.h — shared helpers for loading .mbf font files in tests.
// Used by BitmapFontTest (load individual files) and HtmlExportTest
// (load all 5 sizes into a BitmapFontSet).

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestPaths.h"
#include "microreader/content/BitmapFont.h"

namespace fs = std::filesystem;

// Load one .mbf file from resources/fonts/font-<suffix>.mbf.
// Returns empty vector if the file is missing or unreadable.
static inline std::vector<uint8_t> load_mbf(const std::string& suffix) {
  const fs::path path = fs::path(repo_root()) / "resources" / "fonts" / ("font-" + suffix + ".mbf");
  std::ifstream f(path, std::ios::binary);
  if (!f.good())
    return {};
  f.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(f.tellg());
  if (size == 0)
    return {};
  f.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(size);
  f.read(reinterpret_cast<char*>(data.data()), size);
  return f.good() ? data : std::vector<uint8_t>{};
}

// Load all 5 font sizes into the provided BitmapFontSet.
// `prop_fonts` and `font_data` must be pre-sized to kFontSizeCount;
// caller owns those vectors and must keep them alive as long as font_set is used.
// Returns true if every size was loaded and initialised successfully.
static inline bool load_desktop_fonts(microreader::BitmapFontSet& font_set,
                                      std::vector<microreader::BitmapFont>& prop_fonts,
                                      std::vector<std::vector<uint8_t>>& font_data) {
  static constexpr struct {
    microreader::FontSize size;
    const char* suffix;
  } kSizes[] = {
      {microreader::FontSize::Small,   "small"  },
      {microreader::FontSize::Normal,  "normal" },
      {microreader::FontSize::Large,   "large"  },
      {microreader::FontSize::XLarge,  "xlarge" },
      {microreader::FontSize::XXLarge, "xxlarge"},
  };

  font_data.clear();
  font_data.resize(microreader::kFontSizeCount);
  prop_fonts.clear();
  prop_fonts.resize(microreader::kFontSizeCount);

  for (size_t i = 0; i < microreader::kFontSizeCount; ++i) {
    font_data[i] = load_mbf(kSizes[i].suffix);
    if (font_data[i].empty())
      return false;
    prop_fonts[i].init(font_data[i].data(), font_data[i].size());
    if (!prop_fonts[i].valid())
      return false;
    font_set.set(kSizes[i].size, &prop_fonts[i]);
  }

  return font_set.valid();
}
