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

void MainMenu::on_start() {
  title_ = "Microreader";
  HEAP_LOG("MainMenu: start enter");
  scan_directory_();
  HEAP_LOG("MainMenu: after scan");
  // Restore previously selected book position.
  if (!initial_selection_.empty()) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (entries_[i].path == initial_selection_) {
        set_selected(i);
        break;
      }
    }
  }
}

bool MainMenu::on_select(int index) {
  last_selected_path_ = entries_[index].path;
  reader_.set_path(entries_[index].path.c_str());
  chosen_ = &reader_;
  return false;
}

bool MainMenu::on_back() {
  chosen_ = &settings_;
  return false;
}

void MainMenu::scan_directory_() {
  if (!books_dir_)
    return;

  clear_items();
  entries_.clear();

  auto add_book = [this](std::string path, std::string label) {
    BookEntry e;
    e.path = std::move(path);
    e.label = std::move(label);
    entries_.push_back(std::move(e));
    add_item(entries_.back().label);
  };

#ifdef ESP_PLATFORM
  DIR* dir = opendir(books_dir_);
  if (!dir)
    return;
  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    size_t len = std::strlen(ent->d_name);
    if (len > 5 && len < 220 && std::strcmp(ent->d_name + len - 5, ".epub") == 0) {
      char fullpath[512];
      std::snprintf(fullpath, sizeof(fullpath), "%s/%s", books_dir_, ent->d_name);
      add_book(fullpath, std::string(ent->d_name, len - 5));
    }
  }
  closedir(dir);
#else
  try {
    for (const auto& entry : fs::directory_iterator(books_dir_)) {
      if (!entry.is_regular_file())
        continue;
      auto ext = entry.path().extension().string();
      if (ext != ".epub")
        continue;
      add_book(entry.path().string(), entry.path().stem().string());
    }
  } catch (...) {}
#endif
}

}  // namespace microreader
