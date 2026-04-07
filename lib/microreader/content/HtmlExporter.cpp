#include "HtmlExporter.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "ImageDecoder.h"

namespace microreader {

// Resolves and caches image dimensions from an open Book.
// Callable as a function: provider(key, w, h).
struct BookImageSizeProvider {
  explicit BookImageSizeProvider(Book& book) : book_(book) {}

  bool operator()(uint16_t key, uint16_t& w, uint16_t& h) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      w = it->second.first;
      h = it->second.second;
      return w != 0 || h != 0;
    }
    std::vector<uint8_t> raw;
    if (book_.extract_entry(key, raw) != ZipError::Ok) {
      cache_[key] = {0, 0};
      return false;
    }
    uint16_t rw = 0, rh = 0;
    bool ok = get_image_size(raw.data(), raw.size(), rw, rh);
    cache_[key] = {rw, rh};
    w = rw;
    h = rh;
    return ok;
  }

 private:
  Book& book_;
  std::unordered_map<uint16_t, std::pair<uint16_t, uint16_t>> cache_;
};

// ---------------------------------------------------------------------------
// HTML escaping
// ---------------------------------------------------------------------------

static std::string html_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
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

// ---------------------------------------------------------------------------
// Convert 1-bit packed bitmap to BMP for embedding
// (simpler than PNG, no compression needed)
// ---------------------------------------------------------------------------

static std::vector<uint8_t> bitmap_to_bmp(const DecodedImage& img) {
  // BMP with 1-bit color depth
  uint32_t row_stride = ((img.width + 31) / 32) * 4;  // BMP rows are 4-byte aligned
  uint32_t pixel_data_size = row_stride * img.height;
  uint32_t file_size = 14 + 40 + 8 + pixel_data_size;  // header + DIB + palette + pixels

  std::vector<uint8_t> bmp(file_size, 0);
  uint8_t* p = bmp.data();

  // File header (14 bytes)
  p[0] = 'B';
  p[1] = 'M';
  memcpy(p + 2, &file_size, 4);
  uint32_t offset = 14 + 40 + 8;
  memcpy(p + 10, &offset, 4);

  // DIB header (BITMAPINFOHEADER, 40 bytes)
  uint32_t dib_size = 40;
  memcpy(p + 14, &dib_size, 4);
  int32_t w = img.width;
  int32_t h = img.height;  // positive = bottom-up
  memcpy(p + 18, &w, 4);
  memcpy(p + 22, &h, 4);
  uint16_t planes = 1;
  memcpy(p + 26, &planes, 2);
  uint16_t bpp = 1;
  memcpy(p + 28, &bpp, 2);
  // compression = 0 (none), size, resolution, colors...
  memcpy(p + 34, &pixel_data_size, 4);
  uint32_t colors_used = 2;
  memcpy(p + 46, &colors_used, 4);

  // Color palette: black (0,0,0,0) and white (255,255,255,0)
  uint8_t* palette = p + 54;
  palette[0] = 0;
  palette[1] = 0;
  palette[2] = 0;
  palette[3] = 0;
  palette[4] = 255;
  palette[5] = 255;
  palette[6] = 255;
  palette[7] = 0;

  // Pixel data (bottom-up, our bitmap is top-down)
  uint8_t* pixels = p + offset;
  size_t src_stride = img.stride();
  for (uint16_t y = 0; y < img.height; ++y) {
    uint16_t src_y = img.height - 1 - y;  // flip vertically
    const uint8_t* src_row = img.data.data() + src_y * src_stride;
    uint8_t* dst_row = pixels + y * row_stride;
    memcpy(dst_row, src_row, src_stride);
  }

  return bmp;
}

// ---------------------------------------------------------------------------
// Write a BMP file to disk.  Returns true on success.
// ---------------------------------------------------------------------------

static bool write_bmp_file(const std::vector<uint8_t>& bmp, const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f)
    return false;
  size_t written = fwrite(bmp.data(), 1, bmp.size(), f);
  fclose(f);
  return written == bmp.size();
}

// ---------------------------------------------------------------------------
// Write helpers
// ---------------------------------------------------------------------------

static void write(FILE* f, const char* s) {
  fputs(s, f);
}

static void writef(FILE* f, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(f, fmt, args);
  va_end(args);
}

// ---------------------------------------------------------------------------
// Style to CSS
// ---------------------------------------------------------------------------

static const char* style_class(FontStyle s) {
  switch (s) {
    case FontStyle::Bold:
      return "b";
    case FontStyle::Italic:
      return "i";
    case FontStyle::BoldItalic:
      return "bi";
    default:
      return nullptr;
  }
}

static const char* size_class(FontSize s) {
  switch (s) {
    case FontSize::Small:
      return "sz-s";
    case FontSize::Large:
      return "sz-l";
    default:
      return nullptr;
  }
}

