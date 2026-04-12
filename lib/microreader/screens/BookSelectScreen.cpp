#include "BookSelectScreen.h"

#include <cstdio>
#include <cstring>

#include "../HeapLog.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

void BookSelectScreen::scan_directory_() {
  count_ = 0;
  if (!books_dir_)
    return;

#ifdef ESP_PLATFORM
  DIR* dir = opendir(books_dir_);
  if (!dir)
    return;
  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr && count_ < kMaxBooks) {
    size_t len = std::strlen(ent->d_name);
    if (len > 5 && len < 220 && std::strcmp(ent->d_name + len - 5, ".epub") == 0) {
      auto& e = entries_[count_];
      std::snprintf(e.path, sizeof(e.path), "%s/%s", books_dir_, ent->d_name);
      e.label.assign(ent->d_name, len - 5);
      ++count_;
    }
  }
  closedir(dir);
#else
  try {
    for (const auto& entry : fs::directory_iterator(books_dir_)) {
      if (count_ >= kMaxBooks)
        break;
      if (!entry.is_regular_file())
        continue;
      auto ext = entry.path().extension().string();
      if (ext != ".epub")
        continue;
      auto& e = entries_[count_];
      auto path_str = entry.path().string();
      if (path_str.size() >= sizeof(e.path))
        continue;
      std::memcpy(e.path, path_str.c_str(), path_str.size() + 1);
      e.label = entry.path().stem().string();
      ++count_;
    }
  } catch (...) {}
#endif
}

void BookSelectScreen::draw_all_(DrawBuffer& buf) const {
  const int W = DrawBuffer::kWidth;
  buf.fill(true);
  const char* title = count_ > 0 ? "Select Book:" : "No books found";
  buf.draw_text(kPadding, kPadding, title, true, kScale);
  const int list_y = kPadding + kLineHeight + 4;
  for (int i = 0; i < count_; ++i) {
    buf.draw_text(kPadding, list_y + i * kLineHeight, entries_[i].label.c_str(), i != selected_, kScale);
  }
  (void)W;
}

void BookSelectScreen::start(DrawBuffer& buf) {
  HEAP_LOG("BookSelect: start enter");
  chosen_ = nullptr;
  scan_directory_();
  HEAP_LOG("BookSelect: after scan");
  draw_all_(buf);
}

void BookSelectScreen::stop() {}

bool BookSelectScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  if (buttons.is_pressed(Button::Button0))
    return false;

  if (count_ == 0)
    return true;

  bool moved = false;
  if (buttons.is_pressed(Button::Button3)) {
    if (selected_ > 0) {
      --selected_;
      moved = true;
    }
  }
  if (buttons.is_pressed(Button::Button2)) {
    if (selected_ < count_ - 1) {
      ++selected_;
      moved = true;
    }
  }

  if (moved) {
    draw_all_(buf);
    buf.refresh();
  }

  if (buttons.is_pressed(Button::Button1) && selected_ < count_) {
    reader_.set_path(entries_[selected_].path);
    chosen_ = &reader_;
    return false;
  }

  return true;
}

}  // namespace microreader
