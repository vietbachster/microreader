#include "MainMenu.h"

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

void MainMenu::scan_directory_() {
  if (!books_dir_)
    return;

  int book_count = 0;

#ifdef ESP_PLATFORM
  DIR* dir = opendir(books_dir_);
  if (!dir)
    return;
  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr && book_count < kMaxBooks) {
    size_t len = std::strlen(ent->d_name);
    if (len > 5 && len < 220 && std::strcmp(ent->d_name + len - 5, ".epub") == 0) {
      auto& e = entries_[book_count];
      std::snprintf(e.path, sizeof(e.path), "%s/%s", books_dir_, ent->d_name);
      size_t name_len = len - 5;
      if (name_len > kMaxLabelLen)
        name_len = kMaxLabelLen;
      std::memcpy(e.label, ent->d_name, name_len);
      e.label[name_len] = '\0';
      add_item(e.label);
      ++book_count;
    }
  }
  closedir(dir);
#else
  try {
    for (const auto& entry : fs::directory_iterator(books_dir_)) {
      if (book_count >= kMaxBooks)
        break;
      if (!entry.is_regular_file())
        continue;
      auto ext = entry.path().extension().string();
      if (ext != ".epub")
        continue;
      auto& e = entries_[book_count];
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
      add_item(e.label);
      ++book_count;
    }
  } catch (...) {}
#endif
}

void MainMenu::on_start() {
  title_ = "Microreader";
  HEAP_LOG("MainMenu: start enter");
  scan_directory_();
  HEAP_LOG("MainMenu: after scan");
}

bool MainMenu::on_select(int index) {
  reader_.set_path(entries_[index].path);
  chosen_ = &reader_;
  return false;
}

bool MainMenu::on_back() {
  chosen_ = &settings_;
  return false;
}

}  // namespace microreader
