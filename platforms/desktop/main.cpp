#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

#include "display.h"
#include "input.h"
#include "microreader/Application.h"
#include "microreader/Loop.h"
#include "microreader/content/BitmapFont.h"
#include "microreader/display/DrawBuffer.h"
#include "runtime.h"

// Load a file into a byte vector. Returns empty on failure.
static std::vector<uint8_t> load_file(const char* path) {
  std::vector<uint8_t> data;
  FILE* f = fopen(path, "rb");
  if (!f)
    return data;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz <= 0) {
    fclose(f);
    return data;
  }
  fseek(f, 0, SEEK_SET);
  data.resize(static_cast<size_t>(sz));
  fread(data.data(), 1, data.size(), f);
  fclose(f);
  return data;
}

int main() {
  try {
    DesktopRuntime runtime(16);
    DesktopInputSource input(runtime);
    DesktopEmulatorDisplay display(runtime);
    microreader::Application app;
    microreader::DrawBuffer buf(display);

    // Mount sd/books as the virtual SD card books directory.
    static std::string books_path = std::filesystem::absolute("sd/books").string();
    std::filesystem::create_directories(books_path);
    app.set_books_dir(books_path.c_str());

    // Data directory for converted books, settings, reading state.
    static std::string data_path = std::filesystem::absolute("sd/.microreader").string();
    std::filesystem::create_directories(data_path + "/cache");
    std::filesystem::create_directories(data_path + "/data");
    app.set_data_dir(data_path.c_str());

    // Load proportional fonts (5 sizes) if available.
    microreader::BitmapFont prop_fonts[microreader::kFontSizeCount];
    microreader::BitmapFontSet font_set;
    std::vector<uint8_t> font_data[microreader::kFontSizeCount];

    struct SizeInfo {
      int idx;             // index into prop_fonts/font_data
      const char* label;   // for logging
      const char* suffix;  // filename suffix
    };
    static constexpr SizeInfo kSizes[] = {
        {0, "Small",   "small"  },
        {1, "Normal",  "normal" },
        {2, "Large",   "large"  },
        {3, "XLarge",  "xlarge" },
        {4, "XXLarge", "xxlarge"},
    };

    static std::string fonts_dir = std::filesystem::absolute("resources/fonts").string();
    for (const auto& si : kSizes) {
      std::string path = fonts_dir + "/font-" + si.suffix + ".mbf";
      font_data[si.idx] = load_file(path.c_str());
      if (!font_data[si.idx].empty()) {
        prop_fonts[si.idx].init(font_data[si.idx].data(), font_data[si.idx].size());
        if (prop_fonts[si.idx].valid()) {
          printf("[font] %s: %s (%zu bytes, %u glyphs)\n", si.label, path.c_str(), font_data[si.idx].size(),
                 prop_fonts[si.idx].num_glyphs());
        } else {
          printf("[font] Invalid font file: %s\n", path.c_str());
        }
      }
    }

    static constexpr microreader::FontSize kFontSizes[] = {microreader::FontSize::Small, microreader::FontSize::Normal,
                                                           microreader::FontSize::Large, microreader::FontSize::XLarge,
                                                           microreader::FontSize::XXLarge};
    for (int i = 0; i < microreader::kFontSizeCount; i++)
      font_set.set(kFontSizes[i], &prop_fonts[i]);

    if (font_set.valid()) {
      app.set_reader_font(&font_set);
    } else {
      printf("[font] No valid Normal font — using builtin 8x8\n");
    }

    app.start(buf);
    microreader::run_loop(app, buf, input, runtime);
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
