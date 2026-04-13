#pragma once

#include <memory>

#include "../Input.h"
#include "../content/BitmapFont.h"
#include "../content/Book.h"
#include "../content/TextLayout.h"
#include "../content/mrb/MrbConverter.h"
#include "../content/mrb/MrbReader.h"
#include "../display/DrawBuffer.h"
#include "ChapterSelectScreen.h"
#include "IScreen.h"

namespace microreader {

// Simple EPUB page viewer.
// Renders text using the 8×8 bitmap font scaled 2× (16×16 glyphs).
// Button2 = next page, Button3 = prev page, Button0 = back to menu.
// Button1 = open chapter list (if TOC available).
class ReaderScreen final : public IScreen {
 public:
  // Get the current book path
  std::string get_path() {
    return path_;
  }
  ReaderScreen() = default;
  explicit ReaderScreen(std::string epub_path) : path_(std::move(epub_path)) {}

  void set_path(std::string epub_path) {
    path_ = std::move(epub_path);
  }
  bool has_path() const {
    return !path_.empty();
  }
  void set_data_dir(std::string dir) {
    data_dir_ = std::move(dir);
  }

  // Set the proportional bitmap font for rendering. If null, falls back to
  // the builtin 8×8 bitmap font at 2× scale. The font data must outlive
  // this screen.
  void set_font(const BitmapFont* font) {
    font_set_.set(FontSize::Normal, font);
  }

  // Set the full font set (Small/Normal/Large). Font data must outlive this screen.
  void set_fonts(const BitmapFontSet* fonts) {
    ext_font_set_ = fonts;
  }

  const char* name() const override {
    return "Reader";
  }

  // Returns the chapter select screen to push (set when Button1 pressed), or null.
  IScreen* chosen() const {
    return nav_chosen_;
  }

  void start(DrawBuffer& buf) override;
  void stop() override;
  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 private:
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;
  static constexpr int kPaddingTop = 6;
  static constexpr int kPaddingRight = 12;
  static constexpr int kPaddingBottom = 14;  // extra space for progress indicator
  static constexpr int kPaddingLeft = 12;
  static constexpr int kParaSpacing = 12;

  BitmapFontSet font_set_;                       // owned set (for single-font set_font() path)
  const BitmapFontSet* ext_font_set_ = nullptr;  // external set (from set_fonts())
  std::string path_;
  std::string data_dir_;
  std::string mrb_path_;
  std::string book_key_;       // sanitized title, used for .mrb/.pos naming
  DrawBuffer* buf_ = nullptr;  // set in start(), cleared in stop()
  Book book_;
  MrbReader mrb_;
  std::unique_ptr<MrbChapterSource> chapter_src_;
  size_t chapter_idx_ = 0;
  PagePosition page_pos_;
  PageContent page_;
  bool open_ok_ = false;

  // Chapter select screen — owned here, pushed when user presses Button1.
  ChapterSelectScreen chapter_select_;
  IScreen* nav_chosen_ = nullptr;

  // Saved position (survives stop()) so we can restore after chapter select cancel.
  size_t saved_chapter_idx_ = 0;
  PagePosition saved_page_pos_;

  // Cached image dimensions: one entry per image ref in the MRB.
  struct ImageDims {
    uint16_t width = 0;
    uint16_t height = 0;
  };
  std::vector<ImageDims> dim_cache_;
  bool grayscale_pending_ = false;
  bool grayscale_active_ = false;

  bool resolve_image_size_(uint16_t key, uint16_t& w, uint16_t& h);
  bool decode_image_to_buffer_(uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y, uint16_t max_w,
                               uint16_t max_h);
  // Render page content (BW only). Sets grayscale_pending_ if font has grayscale.
  void render_page_(DrawBuffer& buf);
  // Deferred grayscale pass: writes LSB/MSB planes to BW/RED RAM and triggers
  // grayscale LUT refresh. Called from update() after BW refresh is committed.
  void apply_grayscale_(DrawBuffer& buf);
  void render_text_(DrawBuffer& buf, const BitmapFontSet& fset, GrayPlane plane, bool white);
  bool next_page_();
  bool prev_page_();
  void load_chapter_(size_t idx);
  void save_position_();
  void load_position_();
};

}  // namespace microreader
