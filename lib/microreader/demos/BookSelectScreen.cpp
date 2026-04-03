#include "BookSelectScreen.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
// Desktop: use C++17 filesystem for directory scanning.
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
      // Build display label: strip .epub, truncate to fit.
      size_t name_len = len - 5;
      if (name_len > kMaxLabelLen)
        name_len = kMaxLabelLen;
      std::memcpy(e.label, ent->d_name, name_len);
      e.label[name_len] = '\0';
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
      auto stem = entry.path().stem().string();
      size_t name_len = stem.size();
      if (name_len > kMaxLabelLen)
        name_len = kMaxLabelLen;
      std::memcpy(e.label, stem.c_str(), name_len);
      e.label[name_len] = '\0';
      ++count_;
    }
  } catch (...) {}
#endif
}

void BookSelectScreen::start(Canvas& canvas, DisplayQueue& queue) {
  chosen_ = nullptr;
  selected_ = 0;
  scan_directory_();

  const int W = queue.width();
  const int H = queue.height();
  queue.submit(0, 0, W, H, /*white=*/true);

  // Title.
  title_.set_text(count_ > 0 ? "Select Book:" : "No books found");
  title_.set_position(kPadding, kPadding);
  canvas.add(&title_);

  // List labels.
  const int list_y = kPadding + kLineHeight + 4;
  for (int i = 0; i < count_; ++i) {
    labels_[i] = CanvasText(kPadding, list_y + i * kLineHeight, entries_[i].label, i != selected_, kScale);
    canvas.add(&labels_[i]);
  }

  canvas.commit(queue);
}

void BookSelectScreen::stop() {}

bool BookSelectScreen::update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& /*runtime*/) {
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
    update_cursor_();
    canvas.commit(queue);
  }

  if (buttons.is_pressed(Button::Button1) && selected_ < count_) {
    reader_.set_path(entries_[selected_].path);
    chosen_ = &reader_;
    return false;
  }

  return true;
}

void BookSelectScreen::update_cursor_() {
  for (int i = 0; i < count_; ++i)
    labels_[i].set_color(i != selected_);
}

}  // namespace microreader
