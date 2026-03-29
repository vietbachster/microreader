#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "Display.h"

namespace microreader {

// A pending region update command.
//
// Tracks only the bounding box and timing — DisplayQueue is shape-agnostic.
// The actual pixel content is painted into target_ by the caller at submit
// time (via a paint callback or the solid-rect convenience overload).
struct UpdateCommand {
  int x, y, w, h;
  uint32_t timestamp;
  int phases_done;
};

// Simulated e-ink display controller.
//
// Maintains two 1-bit buffers (Deg0 physical layout):
//   ground_truth_ — the committed pixel state (settled, all commands done).
//   target_       — ground_truth_ with all in-flight commands overlaid;
//                   represents where every pixel is heading.
//
// DisplayQueue is shape-agnostic: it tracks rectangular bounding boxes and
// lets callers paint arbitrary content (rects, circles, text, images) into
// the target buffer via a paint callback.
//
// submit(x,y,w,h, painter) — generic: painter draws into target_, the queue
//   saves the old target region, diffs to fast-forward ground_truth, and
//   records the bounding box.
// submit(x,y,w,h, white)   — convenience for solid-rect fills with an
//   optimized fast-forward path.
//
// tick() commits finished commands by copying target_ → ground_truth_ for
// their bounding-box pixels not covered by active commands.
class DisplayQueue {
 public:
  int phases = 10;

  explicit DisplayQueue(IDisplay& display) : display_(display), next_ts_(0) {
    memset(ground_truth_, 0xFF, DisplayFrame::kPixelBytes);
    memset(target_, 0xFF, DisplayFrame::kPixelBytes);
    display_.set_rotation(rotation_);
  }

  // Logical dimensions (rotation-aware).
  int width() const {
    return rotation_ == Rotation::Deg90 ? DisplayFrame::kPhysicalHeight : DisplayFrame::kPhysicalWidth;
  }
  int height() const {
    return rotation_ == Rotation::Deg90 ? DisplayFrame::kPhysicalWidth : DisplayFrame::kPhysicalHeight;
  }

  // Fill a single horizontal span [x1, x2) on one row of a raw buffer.
  // Public so that custom paint callbacks can build any shape from scanlines.
  static void fill_row(uint8_t* buf, int row, int x1, int x2, bool white) {
    x1 = std::max(x1, 0);
    x2 = std::min(x2, DisplayFrame::kPhysicalWidth);
    if (x1 >= x2 || row < 0 || row >= DisplayFrame::kPhysicalHeight)
      return;
    const int bx1 = x1 / 8;
    const int bx2 = (x2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (x1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((x2 - 1) & 7)));
    uint8_t* rp = buf + row * DisplayFrame::kStride;
    if (bx1 == bx2) {
      const auto m = static_cast<uint8_t>(lmask & rmask);
      if (white)
        rp[bx1] |= m;
      else
        rp[bx1] &= static_cast<uint8_t>(~m);
    } else {
      if (white)
        rp[bx1] |= lmask;
      else
        rp[bx1] &= static_cast<uint8_t>(~lmask);
      if (bx2 > bx1 + 1)
        memset(rp + bx1 + 1, white ? 0xFF : 0x00, bx2 - bx1 - 1);
      if (white)
        rp[bx2] |= rmask;
      else
        rp[bx2] &= static_cast<uint8_t>(~rmask);
    }
  }

  // Submit a solid rectangle fill in logical coordinates (fast path).
  uint32_t submit(int x, int y, int w, int h, bool white) {
    int px, py, pw, ph;
    to_physical_rect_(x, y, w, h, px, py, pw, ph);
    if (!commands_.empty())
      fast_forward_rect_(px, py, pw, ph, white);
    commands_.push_back({px, py, pw, ph, next_ts_, 0});
    fill_rect_(target_, px, py, pw, ph, white);
    target_dirty_ = true;
    return next_ts_++;
  }

