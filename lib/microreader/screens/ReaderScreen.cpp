#include "ReaderScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifndef ESP_PLATFORM
#include <chrono>
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#endif

namespace microreader {

// ---------------------------------------------------------------------------
// ReaderScreen — image size resolution
// ---------------------------------------------------------------------------

bool ReaderScreen::resolve_image_size_(uint16_t key, uint16_t& w, uint16_t& h) {
  if (key >= static_cast<uint16_t>(dim_cache_.size()))
    return false;
  auto& dim = dim_cache_[key];
  if (dim.width != 0 || dim.height != 0) {
    w = dim.width;
    h = dim.height;
    return true;
  }
  const auto& ref = mrb_.image_ref(key);
  if (ref.width != 0 || ref.height != 0) {
    scaled_size(ref.width, ref.height, static_cast<uint16_t>(DrawBuffer::kWidth),
                static_cast<uint16_t>(DrawBuffer::kHeight), dim.width, dim.height);
    w = dim.width;
    h = dim.height;
    return true;
  }
  // Read source dimensions from the image header via streaming.
  StdioZipFile file;
  if (!file.open(path_))
    return false;
  ZipEntry entry;
  if (ZipReader::read_local_entry(file, ref.local_header_offset, entry) != ZipError::Ok)
    return false;

  // Open a ZipEntryInput — use a small stack buffer for stored entries,
  // fall back to heap for deflate.
  uint8_t small_buf[256];
  ZipEntryInput inp;
  std::unique_ptr<uint8_t[]> heap_buf;
  auto zerr = inp.open(file, entry, small_buf, sizeof(small_buf));
  if (zerr != ZipError::Ok) {
    heap_buf.reset(new (std::nothrow) uint8_t[ZipEntryInput::kMinWorkBufSize]);
    if (!heap_buf)
      return false;
    zerr = inp.open(file, entry, heap_buf.get(), ZipEntryInput::kMinWorkBufSize);
    if (zerr != ZipError::Ok)
      return false;
  }

  ImageSizeStream stream;
  uint8_t chunk[256];
  for (;;) {
    size_t n = inp.read(chunk, sizeof(chunk));
    if (n == 0)
      break;
    if (stream.feed(chunk, n))
      break;
  }
  if (!stream.ok())
    return false;

  scaled_size(stream.width(), stream.height(), static_cast<uint16_t>(DrawBuffer::kWidth),
              static_cast<uint16_t>(DrawBuffer::kHeight), dim.width, dim.height);
  w = dim.width;
  h = dim.height;
  return true;
}

bool ReaderScreen::decode_image_to_buffer_(uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y, uint16_t max_w,
                                           uint16_t max_h) {
  StdioZipFile file;
  if (!file.open(path_))
    return false;
  ZipEntry entry;
  if (ZipReader::read_local_entry(file, offset, entry) != ZipError::Ok)
    return false;

  // Set up a sink that blits each dithered row directly to the DrawBuffer.
  struct BlitCtx {
    DrawBuffer* buf;
    int x, y;
  };
  BlitCtx ctx{&buf, dest_x, dest_y};
  ImageRowSink sink;
  sink.ctx = &ctx;
  sink.emit_row = [](void* c, uint16_t row, const uint8_t* data, uint16_t width) {
    auto* bc = static_cast<BlitCtx*>(c);
    bc->buf->blit_1bit_row(bc->x, bc->y + row, data, width);
  };

  // Use the active display buffer as the work buffer to avoid a 44KB heap
  // allocation.  The active buffer is safe to overwrite here: it is not
  // needed for this render pass and will be cleared before the next refresh.
  DecodedImage dims;  // only width/height will be set; data stays empty
  auto err = decode_image_from_entry(file, entry, max_w, max_h, dims, buf.scratch_buf2(), DrawBuffer::kBufSize,
                                     /*scale_to_fill=*/true, &sink);
  return err == ImageError::Ok;
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Bookmark / key-file helpers (must precede start())
// ---------------------------------------------------------------------------

// Sanitize an arbitrary string into a safe filename component (lowercase alnum + hyphens).
static void sanitize_append(std::string& out, const std::string& s) {
  bool last_dash = !out.empty() && out.back() == '-';
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out += static_cast<char>(c);
      last_dash = false;
    } else if (c >= 'A' && c <= 'Z') {
      out += static_cast<char>(c + 32);
      last_dash = false;
    } else if (!last_dash) {
      out += '-';
      last_dash = true;
    }
  }
}

