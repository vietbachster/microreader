#pragma once

#include <cstring>

#include "../Input.h"
#include "../display/Canvas.h"
#include "../display/DisplayQueue.h"
#include "IScreen.h"
#include "ReaderScreen.h"

namespace microreader {

// Book selection screen — lists EPUB files from a directory.
// Button3/Button2 to navigate up/down, Button1 to open, Button0 to go back.
class BookSelectScreen final : public IScreen {
 public:
  static constexpr int kMaxBooks = 16;

  BookSelectScreen() = default;

  // Set the directory to scan for .epub files (call before start).
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

  // After returning false, this is the screen to push next (or nullptr).
  IScreen* chosen() const {
    return chosen_;
  }

  void start(Canvas& canvas, DisplayQueue& queue) override;
  void stop() override;
  bool update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& runtime) override;

 private:
  static constexpr int kScale = 2;
  static constexpr int kGlyphH = CanvasText::kGlyphH * kScale;
  static constexpr int kGlyphW = CanvasText::kGlyphW * kScale;
  static constexpr int kLineHeight = kGlyphH + 8;
  static constexpr int kPadding = 16;

  // Max displayable characters per label (filename truncated to fit screen).
  static constexpr int kMaxLabelLen = 28;

  const char* books_dir_ = nullptr;

  // Book entries: store full path + display label.
  struct BookEntry {
    char path[280];
    char label[kMaxLabelLen + 1];
  };
  BookEntry entries_[kMaxBooks] = {};
  int count_ = 0;
  int selected_ = 0;

  IScreen* chosen_ = nullptr;
  ReaderScreen reader_;

  CanvasText title_{0, 0, "Select Book:", true, kScale};
  CanvasText labels_[kMaxBooks];

  void scan_directory_();
  void update_cursor_();
};

}  // namespace microreader
