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

#include "../HeapLog.h"

namespace microreader {

// ---------------------------------------------------------------------------
// ReaderScreen — image size resolution
// ---------------------------------------------------------------------------

bool ReaderScreen::resolve_image_size_(uint16_t key, uint16_t& w, uint16_t& h) {
  if (key >= static_cast<uint16_t>(img_cache_.size()))
    return false;
  auto& [cw, ch] = img_cache_[key];
  if (cw != 0 || ch != 0) {
    w = cw;
    h = ch;
    return true;
  }
  // book_ may be closed (freed after conversion, or never opened on the MRB-already-exists path).
  // Reopen it lazily for image size lookups.
  if (!book_.is_open()) {
    if (book_.open(path_) != EpubError::Ok)
      return false;
  }
  uint16_t ze = mrb_.image_ref(key).zip_entry_index;
  HEAP_LOG("img_size_before");
  bool ok = book_.read_image_size(ze, cw, ch);
  HEAP_LOG("img_size_after");
  if (!ok)
    return false;
  w = cw;
  h = ch;
  return true;
}

// ---------------------------------------------------------------------------

ReaderScreen::ReaderScreen(const char* epub_path) : path_(epub_path) {}

void ReaderScreen::start(Canvas& /*canvas*/, DisplayQueue& queue) {
  // Build .mrb path from .epub path (sibling file).
  mrb_path_ = path_;
  auto dot = mrb_path_.rfind('.');
  if (dot != std::string::npos)
    mrb_path_ = mrb_path_.substr(0, dot);
  mrb_path_ += ".mrb";

  // Try to open existing MRB first.
  bool mrb_ok = mrb_.open(mrb_path_.c_str());
  bool did_convert = false;

  if (!mrb_ok) {
    // Convert EPUB → MRB.
    HEAP_LOG("before book_.open");
#ifdef ESP_PLATFORM
    int64_t open_start = esp_timer_get_time();
#endif
    auto err = book_.open(path_);
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
    // Reuse display framebuffers as scratch memory during conversion.
    // They are idle until the first render after conversion completes.
    uint8_t* work_buf = queue.scratch_buf1();
    uint8_t* xml_buf = queue.scratch_buf2();
    if (!convert_epub_to_mrb_streaming(book_, mrb_path_.c_str(), work_buf, xml_buf)) {
      open_ok_ = false;
      goto show_error;
    }
#ifdef ESP_PLATFORM
    long conv_ms = (long)((esp_timer_get_time() - conv_start) / 1000);
    long total_ms = (long)((esp_timer_get_time() - open_start) / 1000);
    ESP_LOGI("perf", "Conversion: %ldms  (open+convert=%ldms)", conv_ms, total_ms);
#endif
    HEAP_LOG("after streaming convert");

    // Release EPUB resources — we only need the MRB from now on.
    book_.close();

    // Reinitialize display buffers after scratch use (no hardware refresh yet).
    queue.reset_buffers();
    did_convert = true;

    mrb_ok = mrb_.open(mrb_path_.c_str());
    if (!mrb_ok) {
      open_ok_ = false;
      goto show_error;
    }
  }

  open_ok_ = true;
  chapter_idx_ = 0;
  page_pos_ = PagePosition{0, 0};
  img_cache_.assign(mrb_.image_count(), {0, 0});
  load_chapter_(0);
#ifdef ESP_PLATFORM
  ESP_LOGI("reader", "BOOK_OK: %s", path_);
#endif
  render_page_(queue);
  // After conversion used the display buffers as scratch, do a single
  // half refresh to push the first page to the screen.
  if (did_convert)
    queue.full_refresh(RefreshMode::Half);
  return;

show_error:
#ifdef ESP_PLATFORM
  ESP_LOGE("reader", "BOOK_FAIL: %s", path_);
#endif
  error_label_.set_text("Failed to open book");
  error_label_.set_position(kPadding, kPadding);
  // We'll draw the error in render_page_ path.
  const int W = queue.width();
  const int H = queue.height();
  queue.submit(0, 0, W, H, true);
  queue.submit(kPadding, kPadding, W - 2 * kPadding, kGlyphH * kScale, [this](DisplayFrame& frame) {
    // Draw error text directly.
    const char* msg = "Failed to open book";
    const int n = static_cast<int>(std::strlen(msg));
    const int px = kPadding, py = kPadding;
    for (int i = 0; i < n; ++i) {
      const int idx = static_cast<unsigned char>(msg[i]) - 0x20;
      if (idx < 0 || idx >= detail::kAsciiGlyphCount)
        continue;
      const auto& glyph = detail::kFont8x8[idx];
      const int gx = px + i * kGlyphW * kScale;
      for (int grow = 0; grow < kGlyphH; ++grow) {
        const uint8_t bits = glyph[grow];
        if (bits == 0)
          continue;
        int col = 0;
        while (col < kGlyphW) {
          if (!(bits & (0x80u >> col))) {
            ++col;
            continue;
          }
          const int start = col;
          while (col < kGlyphW && (bits & (0x80u >> col)))
            ++col;
          for (int sr = 0; sr < kScale; ++sr)
            frame.fill_row(py + grow * kScale + sr, gx + start * kScale, gx + col * kScale, false);
        }
      }
    }
  });
  return;
}

void ReaderScreen::stop() {
  // Release all heavy resources so the next book can be opened.
  img_cache_.clear();
  chapter_src_.reset();
  mrb_.close();
  book_.close();
  page_ = PageContent{};
  open_ok_ = false;
}

bool ReaderScreen::update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& /*runtime*/) {
  if (buttons.is_pressed(Button::Button0))
    return false;

  if (!open_ok_)
    return true;

  bool changed = false;
  if (buttons.is_pressed(Button::Button2)) {
    changed = next_page_();
  }
  if (buttons.is_pressed(Button::Button3)) {
    changed = prev_page_();
  }

  if (changed) {
    render_page_(queue);
    canvas.commit(queue);
    // queue.partial_refresh();
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

void ReaderScreen::render_page_(DisplayQueue& queue) {
  const int W = queue.width();
  const int H = queue.height();
  screen_w_ = W;
  screen_h_ = H;

  // Use FixedFont matching our 2×-scaled 8×8 bitmap:
  // glyph_width=16 (8*2), line_height=20 (8*2 + 4 leading).
  FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);
  PageOptions opts(static_cast<uint16_t>(W), static_cast<uint16_t>(H), kPadding, kParaSpacing, Alignment::Start);
  opts.padding_top = kPaddingTop;
  page_ = layout_page(font, opts, *chapter_src_, page_pos_,
                      [this](uint16_t key, uint16_t& w, uint16_t& h) { return resolve_image_size_(key, w, h); });

  // Build a local copy of the text items for the paint lambda.
  // We need: for each word, its x, y_offset, text, len, and glyph advance.
  struct DrawWord {
    int x;
    int y;
    char text[64];
    int len;
    int glyph_advance;  // pixel advance per glyph (accounts for FontSize)
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

  // Draw HRs as thin lines.
  struct DrawHr {
    int x, y, w;
  };
  std::vector<DrawHr> hrs;
  for (const auto& hr : page_.hr_items) {
    hrs.push_back({static_cast<int>(hr.x_offset), static_cast<int>(kPaddingTop + hr.y_offset + page_.vertical_offset),
                   static_cast<int>(hr.width)});
  }

  // Draw images as black boxes.
  struct DrawImg {
    int x, y, w, h;
  };
  std::vector<DrawImg> imgs;
  for (const auto& img : page_.image_items) {
    imgs.push_back({static_cast<int>(img.x_offset),
                    static_cast<int>(kPaddingTop + img.y_offset + page_.vertical_offset), static_cast<int>(img.width),
                    static_cast<int>(img.height)});
  }

  const int scale = kScale;
  queue.submit(
      0, 0, W, H,
      [words = std::move(words), hrs = std::move(hrs), imgs = std::move(imgs), W, H, scale](DisplayFrame& frame) {
        // White background.
        for (int row = 0; row < H; ++row)
          frame.fill_row(row, 0, W, true);

        // Draw each word's glyphs at 2× scale (UTF-8 aware).
        for (const auto& dw : words) {
          const char* p = dw.text;
          const char* end = dw.text + dw.len;
          int ci = 0;
          while (p < end && *p) {
            const int idx = next_glyph_index(p);
            const auto& glyph = detail::kFont8x8[idx];
            const int gx = dw.x + ci * dw.glyph_advance;
            const int gy = dw.y;
            for (int grow = 0; grow < 8; ++grow) {
              const uint8_t bits = glyph[grow];
              if (bits == 0)
                continue;
              int col = 0;
              while (col < 8) {
                if (!(bits & (0x80u >> col))) {
                  ++col;
                  continue;
                }
                const int start = col;
                while (col < 8 && (bits & (0x80u >> col)))
                  ++col;
                for (int sr = 0; sr < scale; ++sr)
                  frame.fill_row(gy + grow * scale + sr, gx + start * scale, gx + col * scale, false);
              }
            }
            ++ci;
          }
        }

        // Draw HRs.
        for (const auto& hr : hrs) {
          frame.fill_row(hr.y, hr.x, hr.x + hr.w, false);
        }

        // Draw images as black rectangles.
        for (const auto& img : imgs) {
          for (int row = img.y; row < img.y + img.h && row < H; ++row)
            frame.fill_row(row, img.x, img.x + img.w, false);
        }
      });
}

bool ReaderScreen::next_page_() {
  if (page_.at_chapter_end) {
    // Move to next chapter.
    if (chapter_idx_ + 1 < mrb_.chapter_count()) {
      load_chapter_(chapter_idx_ + 1);
      page_pos_ = PagePosition{0, 0};
      return true;
    }
    return false;  // Already at last chapter's last page.
  }
  page_pos_ = page_.end;
  return true;
}

bool ReaderScreen::prev_page_() {
  // If at start of chapter, go to previous chapter's last page.
  if (page_pos_ == PagePosition{0, 0}) {
    if (chapter_idx_ > 0) {
      load_chapter_(chapter_idx_ - 1);
      // Layout backward from chapter end to get the last page.
      FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);
      PageOptions opts(static_cast<uint16_t>(screen_w_), static_cast<uint16_t>(screen_h_), kPadding, kParaSpacing,
                       Alignment::Start);
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

  // Layout backward from current page start to get the previous page.
  FixedFont font(kGlyphW * kScale, kGlyphH * kScale + 4);
  PageOptions opts(static_cast<uint16_t>(screen_w_), static_cast<uint16_t>(screen_h_), kPadding, kParaSpacing,
                   Alignment::Start);
  opts.padding_top = kPaddingTop;
  auto pc = layout_page_backward(font, opts, *chapter_src_, page_pos_, [this](uint16_t key, uint16_t& w, uint16_t& h) {
    return resolve_image_size_(key, w, h);
  });
  page_pos_ = pc.start;
  return true;
}

}  // namespace microreader
