#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Display.h"

namespace microreader {

// A pending region update command.
//
// Drives a rectangular region toward a target color over `phases_total` ticks.
// Commands are submitted to DisplayQueue and run until completion.
struct UpdateCommand {
  int x, y, w, h;
  bool white;          // target color: true = white, false = black
  uint32_t timestamp;  // monotonically increasing submission order
  int phases_done;
};

// Simulated e-ink display controller.
//
// Maintains two 1-bit buffers (Deg0 physical layout):
//   ground_truth_ — the committed pixel state (settled, all commands done).
//   target_       — ground_truth_ with all in-flight commands overlaid;
//                   represents where every pixel is heading.
//
// The desktop simulator uses both to drive a per-pixel float simulation:
// pixels animate from ground_truth toward target over `phases` ticks.
//
// submit() appends a command and rebuilds target_.
// tick()   advances commands by one phase; completed ones are committed
//          to ground_truth_ and retired, then target_ is rebuilt.
class DisplayQueue {
 public:
  int phases = 8;  // number of ticks a command runs before committing

  explicit DisplayQueue(IDisplay& display) : display_(display), next_ts_(0) {
    memset(ground_truth_, 0xFF, DisplayFrame::kPixelBytes);
    memset(target_, 0xFF, DisplayFrame::kPixelBytes);
  }

  // Submit a fill command for the region (x, y, w, h) toward `white`.
  // Returns the command's timestamp (monotonically increasing).
  uint32_t submit(int x, int y, int w, int h, bool white) {
    // Fast-forward any in-flight pixels in this region whose current net
    // target conflicts with the new command.  Without this, overlapping
    // commands whose colors cancel out (e.g. black then white on the same
    // area) would leave ground_truth == target, making the simulator treat
    // those pixels as settled even though commands are still running.
    //
    // For each affected pixel where ground_truth != target (in-flight) and
    // target != new command color (conflict), we commit the current target
    // into ground_truth so the new command runs from there instead.
    if (!commands_.empty()) {
      DisplayFrame gt(ground_truth_);
      const int x2 = x + w;
      const int y2 = y + h;
      // One visited flag per pixel in the new command's bounding box.
      // Iterating newest→oldest ensures the most recent command that covers
      // a pixel is the one that decides whether to fast-forward ground_truth;
      // once a pixel is handled we skip it for older commands.
      std::vector<bool> visited(static_cast<std::size_t>(w * h), false);
      for (int i = static_cast<int>(commands_.size()) - 1; i >= 0; --i) {
        const auto& cmd = commands_[i];
        const int ix1 = std::max(cmd.x, x);
        const int iy1 = std::max(cmd.y, y);
        const int ix2 = std::min(cmd.x + cmd.w, x2);
        const int iy2 = std::min(cmd.y + cmd.h, y2);
        if (ix2 <= ix1 || iy2 <= iy1)
          continue;

        for (int py = iy1; py < iy2; ++py) {
          for (int px = ix1; px < ix2; ++px) {
            const int vi = (py - y) * w + (px - x);
            if (visited[vi])
              continue;
            const bool gt_val = gt.get_pixel(px, py);
            if (cmd.white != white) {
              gt.set_pixel(px, py, cmd.white);
              visited[vi] = true;
            }
          }
        }
      }
    }
    commands_.push_back({x, y, w, h, white, next_ts_, 0});
    next_ts_++;
    rebuild_target_();
    return next_ts_ - 1;
  }

  // Advance all active commands by one phase.
  // Completed commands are committed to ground_truth_ then retired.
  // commands_ is always in insertion (timestamp) order — no sort needed.
  void tick() {
    if (commands_.empty())
      return;

    // Separate finished commands from survivors in one pass.
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
      // Commit finished commands into ground_truth, but only for pixels that
      // are not covered by any still-active command.  Writing into a pixel
      // that a newer command owns would corrupt the ground_truth value that
      // submit() fast-forwarded for that command, causing gt == target for
      // in-flight pixels and breaking the transition animation.
      DisplayFrame gt(ground_truth_);
      for (const auto& done : finished) {
        const int x2 = done.x + done.w;
        const int y2 = done.y + done.h;
        for (int py = done.y; py < y2; ++py) {
          for (int px = done.x; px < x2; ++px) {
            bool covered = false;
            for (const auto& active : commands_) {
              if (px >= active.x && px < active.x + active.w && py >= active.y && py < active.y + active.h) {
                covered = true;
                break;
              }
            }
            if (!covered)
              gt.set_pixel(px, py, done.white);
          }
        }
      }

      ground_truth_dirty_ = true;
      rebuild_target_();
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

  // Immediately commit target to ground_truth, clear all pending commands,
  // and trigger a physical full refresh on the display driver.
  void full_refresh(RefreshMode mode = RefreshMode::Full) {
    commands_.clear();
    memcpy(ground_truth_, target_, DisplayFrame::kPixelBytes);
    display_.full_refresh(target_, mode);
    ground_truth_dirty_ = false;
    target_dirty_ = false;
  }

  // Fill the screen with a solid color, clear all pending commands, and refresh.
  void clear_screen(bool white = true, RefreshMode mode = RefreshMode::Full) {
    commands_.clear();
    memset(ground_truth_, white ? 0xFF : 0x00, DisplayFrame::kPixelBytes);
    memset(target_, white ? 0xFF : 0x00, DisplayFrame::kPixelBytes);
    display_.full_refresh(target_, mode);
    ground_truth_dirty_ = false;
    target_dirty_ = false;
  }

 private:
  IDisplay& display_;
  alignas(4) uint8_t ground_truth_[DisplayFrame::kPixelBytes];
  alignas(4) uint8_t target_[DisplayFrame::kPixelBytes];
  bool ground_truth_dirty_ = false;
  bool target_dirty_ = false;
  std::vector<UpdateCommand> commands_;
  uint32_t next_ts_;

  // Recompute target_: start from ground_truth_, overlay all active commands.
  void rebuild_target_() {
    memcpy(target_, ground_truth_, DisplayFrame::kPixelBytes);
    for (const auto& cmd : commands_)
      paint(target_, cmd);
    target_dirty_ = true;
  }

  // Paint cmd's target color into `buf` (raw kPixelBytes buffer, Deg0 layout).
  // Uses DisplayFrame as a coordinate/bit helper (no buffer ownership).
  static void paint(uint8_t* buf, const UpdateCommand& cmd) {
    DisplayFrame frame(buf);  // Deg0: logical coords == physical coords
    const int x2 = cmd.x + cmd.w;
    const int y2 = cmd.y + cmd.h;
    for (int y = cmd.y; y < y2; ++y)
      for (int x = cmd.x; x < x2; ++x)
        frame.set_pixel(x, y, cmd.white);  // bounds-checked inside set_pixel
  }
};

}  // namespace microreader
