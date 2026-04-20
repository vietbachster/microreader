#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "microreader/content/Book.h"
#include "microreader/content/TextLayout.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/display/DrawBuffer.h"
#include "microreader/screens/ReaderScreen.h"
#include "stb_image_write.h"

using namespace microreader;
namespace fs = std::filesystem;

// Resolve the microreader2 repository root from TEST_FIXTURES_DIR.
static std::string repo_root() {
  std::string fixtures = TEST_FIXTURES_DIR;
  auto pos = fixtures.rfind('/');
  if (pos == std::string::npos)
    pos = fixtures.rfind('\\');
  std::string up1 = fixtures.substr(0, pos);  // .../microreader2/test
  pos = up1.rfind('/');
  if (pos == std::string::npos)
    pos = up1.rfind('\\');
  return up1.substr(0, pos);  // .../microreader2
}

static std::string small_books_dir() {
  return repo_root() + "/test/books/small";
}

static std::string sanitize_filename(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.') {
      out += c;
    } else if (c == ' ') {
      out += '_';
    }
  }
  return out.empty() ? "book" : out;
}

static bool load_desktop_fonts(BitmapFontSet& font_set, std::vector<BitmapFont>& prop_fonts,
                               std::vector<std::vector<uint8_t>>& font_data) {
  struct SizeInfo {
    FontSize size;
    const char* suffix;
  };
  static constexpr SizeInfo kSizes[] = {
      {FontSize::Small,   "small"  },
      {FontSize::Normal,  "normal" },
      {FontSize::Large,   "large"  },
      {FontSize::XLarge,  "xlarge" },
      {FontSize::XXLarge, "xxlarge"},
  };

  const fs::path fonts_dir = fs::path(repo_root()) / "resources" / "fonts";
  font_data.clear();
  font_data.resize(kFontSizeCount);
  prop_fonts.clear();
  prop_fonts.resize(kFontSizeCount);

  for (size_t i = 0; i < kFontSizeCount; ++i) {
    const auto& info = kSizes[i];
    fs::path path = fonts_dir / (std::string("font-") + info.suffix + ".mbf");
    if (!fs::exists(path))
      return false;
    std::ifstream file(path, std::ios::binary);
    if (!file.good())
      return false;
    file.seekg(0, std::ios::end);
    auto size = static_cast<size_t>(file.tellg());
    if (size == 0)
      return false;
    file.seekg(0, std::ios::beg);
    font_data[i].resize(size);
    file.read(reinterpret_cast<char*>(font_data[i].data()), size);
    if (!file.good())
      return false;
    prop_fonts[i].init(font_data[i].data(), font_data[i].size());
    if (!prop_fonts[i].valid())
      return false;
    font_set.set(info.size, &prop_fonts[i]);
  }

  return font_set.valid();
}

static bool write_page_png(const fs::path& path, const uint8_t* physical_buf) {
  const int W = DrawBuffer::kWidth;
  const int H = DrawBuffer::kHeight;
  std::vector<uint8_t> rgb(static_cast<size_t>(W * H * 3), 0xFF);
  // Convert 1bpp buffer to 24bpp RGB for PNG
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      const int phys_x = y;
      const int phys_y = DisplayFrame::kPhysicalHeight - 1 - x;
      const size_t phys_index = static_cast<size_t>(phys_y) * DisplayFrame::kStride + static_cast<size_t>(phys_x / 8);
      const uint8_t phys_bit = static_cast<uint8_t>(0x80u >> (phys_x & 7));
      const bool white = (physical_buf[phys_index] & phys_bit) != 0;
      size_t pixel_offset = static_cast<size_t>(y * W + x) * 3;
      rgb[pixel_offset + 0] = white ? 0xFF : 0x00;
      rgb[pixel_offset + 1] = white ? 0xFF : 0x00;
      rgb[pixel_offset + 2] = white ? 0xFF : 0x00;
    }
  }
  return stbi_write_png(path.string().c_str(), W, H, 3, rgb.data(), W * 3) != 0;
}

struct PageInfo {
  std::string filename;
  size_t chapter;
  size_t page_in_chapter;
};

