#pragma once

#include "../Input.h"
#include "../Runtime.h"
#include "../display/DrawBuffer.h"

namespace microreader {

// Interface for application screens that Application can switch between.
class IScreen {
 public:
  virtual ~IScreen() = default;

  virtual const char* name() const = 0;

  // Called once when this screen becomes active.
  // Draws initial content into buf; caller is responsible for buf.refresh().
  virtual void start(DrawBuffer& buf) = 0;

  // Called once when leaving this screen.
  virtual void stop() = 0;

  // Per-frame update. Return true to stay on this screen, false to exit.
  // Call buf.refresh() internally when the display needs updating.
  virtual bool update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) = 0;
};

}  // namespace microreader
