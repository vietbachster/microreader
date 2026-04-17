#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "microreader/content/BitmapFont.h"
#include "microreader/display/DrawBuffer.h"
#include "microreader/screens/ReaderScreen.h"

using namespace microreader;
namespace fs = std::filesystem;

class HeadlessDisplay final : public IDisplay {
 public:
  void full_refresh(const uint8_t* /*pixels*/, RefreshMode /*mode*/) override {}
  void partial_refresh(const uint8_t* /*new_pixels*/, const uint8_t* /*prev_pixels*/) override {}
  void write_ram_bw(const uint8_t* /*data*/) override {}
  void write_ram_red(const uint8_t* /*data*/) override {}
  void grayscale_refresh() override {}
  void revert_grayscale(const uint8_t* /*prev_pixels*/) override {}
  void set_rotation(Rotation /*r*/) override {}
};

static fs::path find_fonts_dir() {
  fs::path current = fs::current_path();
  for (int i = 0; i < 6; ++i) {
    fs::path candidate = current / "resources" / "fonts";
    if (fs::exists(candidate) && fs::is_directory(candidate))
      return candidate;
    if (current == current.root_path())
      break;
    current = current.parent_path();
  }
  return {};
}

static bool load_export_font_set(BitmapFontSet& font_set, std::vector<std::vector<uint8_t>>& font_data) {
  const fs::path fonts_dir = find_fonts_dir();
  if (fonts_dir.empty())
    return false;

  struct FontInfo {
    FontSize size;
    const char* suffix;
  };
  static constexpr FontInfo kSizes[] = {
      {FontSize::Small,   "small"  },
      {FontSize::Normal,  "normal" },
      {FontSize::Large,   "large"  },
      {FontSize::XLarge,  "xlarge" },
      {FontSize::XXLarge, "xxlarge"},
  };

  font_data.clear();
  font_data.resize(kFontSizeCount);
  std::vector<BitmapFont> prop_fonts(kFontSizeCount);

  for (int i = 0; i < kFontSizeCount; ++i) {
    const auto& info = kSizes[i];
    fs::path path = fonts_dir / (std::string("font-") + info.suffix + ".mbf");
    if (!fs::exists(path))
      return false;
    std::ifstream file(path, std::ios::binary);
    if (!file.good())
      return false;
    file.seekg(0, std::ios::end);
    auto size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    font_data[i].resize(size);
    file.read(reinterpret_cast<char*>(font_data[i].data()), size);
    if (!file)
      return false;
    prop_fonts[i].init(font_data[i].data(), font_data[i].size());
    if (!prop_fonts[i].valid())
      return false;
    font_set.set(info.size, &prop_fonts[i]);
  }

  return font_set.valid();
}

