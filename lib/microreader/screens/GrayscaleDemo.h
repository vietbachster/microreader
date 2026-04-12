#pragma once

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"

namespace microreader {

// Demo screen showing 5 rectangles at different grayscale levels.
// LUT 00 = no change (pixel stays as BW pass left it), so:
//   White:      not drawn in BW, LSB=0 MSB=0 → stays white
//   Light gray: not drawn in BW, LSB=1 MSB=0 → driven to light gray (from white)
//   Gray:       drawn in BW,     LSB=0 MSB=1 → driven to gray (from black)
//   Dark gray:  drawn in BW,     LSB=1 MSB=1 → driven to dark gray (from black)
//   Black:      drawn in BW,     LSB=0 MSB=0 → no change, stays black
//
// Button2/3 (down/up) move the rectangles + BW refresh (auto-reverts gray).
// Button1 toggles grayscale on/off. Button0 exits.
class GrayscaleDemo final : public IScreen {
 public:
  GrayscaleDemo() = default;

  const char* name() const override {
    return "Grayscale Demo";
  }

  void start(DrawBuffer& buf) override;
  void stop() override;
  bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

 private:
  static constexpr int kNumRects = 5;
  static constexpr int kRectW = 100;
  static constexpr int kRectH = 80;
  static constexpr int kSpacing = 20;

  int offset_y_ = 0;           // vertical offset controlled by up/down
  bool grayscale_on_ = false;  // toggle state

  void draw_bw_(DrawBuffer& buf) const;
  void apply_grayscale_(DrawBuffer& buf) const;
};

}  // namespace microreader
