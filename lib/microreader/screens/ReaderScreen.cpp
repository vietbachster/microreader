#include "ReaderScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "../Application.h"

#ifdef ESP_PLATFORM
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#else
#include <filesystem>
#endif

namespace microreader {

// ---------------------------------------------------------------------------
// ReaderScreen â€” image size resolution
// ---------------------------------------------------------------------------

// resolve_image_size_ removed â€” image size resolution is now handled by
// make_image_size_query() (MrbReader.h), stored in image_size_fn_.

bool ReaderScreen::decode_image_to_buffer_(uint16_t img_key, uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y,
                                           uint16_t max_w, uint16_t max_h, uint16_t src_y, uint16_t clip_h) {
  char cache_path[256];
  snprintf(cache_path, sizeof(cache_path), "%s/img_%u_%ux%u.bin", book_cache_dir_.c_str(),
           static_cast<unsigned>(img_key), static_cast<unsigned>(max_w), static_cast<unsigned>(max_h));

  FILE* cache_f = std::fopen(cache_path, "rb");
  if (cache_f) {
    uint16_t header[2] = {0, 0};
    if (std::fread(header, 2, 2, cache_f) == 2) {
      uint16_t cached_w = header[0];
      uint16_t cached_h = header[1];
      uint16_t row_bytes = (cached_w + 7) / 8;
      std::vector<uint8_t> row_buf(row_bytes);
      for (uint16_t r = 0; r < cached_h; ++r) {
        if (std::fread(row_buf.data(), 1, row_bytes, cache_f) != row_bytes)
          break;
        if (r < src_y)
          continue;
        uint16_t dest_row = static_cast<uint16_t>(r - src_y);
        if (clip_h > 0 && dest_row >= clip_h)
          break;
        buf.blit_1bit_row(dest_x, dest_y + dest_row, row_buf.data(), cached_w);
      }
      std::fclose(cache_f);
      return true;
    }
    std::fclose(cache_f);
  }

  StdioZipFile file;
  if (!file.open(path_.c_str()))
    return false;
  ZipEntry entry;
  if (ZipReader::read_local_entry(file, offset, entry) != ZipError::Ok)
    return false;

  FILE* cache_w = std::fopen(cache_path, "wb");
  if (cache_w) {
    uint16_t dummy[2] = {0, 0};
    std::fwrite(dummy, 2, 2, cache_w);
  }

  // Set up a sink that blits each dithered row directly to the DrawBuffer.
  struct BlitCtx {
    DrawBuffer* buf;
    int x, y;
    uint16_t src_y;
    uint16_t clip_h;  // max rows to render (0 = no clip)
    FILE* cache_w;
    uint16_t out_w;
    uint16_t out_h;
  };
  BlitCtx ctx{&buf, dest_x, dest_y, src_y, clip_h, cache_w, 0, 0};
  ImageRowSink sink;
  sink.ctx = &ctx;
  sink.emit_row = [](void* c, uint16_t row, const uint8_t* data, uint16_t width) {
    auto* bc = static_cast<BlitCtx*>(c);
    bc->out_w = width;
    if (row >= bc->out_h)
      bc->out_h = static_cast<uint16_t>(row + 1);

    if (bc->cache_w) {
      uint16_t row_bytes = static_cast<uint16_t>((width + 7) / 8);
      std::fwrite(data, 1, row_bytes, bc->cache_w);
    }

    if (row < bc->src_y)
      return;
    uint16_t dest_row = static_cast<uint16_t>(row - bc->src_y);
    if (bc->clip_h > 0 && dest_row >= bc->clip_h)
      return;
    bc->buf->blit_1bit_row(bc->x, bc->y + dest_row, data, width);
  };

  // Use the active display buffer as the work buffer to avoid a 44KB heap
  // allocation.  The active buffer is safe to overwrite here: it is not
  // needed for this render pass and will be cleared before the next refresh.
  DecodedImage dims;  // only width/height will be set; data stays empty
  auto err = decode_image_from_entry(file, entry, max_w, max_h, dims, buf.scratch_buf2(), DrawBuffer::kBufSize,
                                     /*scale_to_fill=*/true, &sink);

  if (cache_w) {
    if (err == ImageError::Ok && ctx.out_w > 0 && ctx.out_h > 0) {
      std::fseek(cache_w, 0, SEEK_SET);
      uint16_t header[2] = {ctx.out_w, ctx.out_h};
      std::fwrite(header, 2, 2, cache_w);
    }
    std::fclose(cache_w);
    if (err != ImageError::Ok) {
#ifdef ESP_PLATFORM
      unlink(cache_path);
#else
      std::remove(cache_path);
#endif
    }
  }

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

void ReaderScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  buf_ = &buf;
  book_key_.clear();
  pos_path_.clear();

  if (app_ && app_->font_manager())
    app_->font_manager()->ensure_ready(buf);

  // Build cache path: <data_dir>/cache/<basename>/book.mrb
  {
    const char* name = path_.c_str();
    const char* sep = std::strrchr(path_.c_str(), '/');
#ifdef _WIN32
    const char* bsep = std::strrchr(path_.c_str(), '\\');
    if (bsep && (!sep || bsep > sep))
      sep = bsep;
#endif
    if (sep)
      name = sep + 1;
    const char* dot = std::strrchr(name, '.');
    size_t name_len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
    book_cache_dir_ = std::string(data_dir_) + "/cache/" + std::string(name, name_len);
#ifdef ESP_PLATFORM
    mkdir(book_cache_dir_.c_str(), 0775);
#else
    std::filesystem::create_directories(book_cache_dir_);
#endif
    mrb_path_ = book_cache_dir_ + "/book.mrb";
  }

  bool mrb_ok = mrb_.open(mrb_path_.c_str());

  if (!mrb_ok) {
    // Upload the current frame before scratch buffer use so the display
    // controller has a valid reference frame for partial refreshes.
    buf.sync_bw_ram();
    buf.show_loading("Converting...", 0);

#ifdef ESP_PLATFORM
    int64_t open_start = esp_timer_get_time();
#endif
    auto err = book_.open(path_.c_str(), buf.scratch_buf1(), buf.scratch_buf2());
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
    if (!convert_epub_to_mrb_streaming(book_, mrb_path_.c_str(), buf.scratch_buf1(), buf.scratch_buf2(),
                                       [&buf](int done, int total) {
                                         int pct = total > 0 ? (done * 100 / total) : 0;
                                         buf.show_loading("Converting...", pct);
                                       })) {
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
  book_key_ = make_book_key(mrb_.metadata(), path_.c_str());
  pos_path_ = std::string(data_dir_) + "/data/" + book_key_ + ".pos";

  open_ok_ = true;
  chapter_idx_ = 0;
  page_pos_ = PagePosition{0, 0};
  image_size_fn_ = make_image_size_query(mrb_, path_, static_cast<uint16_t>(DrawBuffer::kWidth));
  // Restore position: if the user selected a chapter from the TOC, jump there;
  // otherwise load saved bookmark from disk.
  if (app_ && app_->chapter_select()->has_pending()) {
    saved_chapter_idx_ = app_->chapter_select()->pending_chapter();
    saved_page_pos_ = PagePosition{app_->chapter_select()->pending_para_index(), 0, 0};
    app_->chapter_select()->clear_pending();
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
  layout_engine_ = TextLayout{};
  layout_engine_.set_source(*chapter_src_);
  layout_engine_.set_image_size_fn(image_size_fn_);
  layout_engine_.set_hyphenation_lang(detect_language(mrb_.metadata().language));
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
  image_size_fn_ = {};
  chapter_src_.reset();
  mrb_.close();
  book_.close();
  if (open_ok_)
    save_position_();
  page_ = PageContent{};
  mrb_path_.clear();
  mrb_path_.shrink_to_fit();
  pos_path_.clear();
  pos_path_.shrink_to_fit();
  book_key_.clear();
  book_key_.shrink_to_fit();
  open_ok_ = false;
  saved_chapter_idx_ = 0;
  saved_page_pos_ = PagePosition{0, 0, 0};
  buf_ = nullptr;
}

void ReaderScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (!open_ok_) {
    // Still drain the history so stale events don't bleed into the next frame.
    Button btn;
    while (buttons.next_press(btn)) {
      if (btn == Button::Button0) {
        app_->pop_screen();
        return;
      }
    }
    return;
  }

  // Process press events in the order they arrived.
  int page_delta = 0;
  bool had_next_press = false;
  bool had_prev_press = false;
  Button btn;
  while (buttons.next_press(btn)) {
    switch (btn) {
      case Button::Button0:
        app_->pop_screen();
        return;
      case Button::Button1:
        saved_chapter_idx_ = chapter_idx_;
        saved_page_pos_ = page_pos_;
        app_->reader_options()->set_settings(&reader_settings_);
        app_->reader_options()->populate(mrb_.toc(), static_cast<uint16_t>(chapter_idx_), page_pos_.paragraph);
        app_->push_screen(ScreenId::ReaderOptions);
        return;
      case Button::Button2:
      case Button::Up:
        ++page_delta;
        had_next_press = true;
        break;
      case Button::Button3:
      case Button::Down:
        --page_delta;
        had_prev_press = true;
        break;
      default:
        break;
    }
  }

  // Hold-down: advance one page per frame while a nav button is held,
  // but only if no fresh press event arrived this frame (avoids double-counting
  // the initial press).
  if (!had_next_press && (buttons.is_down(Button::Button2) || buttons.is_down(Button::Up)))
    ++page_delta;
  if (!had_prev_press && (buttons.is_down(Button::Button3) || buttons.is_down(Button::Down)))
    --page_delta;

  bool changed = false;
  if (page_delta > 0) {
    for (int i = 0; i < page_delta; ++i)
      changed = next_page_() || changed;
  } else if (page_delta < 0) {
    for (int i = 0; i > page_delta; --i)
      changed = prev_page_() || changed;
  }

  if (changed) {
    if (grayscale_active_) {
      buf.revert_grayscale();
      grayscale_active_ = false;
    }
    render_page_(buf);
    buf.refresh();
    save_position_();
  }

  // Deferred grayscale: only apply when no nav buttons are held, so rapid
  // page flipping stays fast and grayscale is applied once the user stops.
  if (grayscale_pending_ && !buttons.is_down(Button::Button2) && !buttons.is_down(Button::Button3) &&
      !buttons.is_down(Button::Up) && !buttons.is_down(Button::Down)) {
    grayscale_pending_ = false;
    apply_grayscale_(buf);
    grayscale_active_ = true;
  }
}

void ReaderScreen::load_chapter_(size_t idx) {
  chapter_src_.reset();
  if (idx < mrb_.chapter_count()) {
    chapter_src_ = std::make_unique<MrbChapterSource>(mrb_, static_cast<uint16_t>(idx));
    chapter_idx_ = idx;
    layout_engine_.set_source(*chapter_src_);
  }
}

void ReaderScreen::render_page_(DrawBuffer& buf) {
  static constexpr int W = DrawBuffer::kWidth;
  static constexpr int H = DrawBuffer::kHeight;

#ifdef ESP_PLATFORM
  int64_t t0 = esp_timer_get_time();
#endif

  // Use proportional font if available, otherwise fallback to fixed.
  FixedFont fixed_font(kGlyphW * kScale, kGlyphH * kScale + 4);
  const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
  if (fset) {
    const_cast<BitmapFontSet*>(fset)->set_base_size_index(reader_settings_.font_size_idx);
  }
  IFont& font = fset ? static_cast<IFont&>(const_cast<BitmapFontSet&>(*fset)) : static_cast<IFont&>(fixed_font);
  std::optional<Alignment> align_override = std::nullopt;
  if (reader_settings_.align_override != AlignOverride::Book) {
    if (reader_settings_.align_override == AlignOverride::Left)
      align_override = Alignment::Start;
    else if (reader_settings_.align_override == AlignOverride::Center)
      align_override = Alignment::Center;
    else if (reader_settings_.align_override == AlignOverride::Right)
      align_override = Alignment::End;
    else if (reader_settings_.align_override == AlignOverride::Justify)
      align_override = Alignment::Justify;
  }

  PageOptions opts(static_cast<uint16_t>(W), static_cast<uint16_t>(H), kPaddingTop, 8, align_override);
  opts.padding_right = reader_settings_.h_padding();
  opts.padding_bottom = static_cast<uint16_t>(reader_settings_.progress_bottom() + reader_settings_.v_padding());
  opts.padding_left = reader_settings_.h_padding();
  opts.padding_top = static_cast<uint16_t>(kPaddingTop + reader_settings_.v_padding());
  opts.line_height_multiplier_percent = reader_settings_.line_height_multiplier_percent();
  opts.center_text = true;
  layout_engine_.set_font(font);
  layout_engine_.set_options(opts);
  page_pos_ = layout_engine_.resolve_stable_position(page_pos_);
  layout_engine_.set_position(page_pos_);

  page_ = layout_engine_.layout();

  // â”€â”€ Collect image positions from layout
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  struct ImageToDraw {
    uint16_t key;
    int x, y, w, h;
    uint32_t offset;
    uint16_t src_y = 0;
    uint16_t clip_h = 0;  // rendered slice height (0 = full)
  };
  std::vector<ImageToDraw> images;
  auto collect_img = [&](const PageImageItem& img_item) {
    const int img_w = static_cast<int>(img_item.width);
    const int img_h = static_cast<int>(img_item.height);
    if (img_w <= 0 || img_h <= 0)
      return;
    if (img_item.key >= mrb_.image_count())
      return;
    ImageToDraw itd;
    itd.key = img_item.key;
    itd.x = static_cast<int>(img_item.x_offset);
    itd.y = static_cast<int>(img_item.y_offset);  // y_offset is absolute (vertical centering baked in)
    itd.w = img_w;
    // Use full_height as max_h so the decoder scales to the correct aspect ratio;
    // src_y crops to the visible slice within that full render.
    itd.h = img_item.full_height > 0 ? static_cast<int>(img_item.full_height) : img_h;
    itd.offset = mrb_.image_ref(img_item.key).local_header_offset;
    itd.src_y = img_item.y_crop;
    // Clip rendered rows to the slice height so the image doesn't overflow past
    // its layout-assigned area (e.g. into the page number zone or page N+1).
    itd.clip_h = static_cast<uint16_t>(img_h);
    images.push_back(itd);
  };
  for (const auto& ci : page_.items) {
    if (const PageImageItem* img = std::get_if<PageImageItem>(&ci)) {
      collect_img(*img);
    } else if (const PageTextItem* ti = std::get_if<PageTextItem>(&ci)) {
      if (ti->inline_image.has_value())
        collect_img(*ti->inline_image);
    }
  }

  // Track whether grayscale pass is needed (deferred to update()).
  grayscale_pending_ = fset && fset->has_grayscale();

  // â”€â”€ BW rendering
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  buf.fill(true);

  if (fset) {
    render_text_(buf, *fset, GrayPlane::BW, false, reader_settings_.h_padding());
  } else {
    for (const auto& ci : page_.items) {
      const PageTextItem* item = std::get_if<PageTextItem>(&ci);
      if (!item)
        continue;
      for (const auto& w : item->line.words) {
        if (w.len == 0)
          continue;
        char text[64];
        int tlen = static_cast<int>(w.len);
        if (tlen > 63)
          tlen = 63;
        std::memcpy(text, w.text, tlen);
        text[tlen] = '\0';
        buf.draw_text_no_bg(reader_settings_.h_padding() + w.x, static_cast<int>(item->y_offset), text, false /*black*/,
                            kScale);
      }
    }
  }

  for (const auto& hr : page_.items) {
    const PageHrItem* h = std::get_if<PageHrItem>(&hr);
    if (!h)
      continue;
    const int hr_y = static_cast<int>(h->y_offset) + static_cast<int>(h->height) / 2;
    buf.fill_rect(static_cast<int>(h->x_offset), hr_y, static_cast<int>(h->width), 1, false);
  }

  for (const auto& itd : images) {
    if (!decode_image_to_buffer_(itd.key, itd.offset, buf, itd.x, itd.y, static_cast<uint16_t>(itd.w),
                                 static_cast<uint16_t>(itd.h), itd.src_y, itd.clip_h)) {
      buf.fill_rect(itd.x, itd.y, itd.w, itd.h, false);
    }
  }

  if (mrb_.paragraph_count() > 0 && reader_settings_.progress_style != ProgressStyle::None) {
    const bool is_last_chapter = chapter_idx_ + 1 >= mrb_.chapter_count();
    int pct = 0;
    if (page_.at_chapter_end && is_last_chapter) {
      pct = 100;
    } else {
      const uint64_t total_chars = mrb_.total_char_count();
      uint64_t chars_before = 0;
      for (size_t i = 0; i < chapter_idx_; ++i)
        chars_before += mrb_.chapter_char_count(static_cast<uint16_t>(i));
      const uint64_t cur = chars_before + (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0);
      pct = total_chars > 0 ? static_cast<int>(cur * 100u / total_chars) : 0;
    }
    if (reader_settings_.progress_style == ProgressStyle::Percentage) {
      char pct_str[8];
      snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
      buf.draw_text_centered(W / 2, H - 14, pct_str, true);
    } else {
      // Progress bar: a thin filled line at the very bottom of the screen.
      constexpr int kBarH = 2;
      const int bar_w = pct * W / 100;
      buf.fill_rect(0, H - kBarH, bar_w, kBarH, false);         // filled portion (black)
      buf.fill_rect(bar_w, H - kBarH, W - bar_w, kBarH, true);  // unfilled portion (white)
    }
  }

  // â”€â”€ Timing
  // â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  int n_words = 0;
  for (const auto& ci : page_.items)
    if (const PageTextItem* ti = std::get_if<PageTextItem>(&ci))
      n_words += static_cast<int>(ti->line.words.size());

#ifdef ESP_PLATFORM
  long render_us = (long)(esp_timer_get_time() - t0);
  ESP_LOGI("perf", "render_page: %ldus (%ld.%ldms) words=%d images=%d", render_us, render_us / 1000,
           (render_us % 1000) / 100, n_words, (int)images.size());
#endif
}

bool ReaderScreen::render_current_page(DrawBuffer& buf) {
  if (!open_ok_)
    return false;
  render_page_(buf);
  return true;
}

bool ReaderScreen::next_page_and_render(DrawBuffer& buf) {
  if (!open_ok_)
    return false;
  if (!next_page_())
    return false;
  if (grayscale_active_) {
    buf.revert_grayscale();
    grayscale_active_ = false;
  }
  render_page_(buf);
  return true;
}

bool ReaderScreen::is_open_ok() const {
  return open_ok_;
}

size_t ReaderScreen::current_chapter_index() const {
  return chapter_idx_;
}

void ReaderScreen::render_text_(DrawBuffer& buf, const BitmapFontSet& fset, GrayPlane plane, bool white,
                                int left_padding) {
  uint8_t* render = buf.render_buf();
  for (const auto& ci : page_.items) {
    const PageTextItem* item = std::get_if<PageTextItem>(&ci);
    if (!item)
      continue;
    for (const auto& w : item->line.words) {
      if (w.len == 0)
        continue;
      int x = left_padding + w.x;
      int baseline_y = static_cast<int>(item->y_offset) + item->baseline;
      if (w.vertical_align == VerticalAlign::Super)
        baseline_y -= fset.y_advance(w.size_pct) * 20 / 100;
      else if (w.vertical_align == VerticalAlign::Sub)
        baseline_y += fset.y_advance(w.size_pct) * 20 / 100;
      char text[64];
      int tlen = static_cast<int>(w.len);
      if (tlen > 63)
        tlen = 63;
      std::memcpy(text, w.text, tlen);
      text[tlen] = '\0';
      buf.draw_text_plane(render, x, baseline_y, text, static_cast<size_t>(tlen), fset, plane, white, w.style,
                          w.size_pct);
    }
  }
}

void ReaderScreen::apply_grayscale_(DrawBuffer& buf) {
  const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
  if (!fset || !fset->has_grayscale())
    return;

  // LSB plane â†’ BW RAM (no refresh)
  buf.fill(false);
  render_text_(buf, *fset, GrayPlane::LSB, true, reader_settings_.h_padding());
  buf.write_ram_bw();

  // MSB plane â†’ RED RAM (no refresh)
  buf.fill(false);
  render_text_(buf, *fset, GrayPlane::MSB, true, reader_settings_.h_padding());
  buf.write_ram_red();

  // Trigger grayscale refresh with custom LUT
  buf.grayscale_refresh();
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
  PagePosition next = page_.end;
  // If the page ended mid-image (offset > 0 into an image paragraph or
  // mid-promoted-inline-image), snap back to the start of that paragraph
  // so the next page shows the full image.
  if (next.offset > 0 && chapter_src_ && next.paragraph < chapter_src_->paragraph_count()) {
    if (chapter_src_->paragraph(next.paragraph).type == ParagraphType::Image ||
        layout_engine_.is_mid_promoted_image(next))
      next.offset = 0;
  }
  page_pos_ = next;
  return true;
}

bool ReaderScreen::prev_page_() {
  if (page_pos_ == PagePosition{0, 0}) {
    if (chapter_idx_ > 0) {
      load_chapter_(chapter_idx_ - 1);
      // Jump to the last page of the previous chapter using backward layout.
      const uint16_t end_para = static_cast<uint16_t>(chapter_src_->paragraph_count());
      layout_engine_.set_position(PagePosition{end_para, 0});
      auto pc = layout_engine_.layout_backward();
      page_pos_ = pc.start;
      return true;
    }
    return false;
  }

  // If the current page starts mid-image, snap end to after the image so
  // layout_backward includes the full image on the backward page.
  PagePosition end = page_pos_;
  if (end.offset > 0 && chapter_src_ && end.paragraph < chapter_src_->paragraph_count()) {
    if (chapter_src_->paragraph(end.paragraph).type == ParagraphType::Image ||
        layout_engine_.is_mid_promoted_image(end))
      end = PagePosition{static_cast<uint16_t>(end.paragraph + 1), 0};
  }

  layout_engine_.set_position(end);
  auto pc = layout_engine_.layout_backward();
  page_pos_ = pc.start;
  return true;
}

// ---------------------------------------------------------------------------
// Bookmark persistence
// ---------------------------------------------------------------------------

void ReaderScreen::save_position_() {
  if (pos_path_.empty())
    return;
  const std::string& path = pos_path_;
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f)
    return;
  std::fprintf(f, "%u %u %u %u\n", static_cast<unsigned>(chapter_idx_), static_cast<unsigned>(page_pos_.paragraph),
               static_cast<unsigned>(page_pos_.offset), static_cast<unsigned>(page_pos_.text_offset));
  std::fclose(f);
}

void ReaderScreen::load_position_() {
  if (pos_path_.empty())
    return;
  const std::string& path = pos_path_;
  FILE* f = std::fopen(path.c_str(), "r");
  if (!f)
    return;
  unsigned ch = 0, para = 0, line = 0, to = 0;
  int scanned = std::fscanf(f, "%u %u %u %u", &ch, &para, &line, &to);
  if (scanned >= 3) {
    saved_chapter_idx_ = ch;
    saved_page_pos_ = PagePosition{static_cast<uint16_t>(para), static_cast<uint16_t>(line), static_cast<uint32_t>(to)};
    MR_LOGI("reader", "Loaded pos ch=%u para=%u line=%u to=%u (scanned=%d)", ch, para, line, to, scanned);
  }
  std::fclose(f);
}

}  // namespace microreader
