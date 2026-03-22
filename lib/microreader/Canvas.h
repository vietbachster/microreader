#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "DisplayQueue.h"
#include "Font.h"

namespace microreader {

// Forward declaration.
class Canvas;

// Base class for canvas elements that can be managed by a Canvas scene.
//
// Each element has a bounding box, a dirty flag, and a z-index determined
// by its position in the Canvas's element list.  Elements do not submit
// commands individually — the owning Canvas handles erase/redraw and
// ensures overlapping elements are composited correctly.
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
  bool dirty() const {
    return dirty_;
  }

  // Bounding box of the last committed state.
  void committed_bounds(int& x, int& y, int& w, int& h) const {
    x = cx_;
    y = cy_;
    w = cw_;
    h = ch_;
  }
  bool committed_valid() const {
    return committed_valid_;
  }

  // Submit draw commands for the current element state.
  virtual void draw_content(DisplayQueue& queue) const = 0;

  // Report the bounding box of the current state.
  virtual void current_bounds(int& x, int& y, int& w, int& h) const = 0;

 protected:
  void mark_dirty() {
    dirty_ = true;
  }

 private:
  friend class Canvas;

  bool dirty_ = true;
  bool visible_ = true;
  bool committed_valid_ = false;
  int cx_ = 0, cy_ = 0, cw_ = 0, ch_ = 0;

  // Called by Canvas after the element has been drawn (or hidden).
  void accept_commit_() {
    dirty_ = false;
    if (visible_) {
      current_bounds(cx_, cy_, cw_, ch_);
      committed_valid_ = (cw_ > 0 && ch_ > 0);
    } else {
      committed_valid_ = false;
    }
  }
};

// Scene manager that composites CanvasElements with correct overlap handling.
//
// Elements are stored in z-order (index 0 = back, last = front).
// commit() collects damage regions from dirty elements, erases them,
// then redraws all elements that intersect any damage region, back-to-front.
class Canvas {
 public:
  // Add an element at the front (top of z-order).
  void add(CanvasElement* elem) {
    elements_.push_back(elem);
  }

  // Remove an element without erasing it. Call commit() after to clean up.
  void remove(CanvasElement* elem) {
    auto it = std::find(elements_.begin(), elements_.end(), elem);
    if (it != elements_.end())
      elements_.erase(it);
  }

  // Commit all dirty elements.  For each dirty element:
  //   1. Compute damage rect = union of old bbox and new bbox.
  //   2. Erase the damage rect with white.
  //   3. Redraw (back-to-front) every visible element that overlaps any
  //      damage rect, so covered elements are restored correctly.
  void commit(DisplayQueue& queue) {
    // Collect damage rects from dirty elements.
    struct Rect {
      int x, y, w, h;
    };
    std::vector<Rect> damage;
    for (auto* elem : elements_) {
      if (!elem->dirty())
        continue;
      int ox, oy, ow, oh;
      elem->committed_bounds(ox, oy, ow, oh);
      int nx, ny, nw, nh;
      if (elem->visible())
        elem->current_bounds(nx, ny, nw, nh);
      else
        nx = ny = nw = nh = 0;

      if (elem->committed_valid() && (nw > 0 && nh > 0)) {
        // Union of old and new bounding boxes.
        const int ux = std::min(ox, nx);
        const int uy = std::min(oy, ny);
        const int ux2 = std::max(ox + ow, nx + nw);
        const int uy2 = std::max(oy + oh, ny + nh);
        damage.push_back({ux, uy, ux2 - ux, uy2 - uy});
      } else if (elem->committed_valid()) {
        damage.push_back({ox, oy, ow, oh});
      } else if (nw > 0 && nh > 0) {
        damage.push_back({nx, ny, nw, nh});
      }
    }

    if (damage.empty()) {
      // Still need to clear dirty flags.
      for (auto* elem : elements_)
        if (elem->dirty())
          elem->accept_commit_();
      return;
    }

    // Erase all damage rects.
    for (const auto& d : damage)
      queue.submit(d.x, d.y, d.w, d.h, /*white=*/true);

    // Redraw every visible element that intersects any damage rect,
    // in z-order (back-to-front).
    for (auto* elem : elements_) {
      if (!elem->visible())
        continue;
      int ex, ey, ew, eh;
      elem->current_bounds(ex, ey, ew, eh);
      if (ew <= 0 || eh <= 0)
        continue;
      bool overlaps = false;
      for (const auto& d : damage) {
        if (ex < d.x + d.w && ex + ew > d.x && ey < d.y + d.h && ey + eh > d.y) {
          overlaps = true;
          break;
        }
      }
      if (overlaps)
        elem->draw_content(queue);
    }

    // Accept commits for all dirty elements.
    for (auto* elem : elements_)
      if (elem->dirty())
        elem->accept_commit_();
  }

