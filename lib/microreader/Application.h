#pragma once

#include <cstdint>
#include <functional>

#include "Input.h"
#include "Runtime.h"
#include "ScreenManager.h"
#include "display/DrawBuffer.h"
#include "screens/IScreen.h"
#include "screens/MainMenu.h"

namespace microreader {

class Application {
 public:
  Application() = default;

  const char* build_info() const;
  void set_books_dir(const char* dir) {
    menu_.set_books_dir(dir);
  }
  void set_data_dir(const char* dir) {
    menu_.set_data_dir(dir);
    data_dir_ = dir;
  }
  // Path to data directory for settings/state persistence
  const char* data_dir_ = nullptr;
  // Path to the single unified settings file (cached after set_data_dir)
  std::string settings_path_;
  // Book path to auto-open on next start() (set by load_settings_)
  std::string pending_book_path_;
  // Save all persistent state to the settings file
  void save_settings_();
  // Load all persistent state from the settings file
  void load_settings_();

  // Set the proportional font set for the reader screen. Must outlive the app.
  // Also propagates immediately to the ReaderScreen so it takes effect even if
  // called after start() (e.g. from within the font-provisioning hook).
  void set_reader_font(const BitmapFontSet* fonts) {
    reader_font_ = fonts;
    menu_.set_reader_font(fonts);
  }
  // Set an optional hook fired at the start of every book open (before file I/O).
  // Used on ESP32 to lazily provision the font partition from firmware-embedded
  // compressed data.  Has no effect on desktop (hook is never set).
  void set_pre_book_open_hook(std::function<void()> hook) {
    menu_.reader()->set_pre_open_hook(std::move(hook));
  }
  // Set an optional callback for "Invalidate Font" in the Settings menu.
  // When set, the item is shown (ESP32 only). Should zero the partition CRC.
  void set_invalidate_font_fn(std::function<void()> fn) {
    menu_.settings()->set_invalidate_font_fn(std::move(fn));
  }
  void start(DrawBuffer& buf);
  // Auto-open a book by path (skips menu, for debugging).
  void auto_open_book(const char* epub_path, DrawBuffer& buf);
  void update(const ButtonState& buttons, uint32_t dt_ms, DrawBuffer& buf, IRuntime& runtime);
  bool running() const;
  uint64_t tick_count() const;
  uint32_t uptime_ms() const;

 private:
  ButtonState buttons_{};
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;
  bool started_ = false;
  bool running_ = true;

  ScreenManager screen_mgr_;
  MainMenu menu_;
  const BitmapFontSet* reader_font_ = nullptr;
};

}  // namespace microreader