static bool write_html_index(const fs::path& html_path, const std::string& title, const std::vector<PageInfo>& pages) {
  std::ofstream out(html_path);
  if (!out.good())
    return false;

  out << "<!DOCTYPE html>\n";
  out << "<html lang=\"en\">\n";
  out << "<head>\n";
  out << "  <meta charset=\"UTF-8\">\n";
  out << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  out << "  <title>" << title << "</title>\n";
  out << "  <style>\n";
  out << "    body{margin:0;padding:16px;font-family:Arial,sans-serif;background:#f7f7f7;color:#111;}\n";
  out << "    h1{margin:0 0 16px;font-size:1.8rem;}\n";
  out << "    section{margin-bottom:32px;}\n";
  out << "    section h2{margin:0 0 12px;font-size:1.2rem;color:#222;}\n";
  out << "    "
         ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(480px,480px));gap:16px;justify-items:start;"
         "justify-content:center;}"
         "\n";
  out << "    figure{margin:0;width:480px;display:flex;flex-direction:column;align-items:center;}\n";
  out << "    .image-frame{width:480px;height:788px;border-radius:12px;border:1px solid "
         "#e3e3e3;background:#fff;box-shadow:0 8px 24px "
         "rgba(0,0,0,0.06);display:flex;align-items:center;justify-content:center;}\n";
  out << "    img{width:480px;height:788px;object-fit:contain;border-radius:12px 12px 12px 12px;}\n";
  out << "    figcaption{margin-top:4px;color:#333;font-size:0.95rem;text-align:center;}\n";
  out << "    footer{margin-top:24px;font-size:0.95rem;color:#666;}\n";
  out << "  </style>\n";
  out << "</head>\n";
  out << "<body>\n";
  out << "  <h1>" << title << "</h1>\n";

  if (!pages.empty()) {
    size_t current_chapter = pages[0].chapter;
    out << "  <section>\n";
    out << "    <h2>Chapter " << (current_chapter + 1) << "</h2>\n";
    out << "    <div class=\"grid\">\n";

    for (const auto& page : pages) {
      if (page.chapter != current_chapter) {
        out << "    </div>\n";
        out << "  </section>\n";
        current_chapter = page.chapter;
        out << "  <section>\n";
        out << "    <h2>Chapter " << (current_chapter + 1) << "</h2>\n";
        out << "    <div class=\"grid\">\n";
      }
      out << "      <figure>\n";
      out << "        <div class=\"image-frame\">\n";
      out << "          <img src=\"" << page.filename << "\" alt=\"Page " << page.page_in_chapter << "\">\n";
      out << "        </div>\n";
      out << "        <figcaption>Page " << page.page_in_chapter << "</figcaption>\n";
      out << "      </figure>\n";
    }

    out << "    </div>\n";
    out << "  </section>\n";
  }

  out << "  <footer>Generated by microreader export test.</footer>\n";
  out << "</body>\n";
  out << "</html>\n";
  return true;
}

static std::vector<fs::path> discover_epubs(const fs::path& dir) {
  std::vector<fs::path> result;
  if (!fs::exists(dir) || !fs::is_directory(dir))
    return result;
  for (auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() == ".epub")
      result.push_back(entry.path());
  }
  std::sort(result.begin(), result.end());
  return result;
}

static bool export_book_to_html(const fs::path& epub_path, const fs::path& output_root, const BitmapFontSet& font_set) {
  struct TestDisplay : IDisplay {
    void full_refresh(const uint8_t* pixels, RefreshMode mode, bool turnOffScreen) override {
      (void)pixels;
      (void)mode;
      (void)turnOffScreen;
    }
    void partial_refresh(const uint8_t* new_pixels, const uint8_t* prev_pixels) override {
      (void)new_pixels;
      (void)prev_pixels;
    }
  } display;

  DrawBuffer buf(display);
  ReaderScreen screen;
  screen.set_path(epub_path.string());
  screen.set_fonts(&font_set);

  fs::path book_dir = output_root / sanitize_filename(epub_path.stem().string());
  fs::create_directories(book_dir);
  fs::path data_dir = book_dir / "data";
  fs::create_directories(data_dir);
  screen.set_data_dir(data_dir.string());

  screen.start(buf);
  if (!screen.is_open_ok()) {
    std::cerr << "Failed to open book: " << epub_path << std::endl;
    return false;
  }

  std::vector<PageInfo> pages;
  size_t current_chapter = screen.current_chapter_index();
  size_t page_in_chapter = 1;
  size_t page_number = 1;

  auto add_page = [&](const fs::path& filename) {
    pages.push_back(PageInfo{filename.filename().string(), current_chapter, page_in_chapter});
  };

  fs::path page_file = book_dir / "page_1.png";
  if (!write_page_png(page_file, buf.render_buf())) {
    std::cerr << "Failed to write initial page: " << page_file << std::endl;
    return false;
  }
  add_page(page_file);

  while (screen.next_page_and_render(buf)) {
    const size_t chapter = screen.current_chapter_index();
    if (chapter != current_chapter) {
      current_chapter = chapter;
      page_in_chapter = 1;
    } else {
      page_in_chapter++;
    }
    page_number++;
    page_file = book_dir / ("page_" + std::to_string(page_number) + ".png");
    if (!write_page_png(page_file, buf.render_buf())) {
      std::cerr << "Failed to write page: " << page_file << std::endl;
      return false;
    }
    add_page(page_file);
  }

  bool ok = write_html_index(book_dir / "index.html", epub_path.stem().string(), pages);
  if (!ok)
    std::cerr << "Failed to write HTML index for: " << epub_path << std::endl;
  screen.stop();
  return ok;
}