// Build a stable book key from all available metadata fields.
// title + author + language together uniquely identify most books (including
// the same book in different languages or by different authors).
// Falls back to the epub basename if title is absent.
static std::string make_book_key(const EpubMetadata& meta, const char* epub_path) {
  if (!meta.title.empty()) {
    std::string key;
    key.reserve(80);
    sanitize_append(key, meta.title);
    if (meta.author && !meta.author->empty()) {
      key += '-';
      sanitize_append(key, *meta.author);
    }
    if (meta.language && !meta.language->empty()) {
      key += '-';
      sanitize_append(key, *meta.language);
    }
    // Trim trailing dash.
    while (!key.empty() && key.back() == '-')
      key.pop_back();
    if (key.size() > 80)
      key.resize(80);
    if (!key.empty())
      return key;
  }
  // Fallback: epub basename.
  const char* name = epub_path;
  const char* sep = std::strrchr(epub_path, '/');
#ifdef _WIN32
  const char* bsep = std::strrchr(epub_path, '\\');
  if (bsep && (!sep || bsep > sep))
    sep = bsep;
#endif
  if (sep)
    name = sep + 1;
  const char* dot = std::strrchr(name, '.');
  size_t len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
  return std::string(name, len);
}

ReaderScreen::ReaderScreen(const char* epub_path) : path_(epub_path) {}

void ReaderScreen::start(DrawBuffer& buf) {
  buf_ = &buf;
  book_key_.clear();

  // Build .mrb path from epub filename: <data_dir>/<basename>.mrb
  {
    const char* name = path_;
    const char* sep = std::strrchr(path_, '/');
#ifdef _WIN32
    const char* bsep = std::strrchr(path_, '\\');
    if (bsep && (!sep || bsep > sep))
      sep = bsep;
#endif
    if (sep)
      name = sep + 1;
    const char* dot = std::strrchr(name, '.');
    size_t name_len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
    mrb_path_ = std::string(data_dir_) + "/" + std::string(name, name_len) + ".mrb";
  }

  bool mrb_ok = mrb_.open(mrb_path_.c_str());

  if (!mrb_ok) {
#ifdef ESP_PLATFORM
    int64_t open_start = esp_timer_get_time();
#endif
    auto err = book_.open(path_, buf.scratch_buf1(), buf.scratch_buf2());
#ifdef ESP_PLATFORM
    long open_ms = (long)((esp_timer_get_time() - open_start) / 1000);
    ESP_LOGI("perf", "Book::open: %ldms", open_ms);
#endif
    if (err != EpubError::Ok || book_.chapter_count() == 0) {
      open_ok_ = false;
      goto show_error;
    }

#ifdef ESP_PLATFORM
    int64_t conv_start = esp_timer_get_time();
#endif
    if (!convert_epub_to_mrb_streaming(book_, mrb_path_.c_str(), buf.scratch_buf1(), buf.scratch_buf2())) {
      open_ok_ = false;
      goto show_error;
    }
#ifdef ESP_PLATFORM
    long conv_ms = (long)((esp_timer_get_time() - conv_start) / 1000);
    long total_ms = (long)((esp_timer_get_time() - open_start) / 1000);
    ESP_LOGI("perf", "Conversion: %ldms  (open+convert=%ldms)", conv_ms, total_ms);
#endif
    book_.close();

    // Reset both display buffers to white after scratch use (conversion
    // corrupted them). render_page_ will fill the inactive buffer fresh.
    buf.reset_after_scratch(true);

    mrb_ok = mrb_.open(mrb_path_.c_str());
    if (!mrb_ok) {
      open_ok_ = false;
      goto show_error;
    }
  }

  // Derive a stable book key from MRB metadata (title + author).
  // This survives epub file renames while staying unique across different books.
  book_key_ = make_book_key(mrb_.metadata(), path_);

  open_ok_ = true;
  chapter_idx_ = 0;
  page_pos_ = PagePosition{0, 0};
  saved_chapter_idx_ = 0;
  saved_page_pos_ = PagePosition{0, 0};
  dim_cache_.assign(mrb_.image_count(), ImageDims{});
  // Restore position: if the user selected a chapter from the TOC, jump there;
  // otherwise load saved bookmark from disk (defaults to 0/{0,0} on first open).
  if (chapter_select_.has_pending()) {
    saved_chapter_idx_ = chapter_select_.pending_chapter();
    saved_page_pos_ = PagePosition{chapter_select_.pending_para_index(), 0};
    chapter_select_.clear_pending();
  } else {
    load_position_();
  }
  load_chapter_(saved_chapter_idx_);
  if (!chapter_src_) {
    // Fallback to chapter 0 if saved index is invalid.
    saved_chapter_idx_ = 0;
    saved_page_pos_ = PagePosition{0, 0};
    load_chapter_(0);
  }
  if (!chapter_src_) {
    open_ok_ = false;
    goto show_error;
  }
  page_pos_ = saved_page_pos_;
  render_page_(buf);
#ifdef ESP_PLATFORM
  ESP_LOGI("reader", "BOOK_OK: %s", path_);
#endif
  return;

show_error:
#ifdef ESP_PLATFORM
  ESP_LOGE("reader", "BOOK_FAIL: %s", path_);
#endif
  buf.fill(true);
  buf.draw_text(kPaddingLeft, kPaddingTop, "Failed to open book", true, kScale);
}

