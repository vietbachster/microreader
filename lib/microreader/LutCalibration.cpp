#include "LutCalibration.h"

namespace microreader {

void LutCalibration::start(DisplayQueue& queue) {
  queue.clear_screen(/*white=*/false, RefreshMode::Full);
}

void LutCalibration::update(const ButtonState& buttons, DisplayQueue& queue, IRuntime& runtime) {
  if (buttons.is_pressed(Button::Up)) {
    queue.clear_screen(/*white=*/true, RefreshMode::Full);
  }
  if (buttons.is_pressed(Button::Down)) {
    queue.display_deep_sleep();
  }

  if (buttons.is_pressed(Button::Button0)) {
    // divide the screen 50/50, then fill the left half with white and the right half with black
    queue.submit(0, 0, DisplayFrame::kPhysicalWidth / 2, DisplayFrame::kPhysicalHeight, true);
    queue.submit(DisplayFrame::kPhysicalWidth / 2, 0, DisplayFrame::kPhysicalWidth / 2, DisplayFrame::kPhysicalHeight,
                 false);
  }

  if (buttons.is_pressed(Button::Button1)) {
    // divide the screen 50/50, then fill the left half with white and the right half with black
    queue.submit(0, 0, DisplayFrame::kPhysicalWidth / 2, DisplayFrame::kPhysicalHeight, false);
    queue.submit(DisplayFrame::kPhysicalWidth / 2, 0, DisplayFrame::kPhysicalWidth / 2, DisplayFrame::kPhysicalHeight,
                 true);
  }

  // force update the screen 50 times
  if (buttons.is_pressed(Button::Button2)) {
    for (size_t i = 0; i < 50; i++) {
      queue.tick(true);
      runtime.yield();
    }
  }

  // force update the screen 250 times
  if (buttons.is_pressed(Button::Button3)) {
    for (size_t i = 0; i < 250; i++) {
      queue.tick(true);
      runtime.yield();
    }
  }
}

}  // namespace microreader
