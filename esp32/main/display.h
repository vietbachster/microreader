#pragma once

#include "microreader/core/Display.h"

class Esp32Display final : public microreader::IDisplay {
 public:
  void set_rotation(microreader::Rotation r) override {
    rotation_ = r;
    // TODO: write rotation register on the e-ink driver (0/90/180/270°).
    (void)r;
  }

  microreader::Rotation rotation() const override {
    return rotation_;
  }

  void present(const microreader::DisplayFrame& frame, microreader::RefreshMode mode) override {
    (void)mode;
    // TODO: Unpack frame.pixels (800×480, 1-bit row-major, MSB = leftmost pixel,
    //       kStride=100 bytes/row) into the e-ink driver framebuffer and trigger
    //       a full refresh (RefreshMode::Full) or partial refresh (RefreshMode::Fast).
    (void)frame;
  }

 private:
  microreader::Rotation rotation_ = microreader::Rotation::Deg0;
};