void ReaderScreen::stop() {
  if (open_ok_)
    save_position_();
  dim_cache_.clear();
  dim_cache_.shrink_to_fit();
  chapter_src_.reset();
  mrb_.close();
  book_.close();
  page_ = PageContent{};
  mrb_path_.clear();
  mrb_path_.shrink_to_fit();
  book_key_.clear();
  book_key_.shrink_to_fit();
  open_ok_ = false;
  nav_chosen_ = nullptr;
  buf_ = nullptr;
}

bool ReaderScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (buttons.is_pressed(Button::Button0))
    return false;

  if (!open_ok_)
    return true;

  // Button1: open chapter list (only if TOC is available).
  if (buttons.is_pressed(Button::Button1) && !mrb_.toc().entries.empty()) {
    saved_chapter_idx_ = chapter_idx_;
    saved_page_pos_ = page_pos_;
    chapter_select_.populate(mrb_.toc(), static_cast<uint16_t>(chapter_idx_), page_pos_.paragraph);
    nav_chosen_ = &chapter_select_;
    return false;
  }

  bool changed = false;
  if (buttons.is_pressed(Button::Button2))
    changed = next_page_();
  if (buttons.is_pressed(Button::Button3))
    changed = prev_page_();

  if (changed) {
    render_page_(buf);
    buf.refresh();
  }

  return true;
}

void ReaderScreen::load_chapter_(size_t idx) {
  chapter_src_.reset();
  if (idx < mrb_.chapter_count()) {
    chapter_src_ = std::make_unique<MrbChapterSource>(mrb_, static_cast<uint16_t>(idx));
    chapter_idx_ = idx;
  }
}

