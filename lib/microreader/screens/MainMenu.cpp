#include "MainMenu.h"

#include <algorithm>
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

void MainMenu::build_items_(DisplayQueue& queue) {
  count_ = 0;
  // Book selection (shown first if a books directory has been set).
  if (book_select_.has_books_dir()) {
    if (count_ < kMaxItems)
      items_[count_++] = {"Select Book", &book_select_, nullptr};
  }
  // Demo screens.
  IScreen* screens[] = {&bouncing_ball_};
  for (auto* s : screens) {
    if (count_ < kMaxItems)
      items_[count_++] = {s->name(), s, nullptr};
  }
  // Built-in actions.
  if (count_ < kMaxItems)
    items_[count_++] = {"Rotate Screen", nullptr, rotate_action_};
  if (count_ < kMaxItems) {
    update_phases_label_(queue.phases);
    items_[count_++] = {phases_label_, nullptr, phases_action_};
  }
  if (count_ < kMaxItems && book_select_.has_books_dir())
    items_[count_++] = {"Clear Converted", nullptr, clear_converted_action_};
#ifdef ESP_PLATFORM
  if (count_ < kMaxItems)
    items_[count_++] = {"Switch OTA", nullptr, ota_action_};
#endif
}

void MainMenu::update_phases_label_(int phases) {
  char* p = phases_label_;
  const char* prefix = "Phases: ";
  while (*prefix)
    *p++ = *prefix++;
  if (phases >= 10)
    *p++ = '0' + (phases / 10);
  *p++ = '0' + (phases % 10);
  *p = '\0';
}

void MainMenu::rotate_action_(MainMenu& /*self*/, DisplayQueue& queue) {
  Rotation next = queue.rotation() == Rotation::Deg0 ? Rotation::Deg90 : Rotation::Deg0;
  queue.set_rotation(next);
}

void MainMenu::phases_action_(MainMenu& self, DisplayQueue& queue) {
  int next = queue.phases + 1;
  if (next > kMaxPhases)
    next = kMinPhases;
  queue.phases = next;
  self.update_phases_label_(next);
}

void MainMenu::clear_converted_action_(MainMenu& self, DisplayQueue& /*queue*/) {
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

void MainMenu::ota_action_(MainMenu& /*self*/, DisplayQueue& /*queue*/) {
  auto running = esp_ota_get_running_partition();
  auto next = esp_ota_get_next_update_partition(running);
  esp_ota_set_boot_partition(next);
  esp_restart();
}

}  // namespace microreader
#endif

namespace microreader {

void MainMenu::start(Canvas& canvas, DisplayQueue& queue) {
  chosen_ = nullptr;
  build_items_(queue);

  const int W = queue.width();
  const int H = queue.height();
  const int total_h = kLineHeight * count_;
  const int items_y = (H - total_h) / 2;
  const int title_x = (W - title_.text_width()) / 2;

  queue.submit(0, 0, W, H, /*white=*/true);

  title_.set_position(title_x, items_y - kLineHeight);
  canvas.add(&title_);

  for (int i = 0; i < count_; ++i) {
    const int label_len = static_cast<int>(std::strlen(items_[i].label));
    const int lx = (W - label_len * kGlyphW) / 2;
    labels_[i] = CanvasText(lx, items_y + i * kLineHeight, items_[i].label, /*white=*/false, kScale);
    canvas.add(&labels_[i]);
  }

  update_cursor_(items_y, canvas, queue);
  canvas.commit(queue);
}

void MainMenu::stop() {}

bool MainMenu::update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& /*runtime*/) {
  bool moved = false;

  if (buttons.is_pressed(Button::Button3)) {
    selected_ = selected_ > 0 ? selected_ - 1 : count_ - 1;
    moved = true;
  }
  if (buttons.is_pressed(Button::Button2)) {
    selected_ = selected_ < count_ - 1 ? selected_ + 1 : 0;
    moved = true;
  }

  if (moved)
    update_cursor(canvas, queue);
  if (buttons.is_pressed(Button::Button1) && selected_ < count_) {
    const auto& item = items_[selected_];
    if (item.action) {
      item.action(*this, queue);
      // Re-layout after the action (e.g. rotation may change dimensions).
      stop();
      canvas.clear();
      start(canvas, queue);
      // queue.partial_refresh();
    } else {
      chosen_ = item.target_screen;
      return false;
    }
  }
  return true;
}

void MainMenu::update_cursor(Canvas& canvas, DisplayQueue& queue) {
  const int H = queue.height();
  const int total_h = kLineHeight * count_;
  const int items_y = (H - total_h) / 2;
  update_cursor_(items_y, canvas, queue);
}

void MainMenu::update_cursor_(int /*items_y*/, Canvas& canvas, DisplayQueue& queue) {
  for (int i = 0; i < count_; ++i)
    labels_[i].set_color(i != selected_);
  canvas.commit(queue);
}

}  // namespace microreader
