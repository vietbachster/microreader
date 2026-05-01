#pragma once

#include <cstring>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

// Main screen — lists EPUB books from a directory.
// Button1 = open book, Button0 = settings.
class MainMenu final : public ListMenuScreen {
 public:
  MainMenu() = default;

  void set_books_dir(const char* dir) {
    books_dir_ = dir;
  }

  // Restore the book list selection to the entry matching this path.
  // Call before start(); applied after directory scan.
  void set_initial_selection(const char* path) {
    initial_selection_ = path ? path : "";
  }

  // The full path of the most recently selected (opened) book.
  const std::string& last_selected_book_path() const {
    return last_selected_path_;
  }

  // The full path of the currently highlighted entry (even if not yet opened).
  const std::string& current_book_path() const {
    int idx = selected();
    if (idx >= 0 && idx < static_cast<int>(entries_.size()))
      return entries_[idx].path;
    static const std::string kEmpty;
    return kEmpty;
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

  void set_app(Application* app) {
    app_ = app;
  }

 protected:
  void on_start() override;
  bool on_select(int index) override;
  bool on_back() override;

 private:
  const char* books_dir_ = nullptr;
  std::string initial_selection_;   // path to pre-select after scan
  std::string last_selected_path_;  // path of the most recently opened book

  struct BookEntry {
    std::string path;
    std::string label;
  };
  std::vector<BookEntry> entries_;

  void scan_directory_();
};

}  // namespace microreader
