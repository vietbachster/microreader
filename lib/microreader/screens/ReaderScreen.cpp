#include "ReaderScreen.h"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
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

  // Let the decoder heap-allocate its own work buffer (~44KB, temporary).
  DecodedImage dims;  // only width/height will be set; data stays empty
  auto err = decode_image_from_entry(file, entry, max_w, max_h, dims, nullptr, 0, /*scale_to_fill=*/true, &sink);
  return err == ImageError::Ok;
}

// ---------------------------------------------------------------------------

ReaderScreen::ReaderScreen(const char* epub_path) : path_(epub_path) {}

void ReaderScreen::start(DrawBuffer& buf) {
  buf_ = &buf;

  // Build .mrb path from .epub path (sibling file).
  mrb_path_ = path_;
  auto dot = mrb_path_.rfind('.');
  if (dot != std::string::npos)
    mrb_path_ = mrb_path_.substr(0, dot);
  mrb_path_ += ".mrb";

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

  open_ok_ = true;
  chapter_idx_ = 0;
  page_pos_ = PagePosition{0, 0};
  dim_cache_.assign(mrb_.image_count(), ImageDims{});
  load_chapter_(0);
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
  buf.draw_text(kPadding, kPadding, "Failed to open book", true, kScale);
}

void ReaderScreen::stop() {
  dim_cache_.clear();
  dim_cache_.shrink_to_fit();
  chapter_src_.reset();
  mrb_.close();
  book_.close();
  page_ = PageContent{};
  mrb_path_.clear();
  mrb_path_.shrink_to_fit();
  open_ok_ = false;
  buf_ = nullptr;
}

bool ReaderScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (buttons.is_pressed(Button::Button0))
    return false;

  if (!open_ok_)
    return true;

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

  FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);
  PageOptions opts(static_cast<uint16_t>(W), static_cast<uint16_t>(H), kPadding, kParaSpacing, Alignment::Start);
  opts.padding_top = kPaddingTop;

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
    itd.y = static_cast<int>(kPaddingTop + img_item.y_offset + page_.vertical_offset);
    itd.w = img_w;
    itd.h = img_h;
    itd.offset = mrb_.image_ref(img_item.key).local_header_offset;
    images.push_back(itd);
  }

  // ── Build word list ─────────────────────────────────────────────────────
  struct DrawWord {
    int x, y, len, glyph_advance;
    char text[64];
  };
  std::vector<DrawWord> words;
  for (const auto& item : page_.text_items) {
    for (const auto& w : item.line.words) {
      DrawWord dw;
      dw.x = kPadding + w.x;
      dw.y = kPaddingTop + item.y_offset + page_.vertical_offset;
      dw.len = static_cast<int>(w.len);
      if (dw.len > 63)
        dw.len = 63;
      std::memcpy(dw.text, w.text, dw.len);
      dw.text[dw.len] = '\0';
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
    buf.draw_text_no_bg(dw.x, dw.y, dw.text, false /*black*/, kScale);
  }

  // Horizontal rules.
  for (const auto& hr : page_.hr_items) {
    buf.fill_rect(static_cast<int>(hr.x_offset), static_cast<int>(kPaddingTop + hr.y_offset + page_.vertical_offset),
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
      FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);
      PageOptions opts(static_cast<uint16_t>(DrawBuffer::kWidth), static_cast<uint16_t>(DrawBuffer::kHeight), kPadding,
                       kParaSpacing, Alignment::Start);
      opts.padding_top = kPaddingTop;
      auto para_count = static_cast<uint16_t>(chapter_src_->paragraph_count());
      auto pc = layout_page_backward(
          font, opts, *chapter_src_, PagePosition{para_count, 0},
          [this](uint16_t key, uint16_t& w, uint16_t& h) { return resolve_image_size_(key, w, h); });
      page_pos_ = pc.start;
      return true;
    }
    return false;
  }

  FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);
  PageOptions opts(static_cast<uint16_t>(DrawBuffer::kWidth), static_cast<uint16_t>(DrawBuffer::kHeight), kPadding,
                   kParaSpacing, Alignment::Start);
  opts.padding_top = kPaddingTop;
  auto pc = layout_page_backward(font, opts, *chapter_src_, page_pos_, [this](uint16_t key, uint16_t& w, uint16_t& h) {
    return resolve_image_size_(key, w, h);
  });
  page_pos_ = pc.start;
  return true;
}

}  // namespace microreader