static std::string sanitize_html(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

static void write_1bit_bmp(const std::string& path, const DecodedImage& img) {
  if (img.data.empty() || img.width == 0 || img.height == 0)
    return;

  const int W = img.width;
  const int H = img.height;
  const int row_stride = ((W + 31) / 32) * 4;
  const int pixel_bytes = row_stride * H;
  const int file_size = 14 + 40 + 8 + pixel_bytes;

  std::vector<uint8_t> buf(static_cast<size_t>(file_size), 0);
  uint8_t* p = buf.data();

  auto w16 = [](uint8_t* d, uint16_t v) {
    d[0] = static_cast<uint8_t>(v & 0xFF);
    d[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  };
  auto w32 = [](uint8_t* d, uint32_t v) {
    d[0] = static_cast<uint8_t>(v & 0xFF);
    d[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    d[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    d[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
  };
  auto i32 = [](uint8_t* d, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    d[0] = static_cast<uint8_t>(u & 0xFF);
    d[1] = static_cast<uint8_t>((u >> 8) & 0xFF);
    d[2] = static_cast<uint8_t>((u >> 16) & 0xFF);
    d[3] = static_cast<uint8_t>((u >> 24) & 0xFF);
  };

  p[0] = 'B';
  p[1] = 'M';
  w32(p + 2, static_cast<uint32_t>(file_size));
  w32(p + 10, 14 + 40 + 8);
  p += 14;

  w32(p, 40);
  i32(p + 4, W);
  i32(p + 8, -H);
  w16(p + 12, 1);
  w16(p + 14, 1);
  w32(p + 20, static_cast<uint32_t>(pixel_bytes));
  p += 40;

  p[3] = 0;
  p[4] = 255;
  p[5] = 255;
  p[6] = 255;
  p[7] = 0;
  p += 8;

  const size_t src_stride = img.stride();
  for (int y = 0; y < H; ++y)
    std::memcpy(p + static_cast<size_t>(y) * row_stride, img.data.data() + static_cast<size_t>(y) * src_stride,
                src_stride);

  std::ofstream f(path, std::ios::binary);
  if (!f.good())
    return;
  f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
}

static DecodedImage buffer_to_image(const uint8_t* physical_buf) {
  DecodedImage image;
  image.width = DrawBuffer::kWidth;
  image.height = DrawBuffer::kHeight;
  image.data.assign(image.data_size(), 0xFF);

  const size_t phys_stride = DisplayFrame::kStride;
  for (int y = 0; y < static_cast<int>(image.height); ++y) {
    for (int x = 0; x < static_cast<int>(image.width); ++x) {
      const int phys_x = y;
      const int phys_y = DisplayFrame::kPhysicalHeight - 1 - x;
      const size_t phys_idx = static_cast<size_t>(phys_y) * phys_stride + static_cast<size_t>(phys_x) / 8;
      const uint8_t phys_bit = static_cast<uint8_t>(0x80u >> (phys_x & 7));
      const bool white = (physical_buf[phys_idx] & phys_bit) != 0;
      if (!white) {
        const size_t out_idx = static_cast<size_t>(y) * image.stride() + static_cast<size_t>(x) / 8;
        const uint8_t out_bit = static_cast<uint8_t>(0x80u >> (x & 7));
        image.data[out_idx] &= static_cast<uint8_t>(~out_bit);
      }
    }
  }
  return image;
}

static std::string to_web_path(const fs::path& path) {
  std::string result = path.generic_string();
  std::replace(result.begin(), result.end(), '\\', '/');
  return result;
}

static bool write_html_index(const fs::path& html_path, const std::string& title,
                             const std::vector<std::string>& pages) {
  std::ofstream out(html_path);
  if (!out.good())
    return false;

  out << "<!DOCTYPE html>\n";
  out << "<html lang=\"en\">\n";
  out << "<head>\n";
  out << "  <meta charset=\"UTF-8\">\n";
  out << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  out << "  <title>" << sanitize_html(title) << "</title>\n";
  out << "  <style>body{margin:0;padding:16px;font-family:Arial,sans-serif;background:#f7f7f7;color:#111;}"
         "h1{font-size:1.6rem;margin-bottom:0.5rem;}"
         ".page{margin-bottom:32px;box-shadow:0 0 12px "
         "rgba(0,0,0,0.08);background:#fff;padding:16px;border-radius:8px;}"
         "img{width:100%;height:auto;display:block;border:1px solid #ddd;background:#fff;}"
         "footer{margin-top:48px;font-size:0.9rem;color:#666;}</style>\n";
  out << "</head>\n";
  out << "<body>\n";
  out << "  <h1>" << sanitize_html(title) << "</h1>\n";
  out << "  <p>Exported " << pages.size() << " pages.</p>\n";

  for (size_t i = 0; i < pages.size(); ++i) {
    out << "  <div class=\"page\">\n";
    out << "    <h2>Page " << (i + 1) << "</h2>\n";
    out << "    <img src=\"" << sanitize_html(pages[i]) << "\" alt=\"Page " << (i + 1) << "\">\n";
    out << "  </div>\n";
  }

  out << "  <footer>Generated by microreader desktop HTML exporter.</footer>\n";
  out << "</body>\n";
  out << "</html>\n";
  return true;
}

static std::string sanitize_filename(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
      out += c;
    } else if (c == ' ') {
      out += '_';
    }
  }
  return out.empty() ? "book" : out;
}

static bool is_epub_file(const fs::path& path) {
  if (!fs::is_regular_file(path))
    return false;
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext == ".epub";
}

static std::vector<fs::path> find_epubs_in_directory(const fs::path& dir) {
  std::vector<fs::path> paths;
  if (!fs::is_directory(dir))
    return paths;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.is_regular_file() && is_epub_file(entry.path()))
      paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

bool export_book_to_html(const fs::path& epub_path, const fs::path& output_dir, BitmapFontSet* font_set) {
  std::error_code ec;
  if (!fs::create_directories(output_dir, ec) && ec) {
    std::fprintf(stderr, "ERROR: Failed to create output directory: %s (%s)\n", output_dir.string().c_str(),
                 ec.message().c_str());
    return false;
  }

  const fs::path data_dir = output_dir / ".microreader";
  if (!fs::create_directories(data_dir, ec) && ec) {
    std::fprintf(stderr, "ERROR: Failed to create data directory: %s (%s)\n", data_dir.string().c_str(),
                 ec.message().c_str());
    return false;
  }

  HeadlessDisplay display;
  DrawBuffer buf(display);

  ReaderScreen reader;
  reader.set_path(epub_path.string());
  reader.set_data_dir(data_dir.string());
  if (font_set && font_set->valid())
    reader.set_fonts(font_set);

  reader.start(buf);
  if (!reader.is_open_ok()) {
    std::fprintf(stderr, "ERROR: Failed to open EPUB: %s\n", epub_path.string().c_str());
    return false;
  }

  std::vector<std::string> pages;
  size_t page_index = 0;

  auto save_page = [&](size_t index) {
    DecodedImage image = buffer_to_image(buf.render_buf());
    std::ostringstream filename;
    filename << "page_" << std::setfill('0') << std::setw(4) << index << ".bmp";
    const fs::path image_path = output_dir / filename.str();
    write_1bit_bmp(image_path.string(), image);
    pages.push_back(to_web_path(image_path.filename()));
  };

  if (!reader.render_current_page(buf)) {
    std::fprintf(stderr, "ERROR: Failed to render first page: %s\n", epub_path.string().c_str());
    return false;
  }
  save_page(page_index);

  while (reader.next_page_and_render(buf)) {
    ++page_index;
    save_page(page_index);
  }

  const std::string title = sanitize_filename(epub_path.stem().string());
  const fs::path html_path = output_dir / "index.html";
  if (!write_html_index(html_path, title, pages)) {
    std::fprintf(stderr, "ERROR: Failed to write HTML index: %s\n", html_path.string().c_str());
    return false;
  }

  std::printf("[export] %s -> %s (%zu pages)\n", epub_path.filename().string().c_str(), html_path.string().c_str(),
              pages.size());
  return true;
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::fprintf(stderr, "Usage: %s <input.epub|input-folder> <output-directory>\n", argv[0]);
    return 1;
  }

  const fs::path input_path = argv[1];
  const fs::path output_dir = argv[2];

  if (!fs::exists(input_path)) {
    std::fprintf(stderr, "ERROR: Input path not found: %s\n", input_path.string().c_str());
    return 1;
  }

  std::vector<fs::path> epub_paths;
  if (fs::is_directory(input_path)) {
    epub_paths = find_epubs_in_directory(input_path);
    if (epub_paths.empty()) {
      std::fprintf(stderr, "ERROR: No EPUB files found in directory: %s\n", input_path.string().c_str());
      return 1;
    }
  } else if (is_epub_file(input_path)) {
    epub_paths.push_back(input_path);
  } else {
    std::fprintf(stderr, "ERROR: Input path is not an EPUB or directory: %s\n", input_path.string().c_str());
    return 1;
  }

  std::error_code ec;
  if (!fs::create_directories(output_dir, ec) && ec) {
    std::fprintf(stderr, "ERROR: Failed to create output directory: %s (%s)\n", output_dir.string().c_str(),
                 ec.message().c_str());
    return 1;
  }

  BitmapFontSet font_set;
  std::vector<std::vector<uint8_t>> font_data;
  if (load_export_font_set(font_set, font_data)) {
    std::printf("[export] Loaded proportional fonts from resources/fonts\n");
  } else {
    std::printf("[export] Warning: proportional fonts not found, using builtin bitmap font\n");
  }

  bool all_ok = true;
  for (const fs::path& epub_path : epub_paths) {
    const std::string name = sanitize_filename(epub_path.stem().string());
    const fs::path book_output = output_dir / name;
    if (!export_book_to_html(epub_path, book_output, font_set.valid() ? &font_set : nullptr))
      all_ok = false;
  }

  return all_ok ? 0 : 1;
}
