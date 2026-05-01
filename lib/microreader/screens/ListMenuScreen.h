#pragma once

#include <cstdint>
#include <vector>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"

namespace microreader {

// Base class for screens that show a titled list of selectable items.
// Handles drawing (header font for title, UI font for items with selection bar),
// up/down navigation with wrapping, scrolling for long lists, and font
// initialization from embedded data.
//
// Subclasses implement:
//   on_start()      — set title, populate items via add_item()
//   on_select(index) — handle item selection; return true to stay, false to exit
//   on_back()       — handle back button; return true to stay, false to exit (default)
class ListMenuScreen : public IScreen {
 public:
  void start(DrawBuffer& buf) override;
  void stop() override {}
  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 protected:
  const char* title_ = nullptr;

  void add_item(const std::string& label) {
    labels_.push_back(label);
    separators_.push_back(false);
  }
  // Insert a visual separator (thin horizontal line, non-selectable).
  void add_separator() {
    labels_.push_back("");
    separators_.push_back(true);
  }
  void clear_items() {
    labels_.clear();
    separators_.clear();
    selected_ = 0;
    scroll_offset_ = 0;
  }
  int selected() const {
    return selected_;
  }
  void set_selected(int index) {
    selected_ = index;
    on_start_set_selection_ = true;
  }
  int count() const {
    return static_cast<int>(labels_.size());
  }

  // Called during start(). Set title_ and call add_item() to populate the list.
  virtual void on_start() = 0;

  // Called when user presses select on an item.
  // Return true to stay on this screen (e.g. action performed inline).
  // Return false to exit (set chosen_ to navigate to a sub-screen).
  virtual bool on_select(int index) = 0;

  // Called when user presses back. Return false to exit (default), true to stay.
  virtual bool on_back() {
    return false;
  }

 private:
  std::vector<std::string> labels_;
  std::vector<bool> separators_;
  int selected_ = 0;
  int scroll_offset_ = 0;
  bool on_start_set_selection_ = false;
  // Hold-down acceleration counters (frames button has been held without a fresh press).
  int hold_frames_up_ = 0;
  int hold_frames_down_ = 0;

  BitmapFont ui_font_;
  BitmapFont header_font_;

  void draw_all_(DrawBuffer& buf) const;
  void ensure_visible_();
};

}  // namespace microreader