static const char* align_css(Alignment a) {
  switch (a) {
    case Alignment::Center:
      return "center";
    case Alignment::End:
      return "right";
    case Alignment::Justify:
      return "justify";
    default:
      return "left";
  }
}

// ---------------------------------------------------------------------------
// export_to_html()
// ---------------------------------------------------------------------------

bool export_to_html(Book& book, const IFont& font, const HtmlExportOptions& opts, const char* output_path) {
  FILE* f = fopen(output_path, "w");
  if (!f)
    return false;

  // Create per-book img/<stem>/ subdirectory next to the HTML file for image output
  namespace fs = std::filesystem;
  fs::path html_path(output_path);
  std::string stem = html_path.stem().string();
  fs::path img_dir = html_path.parent_path() / "img" / stem;
  fs::create_directories(img_dir);
  std::string img_rel = "img/" + stem;
  std::string img_dir_str = img_dir.string();
  size_t img_counter = 0;

  const auto& meta = book.metadata();
  std::string title_esc = html_escape(meta.title);

  // --- HTML head with embedded CSS ---
  writef(f, "<!DOCTYPE html>\n<html lang=\"%s\">\n<head>\n", meta.language.value_or("en").c_str());
  writef(f, "<meta charset=\"utf-8\">\n");
  writef(f, "<title>%s</title>\n", title_esc.c_str());
  write(f, "<style>\n");
  // Compute CSS font-size so monospace character width matches layout engine's glyph_width.
  // Courier New advance width ≈ 0.6 * font-size.
  uint16_t gw = font.char_width('m', FontStyle::Regular);
  uint16_t lh = font.y_advance();
  double css_font_size = gw / 0.6;

  writef(f,
         "body { font-family: 'Courier New', monospace; font-size: %.2fpx; "
         "line-height: %dpx; margin: 20px; "
         "background: #f5f5dc; color: #222; }\n",
         css_font_size, lh);
  write(f,
        "h1 { text-align: center; border-bottom: 2px solid #666; padding-bottom: 10px; }\n"
        ".meta { text-align: center; color: #666; margin-bottom: 30px; }\n"
        ".toc { margin: 20px 0; padding: 15px; background: #eee; border-radius: 5px; }\n"
        ".toc h2 { margin-top: 0; }\n"
        ".toc ul { list-style: none; padding-left: 0; }\n"
        ".toc li { padding: 3px 0; }\n"
        ".toc a { text-decoration: none; color: #336; }\n"
        ".toc a:hover { text-decoration: underline; }\n"
        ".chapter { margin: 40px 0; page-break-before: always; }\n"
        ".ch-title { color: #333; border-bottom: 1px solid #999; padding-bottom: 5px; }\n"
        ".pages { display: flex; flex-wrap: wrap; gap: 6px; justify-content: center; }\n");
  writef(f,
         ".page { position: relative; width: %dpx; height: %dpx; "
         "flex-shrink: 0; transform: scale(0.5); transform-origin: top left; "
         "margin-right: -%dpx; margin-bottom: -%dpx; "
         "background: white; border: 1px solid #ccc; box-shadow: 2px 2px 5px rgba(0,0,0,0.1); "
         "overflow: hidden; }\n",
         opts.page_width, opts.page_height, opts.page_width / 2, opts.page_height / 2);
  write(f,
        ".page-num { position: absolute; bottom: 2px; right: 5px; font-size: 9px; "
        "color: #bbb; font-family: monospace; }\n"
        ".line { position: absolute; margin: 0; white-space: nowrap; }\n"
        ".w { position: absolute; white-space: pre; }\n"
        ".b { font-weight: bold; }\n"
        ".i { font-style: italic; }\n"
        ".bi { font-weight: bold; font-style: italic; }\n"
        ".sz-s { font-size: 0.75em; color: #1a6e1a; }\n"
        ".sz-l { font-size: 1.25em; color: #8b2252; }\n"
        ".img-abs { position: absolute; }\n"
        ".img-abs img { border: 1px solid #ddd; image-rendering: pixelated; }\n"
        ".img-label { font-size: 10px; color: #999; font-family: monospace; }\n"
        ".hr-abs { position: absolute; height: 1px; background: #aaa; }\n"
        ".debug { font-size: 10px; color: #aaa; font-family: monospace; }\n"
        ".stats { background: #f0f0f0; padding: 10px; margin: 10px 0; font-size: 12px; "
        "font-family: monospace; border-radius: 3px; }\n");
  write(f, "</style>\n</head>\n<body>\n");

  // --- Title & metadata ---
  writef(f, "<h1>%s</h1>\n", title_esc.c_str());
  write(f, "<div class=\"meta\">\n");
  if (meta.author)
    writef(f, "<p>by %s</p>\n", html_escape(*meta.author).c_str());
  if (meta.language)
    writef(f, "<p>Language: %s</p>\n", html_escape(*meta.language).c_str());
  writef(f, "<p>Chapters: %zu</p>\n", book.chapter_count());
  write(f, "</div>\n");

  // --- Build zip-entry-index → spine-index map for TOC links ---
  const auto& spine = book.epub().spine();
  std::map<uint16_t, size_t> zip_to_spine;
  for (size_t si = 0; si < spine.size(); ++si) {
    zip_to_spine.emplace(spine[si].file_idx, si);  // first occurrence wins
  }

  // --- Table of Contents ---
  const auto& toc = book.toc();
  if (!toc.entries.empty()) {
    write(f, "<div class=\"toc\">\n<h2>Table of Contents</h2>\n<ul>\n");
    for (size_t i = 0; i < toc.entries.size(); ++i) {
      auto it = zip_to_spine.find(toc.entries[i].file_idx);
      if (it != zip_to_spine.end()) {
        writef(f, "<li><a href=\"#ch%zu\">%s</a></li>\n", it->second, html_escape(toc.entries[i].label).c_str());
      } else {
        // TOC entry references a file not in the spine — emit without link
        writef(f, "<li>%s</li>\n", html_escape(toc.entries[i].label).c_str());
      }
    }
    write(f, "</ul>\n</div>\n");
  }

  // --- Layout settings ---
  PageOptions page_opts;
  page_opts.width = opts.page_width;
  page_opts.height = opts.page_height;
  page_opts.padding = opts.padding;
  page_opts.para_spacing = opts.para_spacing;
  page_opts.alignment = opts.alignment;

  // --- Render each chapter ---
  size_t total_pages = 0;
  size_t total_paragraphs = 0;
  size_t total_images_decoded = 0;
  size_t total_images_failed = 0;

  size_t num_chapters = book.chapter_count();
  if (opts.max_chapters > 0 && opts.max_chapters < num_chapters)
    num_chapters = opts.max_chapters;

  for (size_t ci = 0; ci < num_chapters; ++ci) {
    Chapter ch;
    EpubError err = book.load_chapter(ci, ch);

    writef(f, "<div class=\"chapter\" id=\"ch%zu\">\n", ci);
    writef(f, "<h2 class=\"ch-title\">Chapter %zu", ci + 1);
    if (ch.title)
      writef(f, ": %s", html_escape(*ch.title).c_str());
    write(f, "</h2>\n");

    if (err != EpubError::Ok) {
      writef(f, "<p style=\"color:red\">Error loading chapter: %d</p>\n", static_cast<int>(err));
      write(f, "</div>\n");
      continue;
    }

    if (opts.show_debug_info) {
      size_t text_count = 0, img_count = 0, hr_count = 0;
      for (const auto& p : ch.paragraphs) {
        switch (p.type) {
          case ParagraphType::Text:
            ++text_count;
            break;
          case ParagraphType::Image:
            ++img_count;
            break;
          case ParagraphType::Hr:
            ++hr_count;
            break;
          case ParagraphType::PageBreak:
            break;
        }
      }
      writef(f, "<div class=\"stats\">%zu paragraphs (%zu text, %zu images, %zu hr)</div>\n", ch.paragraphs.size(),
             text_count, img_count, hr_count);
      total_paragraphs += ch.paragraphs.size();
    }

    // Paginate the chapter
    PagePosition pos;
    pos.paragraph = 0;
    pos.line = 0;
    int page_num = 0;

    write(f, "<div class=\"pages\">\n");

    BookImageSizeProvider size_prov(book);
    while (true) {
      auto page = layout_page(font, page_opts, ch, pos, std::ref(size_prov));
      ++page_num;
      ++total_pages;

      write(f, "<div class=\"page\">\n");

      uint16_t pad = opts.padding;
      uint16_t voff = page.vertical_offset;

      // Render text lines at absolute positions, each word at its computed x
      for (size_t ti = 0; ti < page.text_items.size(); ++ti) {
        const auto& item = page.text_items[ti];

        writef(f, "<div class=\"line\" style=\"left:%dpx;top:%dpx\">\n", pad, pad + voff + item.y_offset);

        for (size_t wi = 0; wi < item.line.words.size(); ++wi) {
          const auto& w = item.line.words[wi];
          std::string word_text = html_escape(std::string(w.text, w.len));

          const char* scls = style_class(w.style);
          const char* zcls = size_class(w.size);
          // Build class attribute
          std::string cls;
          if (scls)
            cls += scls;
          if (scls && zcls)
            cls += ' ';
          if (zcls)
            cls += zcls;

          // Vertical offset for superscript/subscript
          int va_offset = 0;
          if (w.vertical_align == VerticalAlign::Super)
            va_offset = -(font.y_advance(FontSize::Normal) * 30 / 100);
          else if (w.vertical_align == VerticalAlign::Sub)
            va_offset = font.y_advance(FontSize::Normal) * 20 / 100;

          if (!cls.empty()) {
            if (va_offset != 0)
              writef(f, "<span class=\"w %s\" style=\"left:%dpx;top:%dpx\">%s</span>", cls.c_str(), w.x, va_offset,
                     word_text.c_str());
            else
              writef(f, "<span class=\"w %s\" style=\"left:%dpx\">%s</span>", cls.c_str(), w.x, word_text.c_str());
          } else {
            if (va_offset != 0)
              writef(f, "<span class=\"w\" style=\"left:%dpx;top:%dpx\">%s</span>", w.x, va_offset, word_text.c_str());
            else
              writef(f, "<span class=\"w\" style=\"left:%dpx\">%s</span>", w.x, word_text.c_str());
          }
        }

        if (item.line.hyphenated) {
          // Place hyphen right after the last word
          const auto& last = item.line.words.back();
          uint16_t hx = last.x + font.word_width(last.text, last.len, last.style, last.size);
          writef(f, "<span class=\"w\" style=\"left:%dpx\">-</span>", hx);
        }
        write(f, "</div>\n");
      }

      // Render images at absolute positions — write BMP files to img/ folder
      for (size_t ii = 0; ii < page.image_items.size(); ++ii) {
        const auto& img_item = page.image_items[ii];

        DecodedImage decoded;
        ImageError img_err = book.decode_image(img_item.key, decoded, img_item.width, img_item.height);

        if (img_err == ImageError::Ok && decoded.width > 0) {
          auto bmp = bitmap_to_bmp(decoded);
          std::string img_name = "img_" + std::to_string(img_counter++) + ".bmp";
          std::string img_path = img_dir_str + "/" + img_name;
          write_bmp_file(bmp, img_path);
          writef(f,
                 "<div class=\"img-abs\" style=\"left:%dpx;top:%dpx\">"
                 "<img src=\"%s/%s\" "
                 "width=\"%d\" height=\"%d\" alt=\"Image\">"
                 "</div>\n",
                 img_item.x_offset, (img_item.y_offset == 0 ? 0 : pad) + voff + img_item.y_offset, img_rel.c_str(),
                 img_name.c_str(), img_item.width, img_item.height);
          ++total_images_decoded;
        } else {
          writef(f,
                 "<div class=\"img-abs\" style=\"left:%dpx;top:%dpx;"
                 "width:%dpx;height:%dpx;background:#eee;"
                 "border:1px dashed #999;display:flex;align-items:center;"
                 "justify-content:center;\">"
                 "<span style=\"color:#999\">Image error %d</span></div>\n",
                 img_item.x_offset, (img_item.y_offset == 0 ? 0 : pad) + voff + img_item.y_offset, img_item.width,
                 img_item.height, static_cast<int>(img_err));
          ++total_images_failed;
        }
      }

      // Render HRs at their positioned locations
      for (const auto& hr : page.hr_items) {
        writef(f, "<div class=\"hr-abs\" style=\"left:%dpx;top:%dpx;width:%dpx\"></div>\n", hr.x_offset,
               pad + voff + hr.y_offset, hr.width);
      }

      writef(f, "<span class=\"page-num\">%d</span>\n", page_num);
      write(f, "</div>\n");  // .page

      if (page.at_chapter_end)
        break;

      // Safety: prevent infinite loops
      if (page.end <= pos) {
        writef(f, "<p style=\"color:red\">Layout stuck at paragraph %d line %d</p>\n", pos.paragraph, pos.line);
        break;
      }
      pos = page.end;

      if (page_num > 5000) {
        write(f, "<p style=\"color:red\">Chapter exceeded 5000 pages, truncated.</p>\n");
        break;
      }
    }

    write(f, "</div>\n");  // .pages
    write(f, "</div>\n");  // .chapter
  }

  // --- Summary footer ---
  write(f, "<div class=\"stats\" style=\"margin-top:40px\">\n");
  writef(f, "<strong>Export Summary</strong><br>\n");
  writef(f, "Title: %s<br>\n", title_esc.c_str());
  writef(f, "Chapters rendered: %zu / %zu<br>\n", num_chapters, book.chapter_count());
  writef(f, "Total pages: %zu<br>\n", total_pages);
  writef(f, "Total paragraphs: %zu<br>\n", total_paragraphs);
  writef(f, "Images decoded: %zu, failed: %zu<br>\n", total_images_decoded, total_images_failed);
  writef(f, "Page size: %d x %d (padding: %d)<br>\n", opts.page_width, opts.page_height, opts.padding);
  write(f, "</div>\n");

  write(f, "</body>\n</html>\n");
  fclose(f);
  return true;
}

}  // namespace microreader
