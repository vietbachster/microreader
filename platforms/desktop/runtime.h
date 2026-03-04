#pragma once

#include <SDL.h>

#include <stdexcept>
#include <string>

#include "microreader/Display.h"
#include "microreader/Input.h"
#include "microreader/Loop.h"

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

  // IRuntime
  bool should_continue() const override {
    return !quit_;
  }

  microreader::ButtonState poll_buttons() override {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        quit_ = true;
    }
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    uint8_t mask = 0;
    if (keys[SDL_SCANCODE_A])
      mask |= 1u << static_cast<uint8_t>(microreader::Button::Button0);
    if (keys[SDL_SCANCODE_S])
      mask |= 1u << static_cast<uint8_t>(microreader::Button::Button1);
    if (keys[SDL_SCANCODE_D])
      mask |= 1u << static_cast<uint8_t>(microreader::Button::Button2);
    if (keys[SDL_SCANCODE_F])
      mask |= 1u << static_cast<uint8_t>(microreader::Button::Button3);
    if (keys[SDL_SCANCODE_UP])
      mask |= 1u << static_cast<uint8_t>(microreader::Button::Up);
    if (keys[SDL_SCANCODE_DOWN])
      mask |= 1u << static_cast<uint8_t>(microreader::Button::Down);
    if (keys[SDL_SCANCODE_RETURN])
      mask |= 1u << static_cast<uint8_t>(microreader::Button::Power);
    buttons_.update(mask);
    return buttons_;
  }

  uint32_t frame_time_ms() const override {
    return frame_time_ms_;
  }

  void wait_next_frame() override {
    SDL_Delay(frame_time_ms_);
  }

 private:
  uint32_t frame_time_ms_;
  bool quit_ = false;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  microreader::ButtonState buttons_{};
};