 private:
  std::vector<CanvasElement*> elements_;
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

// A solid filled circle.
class CanvasCircle : public CanvasElement {
 public:
  CanvasCircle() = default;
  CanvasCircle(int cx, int cy, int radius, bool white = false) : cx_(cx), cy_(cy), radius_(radius), white_(white) {}

  void set_position(int cx, int cy) {
    if (cx == cx_ && cy == cy_)
      return;
    cx_ = cx;
    cy_ = cy;
    mark_dirty();
  }

  void set_radius(int r) {
    if (r == radius_)
      return;
    radius_ = r;
    mark_dirty();
  }

  void set_color(bool white) {
    if (white == white_)
      return;
    white_ = white;
    mark_dirty();
  }

  int cx() const {
    return cx_;
  }
  int cy() const {
    return cy_;
  }
  int radius() const {
    return radius_;
  }

 protected:
  void draw_content(DisplayQueue& queue) const override {
    if (radius_ <= 0)
      return;
    const int r = radius_;
    const int ccx = cx_, ccy = cy_;
    const bool w = white_;
    queue.submit(ccx - r, ccy - r, 2 * r + 1, 2 * r + 1, [=](uint8_t* buf) {
      const int r2 = r * r;
      int dx = r;
      for (int dy = 0; dy <= r; ++dy) {
        while (dx * dx + dy * dy > r2)
          --dx;
        if (dx < 0)
          break;
        DisplayQueue::fill_row(buf, ccy + dy, ccx - dx, ccx + dx + 1, w);
        if (dy != 0)
          DisplayQueue::fill_row(buf, ccy - dy, ccx - dx, ccx + dx + 1, w);
      }
    });
  }

  void current_bounds(int& x, int& y, int& w, int& h) const override {
    x = cx_ - radius_;
    y = cy_ - radius_;
    w = 2 * radius_ + 1;
    h = 2 * radius_ + 1;
  }

 private:
  int cx_ = 0, cy_ = 0, radius_ = 0;
  bool white_ = false;
};

// A text label rendered with the 8×8 bitmap font.
class CanvasText : public CanvasElement {
 public:
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;

  CanvasText() = default;
  CanvasText(int x, int y, const char* text, bool white = false) : x_(x), y_(y), white_(white) {
    set_text(text);
  }

  void set_position(int x, int y) {
    if (x == x_ && y == y_)
      return;
    x_ = x;
    y_ = y;
    mark_dirty();
  }

  void set_text(const char* text) {
    if (!text)
      text = "";
    const size_t len = std::strlen(text);
    if (len == len_ && std::memcmp(text_, text, len) == 0)
      return;
    if (len >= sizeof(text_))
      len_ = sizeof(text_) - 1;
    else
      len_ = len;
    std::memcpy(text_, text, len_);
    text_[len_] = '\0';
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
  const char* text() const {
    return text_;
  }

 protected:
  void draw_content(DisplayQueue& queue) const override {
    if (len_ == 0)
      return;
    const int tw = static_cast<int>(len_) * kGlyphW;
    const int px = x_, py = y_;
    const bool w = white_;
    const size_t n = len_;
    // Copy text into lambda capture (small buffer).
    char buf[kMaxLen + 1];
    std::memcpy(buf, text_, n + 1);
    queue.submit(px, py, tw, kGlyphH, [=](uint8_t* frame) {
      // Fill background (opposite of text color).
      for (int row = 0; row < kGlyphH; ++row)
        DisplayQueue::fill_row(frame, py + row, px, px + tw, w);
      // Draw glyphs on top.
      for (size_t i = 0; i < n; ++i) {
        const int idx = static_cast<unsigned char>(buf[i]) - 0x20;
        if (idx < 0 || idx >= 95)
          continue;
        const auto& glyph = detail::kFont8x8[idx];
        const int gx = px + static_cast<int>(i) * kGlyphW;
        for (int row = 0; row < kGlyphH; ++row) {
          const uint8_t bits = glyph[row];
          if (bits == 0)
            continue;
          // Walk set-bit runs to minimise fill_row calls.
          int col = 0;
          while (col < kGlyphW) {
            if (!(bits & (0x80u >> col))) {
              ++col;
              continue;
            }
            const int start = col;
            while (col < kGlyphW && (bits & (0x80u >> col)))
              ++col;
            DisplayQueue::fill_row(frame, py + row, gx + start, gx + col, !w);
          }
        }
      }
    });
  }

  void current_bounds(int& x, int& y, int& w, int& h) const override {
    x = x_;
    y = y_;
    w = static_cast<int>(len_) * kGlyphW;
    h = (len_ > 0) ? kGlyphH : 0;
  }

 private:
  static constexpr size_t kMaxLen = 128;
  int x_ = 0, y_ = 0;
  size_t len_ = 0;
  char text_[kMaxLen + 1] = {};
  bool white_ = false;
};

}  // namespace microreader
