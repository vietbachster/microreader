#pragma once

#include "../Input.h"
#include "../Runtime.h"
#include "../display/Canvas.h"
#include "../display/DisplayQueue.h"

namespace microreader {

// Interface for application screens that Application can switch between.
class IScreen {
 public:
  virtual ~IScreen() = default;

  virtual const char* name() const = 0;

  // Called once when this screen becomes active.
  virtual void start(Canvas& canvas, DisplayQueue& queue) = 0;

  // Called once when leaving this screen. Canvas cleanup is handled by ScreenManager.
  virtual void stop() = 0;

  // Per-frame update. Return true to stay on this screen, false to go back to menu.
  virtual bool update(const ButtonState& buttons, Canvas& canvas, DisplayQueue& queue, IRuntime& runtime) = 0;
};

}  // namespace microreader
