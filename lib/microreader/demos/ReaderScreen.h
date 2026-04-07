#pragma once

#include <memory>

#include "../Canvas.h"
#include "../DisplayQueue.h"
#include "../Font.h"
#include "../Input.h"
#include "../content/Book.h"
#include "../content/MrbConverter.h"
#include "../content/MrbReader.h"
#include "../content/TextLayout.h"
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

  void start(Canvas& canvas, DisplayQueue& queue) override;
  void stop() override;
  bool update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& runtime) override;

 private:
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;
  static constexpr int kPadding = 20;
  static constexpr int kPaddingTop = 40;
  static constexpr int kParaSpacing = 12;

  const char* path_ = nullptr;
  std::string mrb_path_;
  Book book_;
  MrbReader mrb_;
  std::unique_ptr<MrbChapterSource> chapter_src_;
  size_t chapter_idx_ = 0;
  PagePosition page_pos_;
  PageContent page_;
  int screen_w_ = 480;
  int screen_h_ = 800;
  bool open_ok_ = false;

  // Image size cache: [key] = (w, h), (0,0) = not yet resolved.
  std::vector<std::pair<uint16_t, uint16_t>> img_cache_;

  CanvasText error_label_{0, 0, "", false, kScale};

  bool resolve_image_size_(uint16_t key, uint16_t& w, uint16_t& h);
  void render_page_(DisplayQueue& queue);
  bool next_page_();
  bool prev_page_();
  void load_chapter_(size_t idx);
};

}  // namespace microreader
