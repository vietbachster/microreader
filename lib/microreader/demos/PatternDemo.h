#pragma once

#include "../Canvas.h"
#include "../DisplayQueue.h"
#include "../Input.h"
#include "IScreen.h"

namespace microreader {

// Scrolling checkerboard pattern.
// Up toggles pause, Button0 exits.
class PatternDemo final : public IScreen {
 public:
  const char* name() const override {
    return "Patterns";
  }

  void start(Canvas& canvas, DisplayQueue& queue) override;
  void stop() override;
  bool update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& runtime) override;

 private:
  static constexpr int kBlock = 40;
  int offset_ = 0;
  bool paused_ = false;
};

}  // namespace microreader
