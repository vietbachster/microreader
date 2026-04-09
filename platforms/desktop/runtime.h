#pragma once

#include <SDL.h>

#include <stdexcept>
#include <string>

#include "microreader/Input.h"
#include "microreader/Runtime.h"
#include "microreader/display/DrawBuffer.h"

// Window scale factor: each display pixel becomes kScale×kScale screen pixels.
static constexpr int kDisplayScale = 1;

class DesktopRuntime final : public microreader::IRuntime {
 public:
  explicit DesktopRuntime(uint32_t frame_time_ms) : frame_time_ms_(frame_time_ms) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
    }
    window_ = SDL_CreateWindow("microreader", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               microreader::DisplayFrame::kPhysicalWidth * kDisplayScale,
                               microreader::DisplayFrame::kPhysicalHeight * kDisplayScale, SDL_WINDOW_SHOWN);
    if (!window_)
      throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_)
      throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());

    // Nearest-neighbour scaling so individual pixels stay crisp when zoomed.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(renderer_, microreader::DisplayFrame::kPhysicalWidth,
                             microreader::DisplayFrame::kPhysicalHeight);

    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                 microreader::DisplayFrame::kPhysicalWidth, microreader::DisplayFrame::kPhysicalHeight);
    if (!texture_)
      throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
  }

  ~DesktopRuntime() {
    if (texture_)
      SDL_DestroyTexture(texture_);
    if (renderer_)
      SDL_DestroyRenderer(renderer_);
    if (window_)
      SDL_DestroyWindow(window_);
    SDL_Quit();
  }

  SDL_Renderer* renderer() const {
    return renderer_;
  }
  SDL_Texture* texture() const {
    return texture_;
  }

  // Resize the window and update the logical render size to match the rotation.
  // 0°/180° → 800×480 window; 90°/270° → 480×800 window.
  void apply_rotation(microreader::Rotation rotation) {
    const bool sideways = rotation == microreader::Rotation::Deg90;
    const int win_w =
        (sideways ? microreader::DisplayFrame::kPhysicalHeight : microreader::DisplayFrame::kPhysicalWidth) *
        kDisplayScale;
    const int win_h =
        (sideways ? microreader::DisplayFrame::kPhysicalWidth : microreader::DisplayFrame::kPhysicalHeight) *
        kDisplayScale;
    SDL_SetWindowSize(window_, win_w, win_h);
    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_RenderSetLogicalSize(
        renderer_, sideways ? microreader::DisplayFrame::kPhysicalHeight : microreader::DisplayFrame::kPhysicalWidth,
        sideways ? microreader::DisplayFrame::kPhysicalWidth : microreader::DisplayFrame::kPhysicalHeight);
  }

  // Register a bool to flip when the user presses T.
  void set_transition_toggle(bool* flag) {
    transition_flag_ = flag;
  }

  // IRuntime: step mode (P toggles, Space advances one tick).
  bool step_mode() const override {
    return step_mode_;
  }
  bool consume_step() override {
    if (!step_requested_)
      return false;
    step_requested_ = false;
    return true;
  }

  // IRuntime
  bool should_continue() const override {
    return !quit_;
  }

  // Consume the latched button presses accumulated since the last call.
  uint8_t consume_pressed_latch() {
    uint8_t l = pressed_latch_;
    pressed_latch_ = 0;
    return l;
  }

  // Process SDL window/debug events. Call once per frame before polling input.
  void pump_events() {
    const uint32_t now = SDL_GetTicks();
    const uint32_t dt = now - last_pump_tick_;
    last_pump_tick_ = now;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        quit_ = true;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_t && transition_flag_)
        *transition_flag_ = !*transition_flag_;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_p)
        step_mode_ = !step_mode_;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_SPACE)
        step_requested_ = true;

      // Latch button key-down events so brief presses between frames are not lost.
      // Also reset repeat tracking for the pressed button.
      if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        using B = microreader::Button;
        auto latch_and_reset = [&](B btn) {
          const uint8_t i = static_cast<uint8_t>(btn);
          pressed_latch_ |= static_cast<uint8_t>(1u << i);
          repeat_hold_[i] = 0;
          repeat_next_[i] = microreader::ButtonState::kRepeatDelayMs;
        };
        switch (e.key.keysym.scancode) {
          case SDL_SCANCODE_LEFT:
            latch_and_reset(B::Button0);
            break;
          case SDL_SCANCODE_RIGHT:
            latch_and_reset(B::Button1);
            break;
          case SDL_SCANCODE_DOWN:
            latch_and_reset(B::Button2);
            break;
          case SDL_SCANCODE_UP:
            latch_and_reset(B::Button3);
            break;
          case SDL_SCANCODE_Q:
            latch_and_reset(B::Up);
            break;
          case SDL_SCANCODE_A:
            latch_and_reset(B::Down);
            break;
          case SDL_SCANCODE_RETURN:
            latch_and_reset(B::Power);
            break;
          default:
            break;
        }
      }
    }

    // Auto-repeat: generate synthetic latch bits for held keys.
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    struct KeyMap {
      SDL_Scancode scan;
      microreader::Button btn;
    };
    static constexpr KeyMap kMap[] = {
        {SDL_SCANCODE_LEFT,   microreader::Button::Button0},
        {SDL_SCANCODE_RIGHT,  microreader::Button::Button1},
        {SDL_SCANCODE_DOWN,   microreader::Button::Button2},
        {SDL_SCANCODE_UP,     microreader::Button::Button3},
        {SDL_SCANCODE_Q,      microreader::Button::Up     },
        {SDL_SCANCODE_A,      microreader::Button::Down   },
        {SDL_SCANCODE_RETURN, microreader::Button::Power  },
    };
    for (const auto& k : kMap) {
      const uint8_t i = static_cast<uint8_t>(k.btn);
      if (keys[k.scan]) {
        repeat_hold_[i] += dt;
        if (repeat_hold_[i] >= repeat_next_[i]) {
          pressed_latch_ |= static_cast<uint8_t>(1u << i);
          repeat_next_[i] += microreader::ButtonState::kRepeatIntervalMs;
        }
      } else {
        repeat_hold_[i] = 0;
        repeat_next_[i] = microreader::ButtonState::kRepeatDelayMs;
      }
    }
  }

  uint32_t frame_time_ms() const override {
    return frame_time_ms_;
  }

  void wait_next_frame() override {
    SDL_Delay(frame_time_ms_);
  }

 private:
  static constexpr uint8_t kNumButtons = microreader::ButtonState::kButtonCount;

  uint32_t frame_time_ms_;
  bool quit_ = false;
  bool* transition_flag_ = nullptr;
  uint8_t pressed_latch_ = 0;
  uint32_t repeat_hold_[kNumButtons] = {};
  uint32_t repeat_next_[kNumButtons] = {};
  uint32_t last_pump_tick_ = 0;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  bool step_mode_ = false;
  bool step_requested_ = false;
};