  // Submit a custom-painted region in logical coordinates.
  // `paint` receives a DisplayFrame wrapping the target buffer (with current
  // rotation) so it can draw using logical coordinates.
  template <typename PaintFn, typename = std::enable_if_t<std::is_invocable_v<PaintFn&, DisplayFrame&>>>
  uint32_t submit(int x, int y, int w, int h, PaintFn&& paint) {
    int px, py, pw, ph;
    to_physical_rect_(x, y, w, h, px, py, pw, ph);
    const int x1 = std::max(px, 0);
    const int y1 = std::max(py, 0);
    const int x2 = std::min(px + pw, DisplayFrame::kPhysicalWidth);
    const int y2 = std::min(py + ph, DisplayFrame::kPhysicalHeight);

    DisplayFrame frame(target_, rotation_);
    if (x1 < x2 && y1 < y2 && !commands_.empty()) {
      // Save the physical target region before the painter modifies it.
      const int bx1 = x1 / 8;
      const int bx2 = (x2 - 1) / 8;
      const int bw = bx2 - bx1 + 1;
      const int rows = y2 - y1;
      save_buf_.resize(static_cast<std::size_t>(rows) * bw);
      for (int r = y1; r < y2; ++r)
        memcpy(save_buf_.data() + (r - y1) * bw, target_ + r * DisplayFrame::kStride + bx1, bw);

      paint(frame);

      // Diff-based fast-forward: for every bit the painter changed,
      // set ground_truth to the OLD target value.  This guarantees
      // gt != new_target for those pixels → display sees a transition.
      for (int r = y1; r < y2; ++r) {
        uint8_t* gt = ground_truth_ + r * DisplayFrame::kStride + bx1;
        const uint8_t* old_t = save_buf_.data() + (r - y1) * bw;
        const uint8_t* new_t = target_ + r * DisplayFrame::kStride + bx1;
        for (int b = 0; b < bw; ++b) {
          const uint8_t changed = old_t[b] ^ new_t[b];
          gt[b] = static_cast<uint8_t>((gt[b] & ~changed) | (old_t[b] & changed));
        }
      }
    } else {
      paint(frame);
    }

    commands_.push_back({px, py, pw, ph, next_ts_, 0});
    target_dirty_ = true;
    return next_ts_++;
  }

  void tick(bool force = false) {
    if (commands_.empty() && !force)
      return;

    std::vector<UpdateCommand> finished;
    int write = 0;
    for (auto& cmd : commands_) {
      ++cmd.phases_done;
      if (cmd.phases_done > phases)
        finished.push_back(cmd);
      else
        commands_[write++] = cmd;
    }
    commands_.resize(write);

    if (!finished.empty()) {
      for (const auto& done : finished)
        commit_finished_(done);
      ground_truth_dirty_ = true;
    }

    display_.tick(ground_truth_, ground_truth_dirty_, target_, target_dirty_);
    ground_truth_dirty_ = false;
    target_dirty_ = false;
  }

  bool idle() const {
    return commands_.empty();
  }
  int pending_count() const {
    return static_cast<int>(commands_.size());
  }

  // Tick until all pending commands have finished.
  void flush() {
    while (!idle())
      tick();
  }

  void full_refresh(RefreshMode mode = RefreshMode::Full) {
    commands_.clear();
    memcpy(ground_truth_, target_, DisplayFrame::kPixelBytes);
    display_.full_refresh(target_, mode);
    ground_truth_dirty_ = false;
    target_dirty_ = false;
  }

  void clear_screen(bool white = true, RefreshMode mode = RefreshMode::Full) {
    commands_.clear();
    memset(ground_truth_, white ? 0xFF : 0x00, DisplayFrame::kPixelBytes);
    memset(target_, white ? 0xFF : 0x00, DisplayFrame::kPixelBytes);
    display_.full_refresh(target_, mode);
    ground_truth_dirty_ = false;
    target_dirty_ = false;
  }

  void display_deep_sleep() {
    display_.deep_sleep();
  }

  void set_rotation(Rotation r) {
    rotation_ = r;
    display_.set_rotation(r);
  }
  Rotation rotation() const {
    return rotation_;
  }

 private:
  IDisplay& display_;
  Rotation rotation_ = Rotation::Deg90;
  alignas(4) uint8_t ground_truth_[DisplayFrame::kPixelBytes];
  alignas(4) uint8_t target_[DisplayFrame::kPixelBytes];
  bool ground_truth_dirty_ = false;
  bool target_dirty_ = false;
  std::vector<UpdateCommand> commands_;
  uint32_t next_ts_;
  std::vector<uint8_t> save_buf_;  // reused by generic submit

  // ── Logical → physical rectangle transform ─────────────────────────
  void to_physical_rect_(int lx, int ly, int lw, int lh, int& px, int& py, int& pw, int& ph) const {
    if (rotation_ == Rotation::Deg0) {
      px = lx;
      py = ly;
      pw = lw;
      ph = lh;
    } else {
      // Deg90: logical (x,y) → physical (y, kPhysH-1-x)
      px = ly;
      py = DisplayFrame::kPhysicalHeight - lx - lw;
      pw = lh;
      ph = lw;
    }
  }

  // ── Byte-level rectangle fill ──────────────────────────────────────
  static void fill_rect_(uint8_t* buf, int rx, int ry, int rw, int rh, bool white) {
    const int x1 = std::max(rx, 0);
    const int y1 = std::max(ry, 0);
    const int x2 = std::min(rx + rw, DisplayFrame::kPhysicalWidth);
    const int y2 = std::min(ry + rh, DisplayFrame::kPhysicalHeight);
    if (x1 >= x2 || y1 >= y2)
      return;
    for (int row = y1; row < y2; ++row)
      fill_row(buf, row, x1, x2, white);
  }

