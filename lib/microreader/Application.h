#pragma once

#include <cstdint>

#include "Canvas.h"
#include "Display.h"
#include "DisplayQueue.h"
#include "Input.h"
#include "Log.h"
#include "Runtime.h"
#include "ScreenManager.h"
#include "demos/IScreen.h"
#include "demos/MenuDemo.h"
#include "demos/ReaderScreen.h"

namespace microreader {

class Application {
 public:
  Application() = default;

  const char* build_info() const;
  void set_books_dir(const char* dir) {
    menu_.set_books_dir(dir);
  }
  void start(ILogger& logger, DisplayQueue& queue);
  // Auto-open a book by path (skips menu, for debugging).
  void auto_open_book(const char* epub_path, DisplayQueue& queue);
  void update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, ILogger& logger, IRuntime& runtime);
  bool running() const;
  uint64_t tick_count() const;
  uint32_t uptime_ms() const;

 private:
  ButtonState buttons_{};
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;
  bool started_ = false;
  bool running_ = true;

  Canvas canvas_;
  ScreenManager screen_mgr_;
  MenuDemo menu_;
  ReaderScreen auto_reader_;  // used by auto_open_book
};

}  // namespace microreader
