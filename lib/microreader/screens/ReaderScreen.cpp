#include "ReaderScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

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
// ReaderScreen — image size resolution
// ---------------------------------------------------------------------------

// resolve_image_size_ removed — image size resolution is now handled by
// make_image_size_query() (MrbReader.h), stored in image_size_fn_.

bool ReaderScreen::decode_image_to_buffer_(uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y, uint16_t max_w,
                                           uint16_t max_h, uint16_t src_y, uint16_t clip_h) {
  StdioZipFile file;
  if (!file.open(path_.c_str()))
    return false;
  ZipEntry entry;
  if (ZipReader::read_local_entry(file, offset, entry) != ZipError::Ok)
    return false;

  // Set up a sink that blits each dithered row directly to the DrawBuffer.
  struct BlitCtx {
    DrawBuffer* buf;
    int x, y;
    uint16_t src_y;
    uint16_t clip_h;  // max rows to render (0 = no clip)
  };
  BlitCtx ctx{&buf, dest_x, dest_y, src_y, clip_h};
  ImageRowSink sink;
  sink.ctx = &ctx;
  sink.emit_row = [](void* c, uint16_t row, const uint8_t* data, uint16_t width) {
    auto* bc = static_cast<BlitCtx*>(c);
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

void ReaderScreen::start(DrawBuffer& buf) {
  buf_ = &buf;
  book_key_.clear();
  pos_path_.clear();

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
    std::string book_cache_dir = std::string(data_dir_) + "/cache/" + std::string(name, name_len);
#ifdef ESP_PLATFORM
    mkdir(book_cache_dir.c_str(), 0775);
#else
    std::filesystem::create_directories(book_cache_dir);
#endif
    mrb_path_ = book_cache_dir + "/book.mrb";
  }

  bool mrb_ok = mrb_.open(mrb_path_.c_str());

  if (!mrb_ok) {
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
  book_key_ = make_book_key(mrb_.metadata(), path_.c_str());
  pos_path_ = std::string(data_dir_) + "/data/" + book_key_ + ".pos";

  open_ok_ = true;
  chapter_idx_ = 0;
  page_pos_ = PagePosition{0, 0};
  saved_chapter_idx_ = 0;
  saved_page_pos_ = PagePosition{0, 0};
  image_size_fn_ = make_image_size_query(mrb_, path_, static_cast<uint16_t>(DrawBuffer::kWidth));
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
  if (open_ok_)
    save_position_();
  image_size_fn_ = {};
  chapter_src_.reset();
  mrb_.close();
  book_.close();
  page_ = PageContent{};
  mrb_path_.clear();
  mrb_path_.shrink_to_fit();
  pos_path_.clear();
  pos_path_.shrink_to_fit();
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
  if (buttons.is_pressed(Button::Button2) || buttons.is_pressed(Button::Up))
    changed = next_page_();
  if (buttons.is_pressed(Button::Button3) || buttons.is_pressed(Button::Down))
    changed = prev_page_();

  if (changed) {
    if (grayscale_active_) {
      buf.revert_grayscale();
      grayscale_active_ = false;
    }
    render_page_(buf);
    buf.refresh();
  }

  // Deferred grayscale: only apply when no nav buttons are held, so rapid
  // page flipping stays fast and grayscale is applied once the user stops.
  if (grayscale_pending_ && !buttons.is_down(Button::Button2) && !buttons.is_down(Button::Button3) &&
      !buttons.is_down(Button::Up) && !buttons.is_down(Button::Down)) {
    grayscale_pending_ = false;
    apply_grayscale_(buf);
    grayscale_active_ = true;
  }

  return true;
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
  IFont& font = fset ? static_cast<IFont&>(const_cast<BitmapFontSet&>(*fset)) : static_cast<IFont&>(fixed_font);
  PageOptions opts(static_cast<uint16_t>(W), static_cast<uint16_t>(H), kPaddingTop, kParaSpacing, Alignment::Start);
  opts.padding_right = kPaddingRight;
  opts.padding_bottom = kPaddingBottom;
  opts.padding_left = kPaddingLeft;
  opts.center_text = true;
  layout_engine_.set_font(font);
  layout_engine_.set_options(opts);
  layout_engine_.set_position(page_pos_);

  page_ = layout_engine_.layout();

  // ── Collect image positions from layout ─────────────────────────────────
  struct ImageToDraw {
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

  // ── BW rendering ────────────────────────────────────────────────────────
  buf.fill(true);

  if (fset) {
    render_text_(buf, *fset, GrayPlane::BW, false);
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
        buf.draw_text_no_bg(kPaddingLeft + w.x, static_cast<int>(item->y_offset), text, false /*black*/, kScale);
      }
    }
  }

  for (const auto& ci : page_.items) {
    const PageHrItem* hr = std::get_if<PageHrItem>(&ci);
    if (!hr)
      continue;
    const int hr_y = static_cast<int>(hr->y_offset) + static_cast<int>(hr->height) / 2;
    buf.fill_rect(static_cast<int>(hr->x_offset), hr_y, static_cast<int>(hr->width), 1, false);
  }

  for (const auto& itd : images) {
    if (!decode_image_to_buffer_(itd.offset, buf, itd.x, itd.y, static_cast<uint16_t>(itd.w),
                                 static_cast<uint16_t>(itd.h), itd.src_y, itd.clip_h)) {
      buf.fill_rect(itd.x, itd.y, itd.w, itd.h, false);
    }
  }

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

void ReaderScreen::render_text_(DrawBuffer& buf, const BitmapFontSet& fset, GrayPlane plane, bool white) {
  uint8_t* render = buf.render_buf();
  for (const auto& ci : page_.items) {
    const PageTextItem* item = std::get_if<PageTextItem>(&ci);
    if (!item)
      continue;
    for (const auto& w : item->line.words) {
      if (w.len == 0)
        continue;
      int x = kPaddingLeft + w.x;
      int baseline_y = static_cast<int>(item->y_offset) + item->baseline;
      if (w.vertical_align == VerticalAlign::Super)
        baseline_y -= fset.y_advance(w.size) * 20 / 100;
      else if (w.vertical_align == VerticalAlign::Sub)
        baseline_y += fset.y_advance(w.size) * 20 / 100;
      char text[64];
      int tlen = static_cast<int>(w.len);
      if (tlen > 63)
        tlen = 63;
      std::memcpy(text, w.text, tlen);
      text[tlen] = '\0';
      buf.draw_text_plane(render, x, baseline_y, text, static_cast<size_t>(tlen), fset, plane, white, w.style, w.size);
    }
  }
}

void ReaderScreen::apply_grayscale_(DrawBuffer& buf) {
  const BitmapFontSet* fset = ext_font_set_ ? ext_font_set_ : (font_set_.valid() ? &font_set_ : nullptr);
  if (!fset || !fset->has_grayscale())
    return;

  // LSB plane → BW RAM (no refresh)
  buf.fill(false);
  render_text_(buf, *fset, GrayPlane::LSB, true);
  buf.write_ram_bw();

  // MSB plane → RED RAM (no refresh)
  buf.fill(false);
  render_text_(buf, *fset, GrayPlane::MSB, true);
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
  std::fprintf(f, "%u %u %u\n", static_cast<unsigned>(chapter_idx_), static_cast<unsigned>(page_pos_.paragraph),
               static_cast<unsigned>(page_pos_.offset));
  std::fclose(f);
}

void ReaderScreen::load_position_() {
  if (pos_path_.empty())
    return;
  const std::string& path = pos_path_;
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