TEST(HtmlExportTest, ExportSmallFolderToHtml) {
  BitmapFontSet font_set;
  std::vector<BitmapFont> prop_fonts(kFontSizeCount);
  std::vector<std::vector<uint8_t>> font_data(kFontSizeCount);
  ASSERT_TRUE(load_desktop_fonts(font_set, prop_fonts, font_data));

  fs::path input_dir = fs::path(small_books_dir());
  ASSERT_TRUE(fs::exists(input_dir));

  fs::path output_root = fs::path(repo_root()) / "test" / "output" / "html_export_folder";
  std::error_code ec;
  fs::create_directories(output_root, ec);
  ASSERT_TRUE(fs::exists(output_root) || !ec) << "output_root=" << output_root << " ec=" << ec.message();

  auto books = discover_epubs(input_dir);
  ASSERT_FALSE(books.empty());

  for (const auto& epub_path : books) {
    ASSERT_TRUE(export_book_to_html(epub_path, output_root, font_set))
        << "Failed to export " << epub_path.filename().string();
  }
}

TEST(HtmlExportTest, ExportAliceIllustrated) {
  BitmapFontSet font_set;
  std::vector<BitmapFont> prop_fonts(kFontSizeCount);
  std::vector<std::vector<uint8_t>> font_data(kFontSizeCount);
  ASSERT_TRUE(load_desktop_fonts(font_set, prop_fonts, font_data));

  fs::path epub_path = fs::path(small_books_dir()) / "alice-illustrated.epub";
  ASSERT_TRUE(fs::exists(epub_path)) << "alice-illustrated.epub not found in test/books/small/";

  fs::path output_root = fs::path(repo_root()) / "test" / "output" / "alice_illustrated";
  std::error_code ec;
  fs::create_directories(output_root, ec);

  ASSERT_TRUE(export_book_to_html(epub_path, output_root, font_set));
  std::cout << "Output: " << (output_root / "alice-illustrated" / "index.html") << std::endl;
}