void ReaderScreen::render_page_(DrawBuffer& buf) {
  static constexpr int W = DrawBuffer::kWidth;
  static constexpr int H = DrawBuffer::kHeight;

#ifdef ESP_PLATFORM
  int64_t t0 = esp_timer_get_time();
#else
  auto t0 = std::chrono::high_resolution_clock::now();
#endif

  // Use proportional font if available, otherwise fallback to fixed.
  FixedFont fixed_font(kGlyphW * kScale, kGlyphH * kScale + 4);
  const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
  IFont& font = fset ? static_cast<IFont&>(const_cast<BitmapFontSet&>(*fset)) : static_cast<IFont&>(fixed_font);
  PageOptions opts(static_cast<uint16_t>(W), static_cast<uint16_t>(H), kPaddingTop, kParaSpacing, Alignment::Start);
  opts.padding_right = kPaddingRight;
  opts.padding_bottom = kPaddingBottom;
  opts.padding_left = kPaddingLeft;
  opts.center_text = true;

  page_ = layout_page(font, opts, *chapter_src_, page_pos_,
                      [this](uint16_t key, uint16_t& w, uint16_t& h) { return resolve_image_size_(key, w, h); });

  // ── Collect image positions from layout ─────────────────────────────────
  struct ImageToDraw {
    int x, y, w, h;
    uint32_t offset;
  };
  std::vector<ImageToDraw> images;
  for (const auto& img_item : page_.image_items) {
    const int img_w = static_cast<int>(img_item.width);
    const int img_h = static_cast<int>(img_item.height);
    if (img_w <= 0 || img_h <= 0)
      continue;
    if (img_item.key >= static_cast<uint16_t>(dim_cache_.size()))
      continue;

    ImageToDraw itd;
    itd.x = static_cast<int>(img_item.x_offset);
    itd.y = static_cast<int>(img_item.y_offset + page_.vertical_offset);
    itd.w = img_w;
    itd.h = img_h;
    itd.offset = mrb_.image_ref(img_item.key).local_header_offset;
    images.push_back(itd);
  }

  // ── Build word list ─────────────────────────────────────────────────────
  struct DrawWord {
    int x, y, len, glyph_advance;
    FontStyle style;
    FontSize size;
    VerticalAlign vertical_align;
    char text[64];
  };
  std::vector<DrawWord> words;
  for (const auto& item : page_.text_items) {
    for (const auto& w : item.line.words) {
      DrawWord dw;
      dw.x = kPaddingLeft + w.x;
      dw.y = item.y_offset + page_.vertical_offset;
      dw.len = static_cast<int>(w.len);
      if (dw.len > 63)
        dw.len = 63;
      std::memcpy(dw.text, w.text, dw.len);
      dw.text[dw.len] = '\0';
      dw.style = w.style;
      dw.size = w.size;
      dw.vertical_align = w.vertical_align;
      dw.glyph_advance = font.char_width(' ', w.style, w.size);
      words.push_back(dw);
    }
  }

  // ── Draw ────────────────────────────────────────────────────────────────
  // White background.
  buf.fill(true);

  // Text words — draw glyphs only (background already white).
  for (const auto& dw : words) {
    if (dw.len == 0)
      continue;
    if (fset) {
      // y_offset is line top; draw_text_proportional expects baseline Y.
      // Use Normal baseline for all sizes so mixed-size text shares one baseline.
      int baseline_y = dw.y + fset->baseline(FontSize::Normal);
      // Vertical shift for superscript/subscript.
      if (dw.vertical_align == VerticalAlign::Super)
        baseline_y -= fset->y_advance(FontSize::Normal) * 20 / 100;
      else if (dw.vertical_align == VerticalAlign::Sub)
        baseline_y += fset->y_advance(FontSize::Normal) * 20 / 100;
      buf.draw_text_proportional(dw.x, baseline_y, dw.text, static_cast<size_t>(dw.len), *fset, false /*black*/,
                                 dw.style, dw.size);
    } else {
      buf.draw_text_no_bg(dw.x, dw.y, dw.text, false /*black*/, kScale);
    }
  }

  // Horizontal rules.
  for (const auto& hr : page_.hr_items) {
    buf.fill_rect(static_cast<int>(hr.x_offset), static_cast<int>(hr.y_offset + page_.vertical_offset),
                  static_cast<int>(hr.width), 1, false);
  }

  // Images — decode directly to the display buffer (no intermediate bitmap).
  for (const auto& itd : images) {
    if (!decode_image_to_buffer_(itd.offset, buf, itd.x, itd.y, static_cast<uint16_t>(itd.w),
                                 static_cast<uint16_t>(itd.h))) {
      // Fallback: black rectangle for failed decodes.
      buf.fill_rect(itd.x, itd.y, itd.w, itd.h, false);
    }
  }

  // ── Progress percentage ───────────────────────────────────────────────
  if (mrb_.paragraph_count() > 0) {
    const bool is_last_chapter = chapter_idx_ + 1 >= mrb_.chapter_count();
    uint32_t cur = page_pos_.paragraph;
    for (size_t i = 0; i < chapter_idx_; ++i)
      cur += mrb_.chapter_paragraph_count(static_cast<uint16_t>(i));
    int pct = (page_.at_chapter_end && is_last_chapter) ? 100 : static_cast<int>(cur * 100u / mrb_.paragraph_count());
    char pct_str[8];
    snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
    buf.draw_text_centered(W / 2, H - 14, pct_str, true);
  }

  // ── Timing ──────────────────────────────────────────────────────────────
#ifdef ESP_PLATFORM
  long render_us = (long)(esp_timer_get_time() - t0);
  ESP_LOGI("perf", "render_page: %ldus (%ld.%ldms) words=%d images=%d", render_us, render_us / 1000,
           (render_us % 1000) / 100, (int)words.size(), (int)images.size());
#else
  auto t1 = std::chrono::high_resolution_clock::now();
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  printf("[perf] render_page: %lldus (%.1fms) words=%d images=%d\n", (long long)us, us / 1000.0, (int)words.size(),
         (int)images.size());
#endif
}

