#pragma once

#include "Display.h"
#include "DisplayQueue.h"
#include "Input.h"
#include "Runtime.h"

namespace microreader {

class LutCalibration {
 public:
  LutCalibration() = default;

  void start(DisplayQueue& queue);
  void update(const ButtonState& buttons, DisplayQueue& queue, IRuntime& runtime);
};

}  // namespace microreader
