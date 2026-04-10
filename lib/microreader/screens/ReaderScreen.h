#pragma once

#include <memory>

#include "../Input.h"
#include "../content/Book.h"
#include "../content/TextLayout.h"
#include "../content/mrb/MrbConverter.h"
#include "../content/mrb/MrbReader.h"
#include "../display/DrawBuffer.h"
#include "../display/Font.h"
#include "IScreen.h"

namespace microreader {

// Simple EPUB page viewer.
// Renders text using the 8×8 bitmap font scaled 2× (16×16 glyphs).
// Button2 = next page, Button3 = prev page, Button0 = back to menu.
class ReaderScreen final : public IScreen {
 public:
  ReaderScreen() = default;
  explicit ReaderScreen(const char* epub_path);

  void set_path(const char* epub_path) {
    path_ = epub_path;
  }
  bool has_path() const {
    return path_ != nullptr;
  }

  const char* name() const override {
    return "Reader";
  }

  void start(DrawBuffer& buf) override;
  void stop() override;
  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 private:
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;
  static constexpr int kPadding = 20;
  static constexpr int kParaSpacing = 12;

  const char* path_ = nullptr;
  std::string mrb_path_;
  DrawBuffer* buf_ = nullptr;  // set in start(), cleared in stop()
  Book book_;
  MrbReader mrb_;
  std::unique_ptr<MrbChapterSource> chapter_src_;
  size_t chapter_idx_ = 0;
  PagePosition page_pos_;
  PageContent page_;
  bool open_ok_ = false;

  // Cached image dimensions: one entry per image ref in the MRB.
  struct ImageDims {
    uint16_t width = 0;
    uint16_t height = 0;
  };
  std::vector<ImageDims> dim_cache_;

  bool resolve_image_size_(uint16_t key, uint16_t& w, uint16_t& h);
  bool decode_image_to_buffer_(uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y, uint16_t max_w,
                               uint16_t max_h);
  void render_page_(DrawBuffer& buf);
  bool next_page_();
  bool prev_page_();
  void load_chapter_(size_t idx);
};

}  // namespace microreader
