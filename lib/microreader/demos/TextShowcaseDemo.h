#pragma once

#include "../Canvas.h"
#include "../DisplayQueue.h"
#include "../Input.h"
#include "IScreen.h"

namespace microreader {

// Demonstrates text rendering at multiple scales.
// Up cycles through scales 1–4, Button0 exits.
class TextShowcaseDemo final : public IScreen {
 public:
  const char* name() const override {
    return "Text Showcase";
  }

  void start(Canvas& canvas, DisplayQueue& queue) override;
  void stop() override;
  bool update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& runtime) override;

 private:
  static constexpr int kMaxLines = 6;
  CanvasText title_{0, 0, "Text Showcase", true, 2};
  CanvasText lines_[kMaxLines];
  int line_count_ = 0;
  int scale_ = 1;

  void layout_(Canvas& canvas, DisplayQueue& queue);
};

}  // namespace microreader
