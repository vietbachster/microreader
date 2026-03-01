#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "microreader/core/Application.h"
#include "microreader/core/Loop.h"

class DesktopEmulatorDisplay final : public microreader::IDisplay {
 public:
  void present(const microreader::DisplayFrame& frame, microreader::RefreshMode mode) override {
    std::cout << "[desktop-emu] present(" << (mode == microreader::RefreshMode::Full ? "Full" : "Fast") << ")"
              << std::endl;
    for (std::size_t i = 0; i < frame.line_count; ++i) {
      std::cout << "  " << frame.lines[i].data() << std::endl;
    }
  }
};

class DesktopLogger final : public microreader::ILogger {
 public:
  void log(microreader::LogLevel level, const std::string& message) override {
    switch (level) {
      case microreader::LogLevel::Info:
        std::cout << "[log][info] " << message << std::endl;
        break;
      case microreader::LogLevel::Warning:
        std::cout << "[log][warn] " << message << std::endl;
        break;
      case microreader::LogLevel::Error:
        std::cerr << "[log][error] " << message << std::endl;
        break;
    }
  }
};

class DesktopInputSource final : public microreader::IInputSource {
 public:
  microreader::ButtonState poll_buttons() override {
    // Minimal emulator input: no key mapping yet, all buttons released.
    state_.update(0);
    return state_;
  }

 private:
  microreader::ButtonState state_{};
};

class DesktopRuntime final : public microreader::IRuntime {
 public:
  DesktopRuntime(uint32_t frame_time_ms, uint32_t max_ticks)
      : frame_time_ms_(frame_time_ms), max_ticks_(max_ticks), input_(nullptr) {}

  void set_input_source(microreader::IInputSource* input) {
    input_ = input;
  }

  bool should_continue() const override {
    return ticks_ < max_ticks_;
  }

  microreader::ButtonState poll_buttons() override {
    if (!input_) {
      microreader::ButtonState empty{};
      empty.update(0);
      return empty;
    }
    return input_->poll_buttons();
  }

  uint32_t frame_time_ms() const override {
    return frame_time_ms_;
  }

  void wait_next_frame() override {
    std::this_thread::sleep_for(std::chrono::milliseconds(frame_time_ms_));
    ++ticks_;
  }

 private:
  uint32_t frame_time_ms_;
  uint32_t max_ticks_;
  uint32_t ticks_ = 0;
  microreader::IInputSource* input_;
};

int main() {
  microreader::Application app;
  DesktopEmulatorDisplay display;
  DesktopLogger logger;
  DesktopInputSource input;
  DesktopRuntime runtime(16, 180);
  runtime.set_input_source(&input);

  std::cout << "desktop loop start" << std::endl;
  microreader::run_loop(app, runtime, display, logger);
  std::cout << "desktop loop end ticks=" << app.tick_count() << " uptime_ms=" << app.uptime_ms() << std::endl;
  return 0;
}
