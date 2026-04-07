#include "Application.h"

#include <algorithm>
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

void Application::start(DisplayQueue& queue) {
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

  screen_mgr_.push(&menu_, canvas_, queue);
  queue.full_refresh();
}

void Application::auto_open_book(const char* epub_path, DisplayQueue& queue) {
  auto_reader_.set_path(epub_path);
  screen_mgr_.push(&auto_reader_, canvas_, queue);
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, IRuntime& runtime) {
  if (!started_)
    start(queue);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    constexpr int kBlock = 80;
    const int W = queue.width();
    const int H = queue.height();
    queue.submit(0, 0, W, H, [=](DisplayFrame& frame) {
      for (int row = 0; row < H; ++row) {
        const int tile_y = row / kBlock;
        int col = 0;
        while (col < W) {
          const int tile_x = col / kBlock;
          const bool white = ((tile_x ^ tile_y) & 1) == 0;
          const int tile_end = (tile_x + 1) * kBlock;
          const int span_end = tile_end < W ? tile_end : W;
          frame.fill_row(row, col, span_end, white);
          col = span_end;
        }
      }
    });
    queue.full_refresh(RefreshMode::Full);
    queue.display_deep_sleep();
    running_ = false;
    return;
  }

  IScreen* top = screen_mgr_.top();
  if (top) {
    if (!top->update(buttons_, canvas_, queue, runtime)) {
      // Screen signalled exit.
      if (top == &menu_) {
        // Menu chose a screen — push it onto the stack.
        IScreen* chosen = menu_.chosen();
        if (chosen)
          screen_mgr_.push(chosen, canvas_, queue);
      } else {
        // Check if the exiting screen selected a sub-screen to push.
        IScreen* next = nullptr;
        auto* book_sel = menu_.book_select();
        if (top == book_sel)
          next = book_sel->chosen();

        if (next) {
          // Push the sub-screen (e.g. BookSelect → Reader).
          screen_mgr_.push(next, canvas_, queue);
        } else {
          // Pop back to the previous screen.
          screen_mgr_.pop(canvas_, queue);
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
