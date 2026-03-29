#include "PatternDemo.h"

namespace microreader {

void PatternDemo::start(Canvas& canvas, DisplayQueue& queue) {
  offset_ = 0;
  const int W = queue.width();
  const int H = queue.height();
  const int block = kBlock;
  const int off = offset_;
  queue.submit(0, 0, W, H, [=](DisplayFrame& frame) {
    for (int row = 0; row < H; ++row) {
      const int ty = (row + off) / block;
      int col = 0;
      while (col < W) {
        const int tx = (col + off) / block;
        const bool white = ((tx ^ ty) & 1) == 0;
        const int tile_end = (tx + 1) * block - off;
        const int span_end = tile_end < W ? tile_end : W;
        frame.fill_row(row, col, span_end, white);
        col = span_end;
      }
    }
  });
}

void PatternDemo::stop() {}

bool PatternDemo::update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime&) {
  if (buttons.is_pressed(Button::Button0))
    return false;

  if (buttons.is_pressed(Button::Up))
    paused_ = !paused_;

  if (paused_)
    return true;

  offset_ = (offset_ + 2) % (kBlock * 2);

  const int W = queue.width();
  const int H = queue.height();
  const int block = kBlock;
  const int off = offset_;
  queue.submit(0, 0, W, H, [=](DisplayFrame& frame) {
    for (int row = 0; row < H; ++row) {
      const int ty = (row + off) / block;
      int col = 0;
      while (col < W) {
        const int tx = (col + off) / block;
        const bool white = ((tx ^ ty) & 1) == 0;
        const int tile_end = (tx + 1) * block - off;
        const int span_end = tile_end < W ? tile_end : W;
        frame.fill_row(row, col, span_end, white);
        col = span_end;
      }
    }
  });
  return true;
}

}  // namespace microreader
