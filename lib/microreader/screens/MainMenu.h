#pragma once

#include <cstring>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"
#include "ReaderScreen.h"
#include "SettingsScreen.h"

namespace microreader {

// Main screen — lists EPUB books from a directory.
// Button1 = open book, Button0 = settings.
class MainMenu final : public ListMenuScreen {
 public:
  static constexpr int kMaxBooks = 16;

  MainMenu() = default;

  void set_books_dir(const char* dir) {
    books_dir_ = dir;
  }
  void set_data_dir(const char* dir) {
    reader_.set_data_dir(dir);
    settings_.set_data_dir(dir);
  }

  bool has_books_dir() const {
    return books_dir_ != nullptr;
  }
  const char* books_dir() const {
    return books_dir_;
  }

  const char* name() const override {
    return "Books";
  }

  void set_reader_font(const BitmapFontSet* fonts) {
    reader_.set_fonts(fonts);
  }

  ReaderScreen* reader() {
    return &reader_;
  }
  SettingsScreen* settings() {
    return &settings_;
  }

 protected:
  void on_start() override;
  bool on_select(int index) override;
  bool on_back() override;

 private:
  static constexpr int kMaxLabelLen = 220;

  const char* books_dir_ = nullptr;

  struct BookEntry {
    char path[280];
    char label[kMaxLabelLen + 1];
  };
  BookEntry entries_[kMaxBooks] = {};

  ReaderScreen reader_;
  SettingsScreen settings_;

  void scan_directory_();
};

}  // namespace microreader