  // ── Byte-level rectangle fast-forward (optimized for solid rects) ──
  void fast_forward_rect_(int rx, int ry, int rw, int rh, bool white) {
    const int x1 = std::max(rx, 0);
    const int y1 = std::max(ry, 0);
    const int x2 = std::min(rx + rw, DisplayFrame::kPhysicalWidth);
    const int y2 = std::min(ry + rh, DisplayFrame::kPhysicalHeight);
    if (x1 >= x2 || y1 >= y2)
      return;

    const int bx1 = x1 / 8;
    const int bx2 = (x2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (x1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((x2 - 1) & 7)));

    for (int row = y1; row < y2; ++row) {
      uint8_t* gt = ground_truth_ + row * DisplayFrame::kStride;
      const uint8_t* tg = target_ + row * DisplayFrame::kStride;

      auto apply = [&](int idx, uint8_t m) {
        if (white)
          gt[idx] = gt[idx] & static_cast<uint8_t>(tg[idx] | ~m);
        else
          gt[idx] = gt[idx] | static_cast<uint8_t>(tg[idx] & m);
      };

      if (bx1 == bx2) {
        apply(bx1, static_cast<uint8_t>(lmask & rmask));
      } else {
        apply(bx1, lmask);
        for (int b = bx1 + 1; b < bx2; ++b) {
          if (white)
            gt[b] &= tg[b];
          else
            gt[b] |= tg[b];
        }
        apply(bx2, rmask);
      }
    }
  }

  // ── Copy a row span from target_ to ground_truth_ ─────────────────
  static void copy_row_(uint8_t* dst, const uint8_t* src, int row, int x1, int x2) {
    x1 = std::max(x1, 0);
    x2 = std::min(x2, DisplayFrame::kPhysicalWidth);
    if (x1 >= x2 || row < 0 || row >= DisplayFrame::kPhysicalHeight)
      return;
    const int bx1 = x1 / 8;
    const int bx2 = (x2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (x1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((x2 - 1) & 7)));
    uint8_t* d = dst + row * DisplayFrame::kStride;
    const uint8_t* s = src + row * DisplayFrame::kStride;
    if (bx1 == bx2) {
      const auto m = static_cast<uint8_t>(lmask & rmask);
      d[bx1] = static_cast<uint8_t>((d[bx1] & ~m) | (s[bx1] & m));
    } else {
      d[bx1] = static_cast<uint8_t>((d[bx1] & ~lmask) | (s[bx1] & lmask));
      if (bx2 > bx1 + 1)
        memcpy(d + bx1 + 1, s + bx1 + 1, bx2 - bx1 - 1);
      d[bx2] = static_cast<uint8_t>((d[bx2] & ~rmask) | (s[bx2] & rmask));
    }
  }

  // ── Commit a finished command to ground_truth ──────────────────────
  // Copies target_ → ground_truth_ for the command's bounding box,
  // skipping pixels covered by still-active commands.  Shape-agnostic:
  // target_ already contains the correct net pixel values.
  void commit_finished_(const UpdateCommand& done) {
    const int dx1 = std::max(done.x, 0);
    const int dy1 = std::max(done.y, 0);
    const int dx2 = std::min(done.x + done.w, DisplayFrame::kPhysicalWidth);
    const int dy2 = std::min(done.y + done.h, DisplayFrame::kPhysicalHeight);
    if (dx1 >= dx2 || dy1 >= dy2)
      return;

    // Collect active commands whose bboxes overlap the finished bbox.
    struct Overlap {
      int x1, x2, y1, y2;
    };
    std::vector<Overlap> overlaps;
    for (const auto& a : commands_) {
      const int ox1 = std::max(a.x, done.x);
      const int oy1 = std::max(a.y, done.y);
      const int ox2 = std::min(a.x + a.w, done.x + done.w);
      const int oy2 = std::min(a.y + a.h, done.y + done.h);
      if (ox1 < ox2 && oy1 < oy2)
        overlaps.push_back({ox1, ox2, oy1, oy2});
    }

    if (overlaps.empty()) {
      for (int row = dy1; row < dy2; ++row)
        copy_row_(ground_truth_, target_, row, dx1, dx2);
      return;
    }

    std::sort(overlaps.begin(), overlaps.end(), [](const Overlap& a, const Overlap& b) { return a.x1 < b.x1; });

    for (int row = dy1; row < dy2; ++row) {
      int cursor = dx1;
      for (const auto& o : overlaps) {
        if (row < o.y1 || row >= o.y2)
          continue;
        if (o.x1 > cursor)
          copy_row_(ground_truth_, target_, row, cursor, o.x1);
        cursor = std::max(cursor, o.x2);
      }
      if (cursor < dx2)
        copy_row_(ground_truth_, target_, row, cursor, dx2);
    }
  }
};

}  // namespace microreader
