#pragma once

#include <cstdint>

#include "Canvas.h"
#include "Demo.h"
#include "Display.h"
#include "DisplayQueue.h"
#include "Input.h"
#include "Log.h"
#include "LutCalibration.h"
#include "Runtime.h"

namespace microreader {

class Application {
 public:
  Application() = default;

  const char* build_info() const;
  void start(ILogger& logger, DisplayQueue& queue);
  // Update app logic and submit any display commands to the controller.
  void update(const ButtonState& buttons, uint32_t dt_ms, DisplayQueue& queue, ILogger& logger, IRuntime& runtime);
  bool running() const;
  uint64_t tick_count() const;
  uint32_t uptime_ms() const;

 private:
  ButtonState buttons_{};
  Rotation rotation_ = Rotation::Deg0;
  uint64_t ticks_ = 0;
  uint32_t uptime_ms_ = 0;
  bool dirty_ = true;
  bool started_ = false;
  bool running_ = true;

  Canvas canvas_;  // scene manager — composites all elements
  Demo demo_;
  LutCalibration lut_calibration_;
};

}  // namespace microreader
