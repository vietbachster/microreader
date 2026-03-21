#pragma once

#include "DisplayQueue.h"

namespace microreader {

// Base class for canvas elements that manage their own erase/draw lifecycle
// against a DisplayQueue.
//
// The element tracks whether its state has changed since the last commit().
// Calling commit() emits controller submissions only when something differs:
//   1. Erase the previously committed region (white fill).
//   2. Draw the new content (via draw_content()).
// If nothing changed, commit() is a no-op.
class CanvasElement {
 public:
  virtual ~CanvasElement() = default;

  void set_visible(bool v) {
    if (v == visible_)
      return;
    visible_ = v;
    dirty_ = true;
  }
  bool visible() const {
    return visible_;
  }

  // Submit erase/draw commands only if state has changed since the last
  // commit(). No-op otherwise.
  void commit(DisplayQueue& queue) {
    if (!dirty_)
      return;
    dirty_ = false;

    // Erase the previously committed area with white.
    if (committed_valid_)
      queue.submit(cx_, cy_, cw_, ch_, /*white=*/true);

    if (visible_) {
      draw_content(queue);
      current_bounds(cx_, cy_, cw_, ch_);
      committed_valid_ = (cw_ > 0 && ch_ > 0);
    } else {
      committed_valid_ = false;
    }
  }

 protected:
  void mark_dirty() {
    dirty_ = true;
  }

  // Submit draw commands for the current element state.
  virtual void draw_content(DisplayQueue& queue) const = 0;

  // Report the bounding box of the current state. Must match what
  // draw_content() covers so the next commit() knows what to erase.
  virtual void current_bounds(int& x, int& y, int& w, int& h) const = 0;

 private:
  bool dirty_ = true;
  bool visible_ = true;
  bool committed_valid_ = false;
  int cx_ = 0, cy_ = 0, cw_ = 0, ch_ = 0;
};

// A solid filled rectangle.
class CanvasRect : public CanvasElement {
 public:
  CanvasRect() = default;
  CanvasRect(int x, int y, int w, int h, bool white = false) : x_(x), y_(y), w_(w), h_(h), white_(white) {}

  void set_position(int x, int y) {
    if (x == x_ && y == y_)
      return;
    x_ = x;
    y_ = y;
    mark_dirty();
  }

  void set_size(int w, int h) {
    if (w == w_ && h == h_)
      return;
    w_ = w;
    h_ = h;
    mark_dirty();
  }

  void set_color(bool white) {
    if (white == white_)
      return;
    white_ = white;
    mark_dirty();
  }

  int x() const {
    return x_;
  }
  int y() const {
    return y_;
  }
  int w() const {
    return w_;
  }
  int h() const {
    return h_;
  }

 protected:
  void draw_content(DisplayQueue& queue) const override {
    queue.submit(x_, y_, w_, h_, white_);
  }

  void current_bounds(int& x, int& y, int& w, int& h) const override {
    x = x_;
    y = y_;
    w = w_;
    h = h_;
  }

 private:
  int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
  bool white_ = false;
};

}  // namespace microreader