// ---------------------------------------------------------------------------
// DebugAliceLayout — dumps a human-readable layout trace for every page
// of alice-illustrated.epub.  Output: test/output/alice_debug_layout.txt
//
// Each page shows every item's y_offset, height, para/line index, and a
// text snippet (for text items) or dimensions (for image items), plus the
// gap between the last item and the page bottom.  Use this to spot
// incorrect spacing, overlapping items, or wasted whitespace.
// ---------------------------------------------------------------------------
TEST(HtmlExportTest, DebugAliceLayout) {
  fs::path epub_path = fs::path(small_books_dir()) / "alice-illustrated.epub";
  ASSERT_TRUE(fs::exists(epub_path)) << "alice-illustrated.epub not found in test/books/small/";

  // ── Open book and convert to MRB ────────────────────────────────────────
  Book book;
  ASSERT_EQ(book.open(epub_path.string().c_str()), EpubError::Ok);

  auto mrb_path = (fs::temp_directory_path() / "alice_debug.mrb").string();
  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, mrb_path.c_str())) << "MRB conversion failed";

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(mrb_path.c_str())) << "MRB open failed";

  // ── Layout parameters matching ReaderScreen exactly ─────────────────────
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;
  // Match ReaderScreen private constants exactly:
  static constexpr int kPadTop = 6;
  static constexpr int kPadRight = 12;
  static constexpr int kPadBottom = 12;
  static constexpr int kPadLeft = 12;
  static constexpr int kParaSp = 8;
  FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);

  PageOptions opts(static_cast<uint16_t>(DrawBuffer::kWidth), static_cast<uint16_t>(DrawBuffer::kHeight),
                   static_cast<uint16_t>(kPadTop), static_cast<uint16_t>(kParaSp), Alignment::Start);
  opts.padding_right = static_cast<uint16_t>(kPadRight);
  opts.padding_bottom = static_cast<uint16_t>(kPadBottom);
  opts.padding_left = static_cast<uint16_t>(kPadLeft);
  opts.center_text = true;

  const int page_height = DrawBuffer::kHeight;

  // ── Output file ──────────────────────────────────────────────────────────
  fs::path out_path = fs::path(repo_root()) / "test" / "output" / "alice_debug_layout.txt";
  fs::create_directories(out_path.parent_path());
  std::ofstream out(out_path);
  ASSERT_TRUE(out.good()) << "Cannot open output file: " << out_path;

  auto write_separator = [&](char c = '-', int n = 80) { out << std::string(static_cast<size_t>(n), c) << '\n'; };

  out << "Book: " << epub_path.filename().string() << '\n';
  out << "Total chapters: " << mrb.chapter_count() << '\n';
  out << "Page size: " << DrawBuffer::kWidth << "x" << DrawBuffer::kHeight << '\n';
  out << "Padding: top=" << kPadTop << " right=" << kPadRight << " bottom=" << kPadBottom << " left=" << kPadLeft
      << "  para_spacing=" << kParaSp << '\n';
  write_separator('=');

  int global_page = 0;

  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);
    PagePosition pos{0, 0};
    int page_in_chapter = 0;

    while (true) {
      auto pc = layout_page(font, opts, src, pos);
      ++global_page;
      ++page_in_chapter;

      // ── Page header ──────────────────────────────────────────────────
      out << "=== CH " << ci << " | Page " << page_in_chapter << " (global #" << global_page << ")"
          << " | pos{" << pc.start.paragraph << "," << pc.start.line << "}"
          << " → {" << pc.end.paragraph << "," << pc.end.line << "}"
          << " | voff=" << pc.vertical_offset << " | at_end=" << (pc.at_chapter_end ? 1 : 0) << '\n';

      // ── Collect all items sorted by y for easy reading ────────────────
      struct Item {
        int y;
        int h;  // estimated height (text=line height, image=actual, hr=1)
        std::string label;
      };
      std::vector<Item> items;

      for (const auto& ti : pc.text_items) {
        // Build a short text snippet from the line words.
        std::string snippet;
        for (const auto& w : ti.line.words) {
          if (!snippet.empty())
            snippet += ' ';
          snippet.append(w.text, w.len);
          if (snippet.size() > 60) {
            snippet += "...";
            break;
          }
        }
        int h = ti.height > 0 ? ti.height : font.y_advance();
        char buf[256];
        std::snprintf(buf, sizeof(buf), "  TEXT  y=%4d h=%3d  para=%u:ln=%u  \"%s\"", ti.y_offset + pc.vertical_offset,
                      h, ti.paragraph_index, ti.line_index, snippet.c_str());
        items.push_back({ti.y_offset + pc.vertical_offset, h, buf});
      }

      for (const auto& img : pc.image_items) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "  IMG   y=%4d h=%3d  para=%u  key=%u  %ux%u  @x=%u",
                      img.y_offset + pc.vertical_offset, img.height, img.paragraph_index, img.key, img.width,
                      img.height, img.x_offset);
        items.push_back({img.y_offset + pc.vertical_offset, img.height, buf});
      }

      for (const auto& hr : pc.hr_items) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "  HR    y=%4d        @x=%u  w=%u", hr.y_offset + pc.vertical_offset,
                      hr.x_offset, hr.width);
        items.push_back({hr.y_offset + pc.vertical_offset, 1, buf});
      }

      // Sort by y.
      std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.y < b.y; });

      if (items.empty()) {
        out << "  (empty page)\n";
      } else {
        for (const auto& it : items)
          out << it.label << '\n';

        // Check for overlaps between consecutive items.
        for (size_t i = 1; i < items.size(); ++i) {
          int prev_bottom = items[i - 1].y + items[i - 1].h;
          int gap = items[i].y - prev_bottom;
          if (gap < 0) {
            out << "  *** OVERLAP: item " << i << " starts at y=" << items[i].y << " but prev ends at y=" << prev_bottom
                << " (overlap=" << -gap << "px) ***\n";
          } else if (gap > kParaSp + font.y_advance()) {
            // Large unexpected gap.
            out << "  --- gap " << gap << "px between items " << (i - 1) << " and " << i << " ---\n";
          }
        }

        // Gap from last item bottom to usable page bottom.
        int last_bottom = items.back().y + items.back().h;
        int usable_bottom = page_height - kPadBottom;
        int tail_gap = usable_bottom - last_bottom;
        out << "  [tail gap to usable bottom: " << tail_gap << "px"
            << " | usable area: " << (usable_bottom - kPadTop) << "px]\n";
      }

      if (pc.at_chapter_end)
        break;

      // Safety guard.
      if (page_in_chapter > 5000)
        break;

      pos = pc.end;
    }

    write_separator();
  }

  mrb.close();
  std::remove(mrb_path.c_str());

  out.close();
  std::cout << "Layout debug: " << out_path << '\n';
}
