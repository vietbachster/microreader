#pragma once

#include <array>

#include "../Input.h"
#include "../display/Canvas.h"
#include "../display/Display.h"
#include "../display/DisplayQueue.h"
#include "BookSelectScreen.h"
#include "BouncingBallDemo.h"
#include "IScreen.h"

namespace microreader {

// Simple text menu for selecting between screens and toggling options.
// Button3/Button2 to navigate up/down, Button1 to select.
// Owns its demo screens and actions internally.
class MainMenu final : public IScreen {
 public:
  static constexpr int kMaxItems = 10;

  MainMenu() = default;

  // Set the directory to scan for books (call before start).
  void set_books_dir(const char* dir) {
    book_select_.set_books_dir(dir);
  }

  const char* name() const override {
    return "Menu";
  }

  // Returns the screen the user selected.
  IScreen* chosen() const {
    return chosen_;
  }

  // Access the book selection screen (for Application to handle sub-navigation).
  BookSelectScreen* book_select() {
    return &book_select_;
  }

  void start(Canvas& canvas, DisplayQueue& queue) override;
  void stop() override;
  bool update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& runtime) override;

 private:
  // Layout constants.
  static constexpr int kScale = 2;
  static constexpr int kGlyphH = CanvasText::kGlyphH * kScale;
  static constexpr int kGlyphW = CanvasText::kGlyphW * kScale;
  static constexpr int kLineHeight = kGlyphH + 8;

  // Phases action limits.
  static constexpr int kMinPhases = 2;
  static constexpr int kMaxPhases = 12;

  // Demo screens owned by the menu.
  BouncingBallDemo bouncing_ball_;
  BookSelectScreen book_select_;

  // Full item list (screens + built-in actions), rebuilt in build_items_().
  struct Item {
    const char* label = nullptr;
    IScreen* target_screen = nullptr;
    void (*action)(MainMenu& self, DisplayQueue& queue) = nullptr;
  };
  Item items_[kMaxItems] = {};
  int count_ = 0;
  int selected_ = 0;
  IScreen* chosen_ = nullptr;

  // Mutable label for the phases action.
  char phases_label_[16] = {};

  CanvasText title_{0, 0, "Select Demo:", true, kScale};
  std::array<CanvasText, kMaxItems> labels_;

  void build_items_(DisplayQueue& queue);
  void update_phases_label_(int phases);
  static void rotate_action_(MainMenu& self, DisplayQueue& queue);
  static void phases_action_(MainMenu& self, DisplayQueue& queue);
  static void clear_converted_action_(MainMenu& self, DisplayQueue& queue);
#ifdef ESP_PLATFORM
  static void ota_action_(MainMenu& self, DisplayQueue& queue);
#endif

  void update_cursor(Canvas& canvas, DisplayQueue& queue);
  void update_cursor_(int items_y, Canvas& canvas, DisplayQueue& queue);
};

}  // namespace microreader