bool ReaderScreen::next_page_() {
  if (page_.at_chapter_end) {
    if (chapter_idx_ + 1 < mrb_.chapter_count()) {
      load_chapter_(chapter_idx_ + 1);
      page_pos_ = PagePosition{0, 0};
      return true;
    }
    return false;
  }
  page_pos_ = page_.end;
  return true;
}

bool ReaderScreen::prev_page_() {
  if (page_pos_ == PagePosition{0, 0}) {
    if (chapter_idx_ > 0) {
      load_chapter_(chapter_idx_ - 1);
      const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
      FixedFont fixed_font(kGlyphW * kScale, kGlyphH * kScale + 4);
      IFont& font = fset ? static_cast<IFont&>(const_cast<BitmapFontSet&>(*fset)) : static_cast<IFont&>(fixed_font);
      PageOptions opts(static_cast<uint16_t>(DrawBuffer::kWidth), static_cast<uint16_t>(DrawBuffer::kHeight),
                       kPaddingTop, kParaSpacing, Alignment::Start);
      opts.padding_right = kPaddingRight;
      opts.padding_bottom = kPaddingBottom;
      opts.padding_left = kPaddingLeft;
      opts.center_text = true;
      auto para_count = static_cast<uint16_t>(chapter_src_->paragraph_count());
      auto pc = layout_page_backward(
          font, opts, *chapter_src_, PagePosition{para_count, 0},
          [this](uint16_t key, uint16_t& w, uint16_t& h) { return resolve_image_size_(key, w, h); });
      page_pos_ = pc.start;
      return true;
    }
    return false;
  }

  const BitmapFontSet* fset2 = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
  FixedFont fixed_font(kGlyphW * kScale, kGlyphH * kScale + 4);
  IFont& font = fset2 ? static_cast<IFont&>(const_cast<BitmapFontSet&>(*fset2)) : static_cast<IFont&>(fixed_font);
  PageOptions opts(static_cast<uint16_t>(DrawBuffer::kWidth), static_cast<uint16_t>(DrawBuffer::kHeight), kPaddingTop,
                   kParaSpacing, Alignment::Start);
  opts.padding_right = kPaddingRight;
  opts.padding_bottom = kPaddingBottom;
  opts.padding_left = kPaddingLeft;
  opts.center_text = true;
  auto pc = layout_page_backward(font, opts, *chapter_src_, page_pos_, [this](uint16_t key, uint16_t& w, uint16_t& h) {
    return resolve_image_size_(key, w, h);
  });
  page_pos_ = pc.start;
  return true;
}

// ---------------------------------------------------------------------------
// Bookmark persistence
// ---------------------------------------------------------------------------

void ReaderScreen::save_position_() {
  if (!data_dir_ || book_key_.empty())
    return;
  std::string path = std::string(data_dir_) + "/" + book_key_ + ".pos";
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f)
    return;
  std::fprintf(f, "%u %u %u\n", static_cast<unsigned>(chapter_idx_), static_cast<unsigned>(page_pos_.paragraph),
               static_cast<unsigned>(page_pos_.line));
  std::fclose(f);
}

void ReaderScreen::load_position_() {
  if (!data_dir_ || book_key_.empty())
    return;
  std::string path = std::string(data_dir_) + "/" + book_key_ + ".pos";
  FILE* f = std::fopen(path.c_str(), "r");
  if (!f)
    return;
  unsigned ch = 0, para = 0, line = 0;
  if (std::fscanf(f, "%u %u %u", &ch, &para, &line) == 3) {
    saved_chapter_idx_ = ch;
    saved_page_pos_ = PagePosition{static_cast<uint16_t>(para), static_cast<uint16_t>(line)};
  }
  std::fclose(f);
}

}  // namespace microreader
