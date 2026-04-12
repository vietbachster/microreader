#include "Application.h"

#include <cstdlib>
#include <ctime>

#include "HeapLog.h"

#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

namespace microreader {

const char* Application::build_info() const {
  return "microreader";
}

void Application::start(DrawBuffer& buf) {
  ticks_ = 0;
  uptime_ms_ = 0;
  buttons_ = ButtonState{};
  started_ = true;
  running_ = true;
  MR_LOGI("app", "%s", build_info());
#ifdef ESP_PLATFORM
  std::srand(esp_random());
#else
  std::srand(static_cast<unsigned>(std::time(nullptr)));
#endif

  if (reader_font_)
    menu_.set_reader_font(reader_font_);

  screen_mgr_.push(&menu_, buf);
  buf.full_refresh();
}

void Application::auto_open_book(const char* epub_path, DrawBuffer& buf) {
  auto_reader_.set_path(epub_path);
  if (reader_font_)
    auto_reader_.set_fonts(reader_font_);
  screen_mgr_.push(&auto_reader_, buf);
  buf.refresh();
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DrawBuffer& buf, IRuntime& runtime) {
  if (!started_)
    start(buf);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    // Draw checkerboard pattern then full refresh before deep sleep.
    constexpr int kBlock = 80;
    const int W = DrawBuffer::kWidth;
    const int H = DrawBuffer::kHeight;
    buf.fill(true);
    for (int row = 0; row < H; ++row) {
      const int tile_y = row / kBlock;
      int col = 0;
      while (col < W) {
        const int tile_x = col / kBlock;
        const bool white = ((tile_x ^ tile_y) & 1) == 0;
        const int tile_end = (tile_x + 1) * kBlock;
        const int span_end = tile_end < W ? tile_end : W;
        buf.fill_row(row, col, span_end, white);
        col = span_end;
      }
    }
    buf.full_refresh(RefreshMode::Full);
    buf.deep_sleep();
    running_ = false;
    return;
  }

  IScreen* top = screen_mgr_.top();
  if (top) {
    if (!top->update(buttons_, buf, runtime)) {
      // Screen signalled exit.
      if (top == &menu_) {
        // Menu chose a sub-screen — push it.
        IScreen* chosen = menu_.chosen();
        if (chosen) {
          screen_mgr_.push(chosen, buf);
          buf.refresh();
        }
      } else {
        // Check if the exiting screen selected a sub-screen to push.
        IScreen* next = nullptr;
        auto* settings = menu_.settings();
        auto* reader = menu_.reader();
        if (top == settings)
          next = settings->chosen();
        else if (top == reader)
          next = reader->chosen();

        if (next) {
          screen_mgr_.push(next, buf);
          buf.refresh();
        } else {
          // Pop back to the previous screen.
          screen_mgr_.pop(buf);
          buf.refresh();
        }
      }
    }
  }
}

bool Application::running() const {
  return running_;
}
uint64_t Application::tick_count() const {
  return ticks_;
}
uint32_t Application::uptime_ms() const {
  return uptime_ms_;
}

}  // namespace microreader
