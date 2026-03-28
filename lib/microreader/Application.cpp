#include "Application.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif

namespace microreader {

const char* Application::build_info() const {
  return "microreader";
}

void Application::start(ILogger& logger, DisplayQueue& queue) {
  ticks_ = 0;
  uptime_ms_ = 0;
  buttons_ = ButtonState{};
  dirty_ = true;
  started_ = true;
  running_ = true;
  logger.log(LogLevel::Info, build_info());
#ifdef ESP_PLATFORM
  std::srand(esp_random());
#else
  std::srand(static_cast<unsigned>(std::time(nullptr)));
#endif

  // lut_calibration_.start(queue);
  demo_.start(canvas_, queue);
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, ILogger& logger,
                         IRuntime& runtime) {
  if (!started_)
    start(logger, queue);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

  if (buttons_.is_pressed(Button::Power)) {
    // Paint a checkerboard pattern (~80px squares) as the sleep screen.
    constexpr int kBlock = 80;
    constexpr int W = DisplayFrame::kPhysicalWidth;
    constexpr int H = DisplayFrame::kPhysicalHeight;
    queue.submit(0, 0, W, H, [=](uint8_t* buf) {
      for (int row = 0; row < H; ++row) {
        const int tile_y = row / kBlock;
        uint8_t* rp = buf + row * DisplayFrame::kStride;
        int col = 0;
        while (col < W) {
          const int tile_x = col / kBlock;
          const bool white = ((tile_x ^ tile_y) & 1) == 0;
          const int tile_end = (tile_x + 1) * kBlock;
          const int span_end = tile_end < W ? tile_end : W;
          DisplayQueue::fill_row(buf, row, col, span_end, white);
          col = span_end;
        }
      }
    });
    queue.full_refresh(RefreshMode::Full);
    queue.display_deep_sleep();
    running_ = false;
    return;
  }

  // lut_calibration_.update(buttons_, queue, runtime);
  demo_.update(buttons_, canvas_, queue);
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
