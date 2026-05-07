#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

#include "display.h"
#include "input.h"
#include "microreader/Application.h"
#include "microreader/FontManager.h"
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

    // Mount sd/ as the virtual SD card books directory.
    static std::string books_path = std::filesystem::absolute("sd").string();
    std::filesystem::create_directories(books_path);
    app.set_books_dir(books_path.c_str());

    // Data directory for converted books, settings, reading state.
    static std::string data_path = std::filesystem::absolute("sd/.microreader").string();
    std::filesystem::create_directories(data_path + "/cache");
    std::filesystem::create_directories(data_path + "/data");
    app.set_data_dir(data_path.c_str());

    // Load proportional fonts if available.
    microreader::FontManager font_mgr;
    std::vector<uint8_t> font_data[microreader::kMaxFontSizes];

    static std::string fonts_dir = std::filesystem::absolute("resources/fonts").string();
    for (int i = 0; i < microreader::kMaxFontSizes; i++) {
      std::string path = fonts_dir + "/font-" + std::to_string(i) + ".mbf";
      font_data[i] = load_file(path.c_str());
      if (!font_data[i].empty()) {
        font_mgr.load_font(font_data[i].data(), font_data[i].size());
        if (font_mgr.valid())
          printf("[font] Size index %d: %s (%zu bytes)\n", i, path.c_str(), font_data[i].size());
        else
          printf("[font] Invalid font file: %s\n", path.c_str());
      }
    }

    app.set_reader_font(font_mgr.font_set());
    app.set_font_manager(&font_mgr);
    if (!font_mgr.valid())
      printf("[font] No valid Normal font — using builtin 8x8\n");

    app.start(buf, runtime);
    microreader::run_loop(app, buf, input, runtime);
  } catch (const std::exception& e) {
    std::cerr << "Fatal: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
