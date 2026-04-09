#pragma once

#include <array>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "BookSelectScreen.h"
#include "BouncingBallDemo.h"
#include "IScreen.h"

namespace microreader {

// Simple text menu for selecting between screens and triggering actions.
// Button3/Button2 = navigate up/down, Button1 = select.
class MainMenu final : public IScreen {
 public:
  static constexpr int kMaxItems = 8;

  MainMenu() = default;

  void set_books_dir(const char* dir) {
    book_select_.set_books_dir(dir);
  }

  const char* name() const override {
    return "Menu";
  }

  IScreen* chosen() const {
    return chosen_;
  }

  BookSelectScreen* book_select() {
    return &book_select_;
  }

  void start(DrawBuffer& buf) override;
  void stop() override;
  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 private:
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8 * kScale;
  static constexpr int kGlyphH = 8 * kScale;
  static constexpr int kLineHeight = kGlyphH + 8;

  BouncingBallDemo bouncing_ball_;
  BookSelectScreen book_select_;

  struct Item {
    const char* label = nullptr;
    IScreen* target_screen = nullptr;
    void (*action)(MainMenu& self) = nullptr;
  };
  Item items_[kMaxItems] = {};
  int count_ = 0;
  int selected_ = 0;
  IScreen* chosen_ = nullptr;

  void build_items_();
  void draw_all_(DrawBuffer& buf) const;

  static void clear_converted_action_(MainMenu& self);
#ifdef ESP_PLATFORM
  static void ota_action_(MainMenu& self);
#endif
};

}  // namespace microreader
