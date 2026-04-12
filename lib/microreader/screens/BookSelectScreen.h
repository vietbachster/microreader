#pragma once

#include <cstring>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"
#include "ReaderScreen.h"

namespace microreader {

// Book selection screen — lists EPUB files from a directory.
// Button3/Button2 = navigate up/down, Button1 = open, Button0 = go back.
class BookSelectScreen final : public IScreen {
 public:
  static constexpr int kMaxBooks = 16;

  BookSelectScreen() = default;

  void set_books_dir(const char* dir) {
    books_dir_ = dir;
  }
  bool has_books_dir() const {
    return books_dir_ != nullptr;
  }
  const char* books_dir() const {
    return books_dir_;
  }

  const char* name() const override {
    return "Select Book";
  }

  IScreen* chosen() const {
    return chosen_;
  }

  void set_reader_font(const BitmapFontSet* fonts) {
    reader_.set_fonts(fonts);
  }

  ReaderScreen* reader() {
    return &reader_;
  }

  void start(DrawBuffer& buf) override;
  void stop() override;
  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 private:
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8 * kScale;
  static constexpr int kGlyphH = 8 * kScale;
  static constexpr int kLineHeight = kGlyphH + 8;
  static constexpr int kPadding = 16;
  static constexpr int kMaxLabelLen = 220;

  const char* books_dir_ = nullptr;

  struct BookEntry {
    char path[280];
    char label[kMaxLabelLen + 1];
  };
  BookEntry entries_[kMaxBooks] = {};
  int count_ = 0;
  int selected_ = 0;

  IScreen* chosen_ = nullptr;
  ReaderScreen reader_;

  void scan_directory_();
  void draw_all_(DrawBuffer& buf) const;
};

}  // namespace microreader
