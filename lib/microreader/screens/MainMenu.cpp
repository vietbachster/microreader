#include "MainMenu.h"

#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <dirent.h>

#include "esp_ota_ops.h"
#include "esp_system.h"
#else
#include <filesystem>
#endif

namespace microreader {

void MainMenu::build_items_() {
  count_ = 0;
  if (book_select_.has_books_dir()) {
    if (count_ < kMaxItems)
      items_[count_++] = {"Select Book", &book_select_, nullptr};
  }
  if (count_ < kMaxItems)
    items_[count_++] = {bouncing_ball_.name(), &bouncing_ball_, nullptr};
  if (count_ < kMaxItems && book_select_.has_books_dir())
    items_[count_++] = {"Clear Converted", nullptr, clear_converted_action_};
#ifdef ESP_PLATFORM
  if (count_ < kMaxItems)
    items_[count_++] = {"Switch OTA", nullptr, ota_action_};
#endif
}

void MainMenu::draw_all_(DrawBuffer& buf) const {
  static const char* kTitle = "Select Demo:";
  const int W = DrawBuffer::kWidth;
  const int H = DrawBuffer::kHeight;
  const int total_h = kLineHeight * count_;
  const int items_y = (H - total_h) / 2;
  const int title_w = static_cast<int>(std::strlen(kTitle)) * kGlyphW;
  const int title_x = (W - title_w) / 2;

  buf.fill(true);
  buf.draw_text(title_x, items_y - kLineHeight, kTitle, true, kScale);

  for (int i = 0; i < count_; ++i) {
    const int label_len = static_cast<int>(std::strlen(items_[i].label));
    const int lx = (W - label_len * kGlyphW) / 2;
    const int ly = items_y + i * kLineHeight;
    // Selected item: black bg / white text (white=false). Others: white bg / black text.
    buf.draw_text(lx, ly, items_[i].label, i != selected_, kScale);
  }
}

void MainMenu::clear_converted_action_(MainMenu& self) {
  const char* dir = self.book_select_.books_dir();
  if (!dir)
    return;
#ifdef ESP_PLATFORM
  DIR* d = opendir(dir);
  if (!d)
    return;
  struct dirent* ent;
  char path[300];
  while ((ent = readdir(d)) != nullptr) {
    size_t len = std::strlen(ent->d_name);
    if (len > 4 && std::strcmp(ent->d_name + len - 4, ".mrb") == 0) {
      std::snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
      std::remove(path);
    }
  }
  closedir(d);
#else
  namespace fs = std::filesystem;
  try {
    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".mrb")
        fs::remove(entry.path());
    }
  } catch (...) {}
#endif
}

}  // namespace microreader

#ifdef ESP_PLATFORM
namespace microreader {

void MainMenu::ota_action_(MainMenu& /*self*/) {
  auto running = esp_ota_get_running_partition();
  auto next = esp_ota_get_next_update_partition(running);
  esp_ota_set_boot_partition(next);
  esp_restart();
}

}  // namespace microreader
#endif

namespace microreader {

void MainMenu::start(DrawBuffer& buf) {
  chosen_ = nullptr;
  build_items_();
  draw_all_(buf);
}

void MainMenu::stop() {}

bool MainMenu::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& /*runtime*/) {
  bool moved = false;

  if (buttons.is_pressed(Button::Button3)) {
    selected_ = selected_ > 0 ? selected_ - 1 : count_ - 1;
    moved = true;
  }
  if (buttons.is_pressed(Button::Button2)) {
    selected_ = selected_ < count_ - 1 ? selected_ + 1 : 0;
    moved = true;
  }

  if (moved) {
    draw_all_(buf);
    buf.refresh();
  }

  if (buttons.is_pressed(Button::Button1) && selected_ < count_) {
    const auto& item = items_[selected_];
    if (item.action) {
      item.action(*this);
      // Redraw after action (e.g. Clear Converted).
      draw_all_(buf);
      buf.refresh();
    } else {
      chosen_ = item.target_screen;
      return false;
    }
  }
  return true;
}

}  // namespace microreader
