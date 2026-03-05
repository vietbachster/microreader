#include <cmath>
#include <string>

#include "epd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "logger.h"
#include "microreader/Input.h"
#include "serial_lut.h"

// ---- Ball state ----
struct Ball {
  int x, y;    // centre position
  int dx, dy;  // direction: each ±1
  int radius;
  int speed;
};

static constexpr int kNumBalls = 20;

// Varied sizes, speeds, starting positions and diagonal directions.
// Balls 11-20 mirror the radius/speed of balls 1-10 with different positions/dirs.
static Ball balls[kNumBalls] = {
    // --- first 10 ---
    {80,  60,  1,  1,  10, 4 },
    {200, 120, 1,  -1, 20, 6 },
    {400, 240, -1, 1,  35, 6 },
    {600, 80,  -1, -1, 15, 8 },
    {150, 350, 1,  1,  25, 10},
    {700, 400, -1, 1,  12, 12},
    {300, 420, 1,  -1, 40, 5 },
    {500, 300, -1, -1, 18, 14},
    {650, 200, 1,  1,  28, 9 },
    {100, 180, -1, 1,  8,  14},
    {720, 440, -1, -1, 10, 4 },
    {350, 50,  -1, 1,  20, 6 },
    {130, 380, 1,  -1, 35, 6 },
    {450, 430, 1,  1,  15, 8 },
    {620, 260, -1, -1, 25, 10},
    {50,  120, 1,  1,  12, 12},
    {760, 160, -1, 1,  40, 5 },
    {230, 290, 1,  -1, 18, 14},
    {540, 460, -1, 1,  28, 9 },
    {380, 140, 1,  -1, 8,  14},
};

// Set/clear one pixel in the 1bpp buffer (bit 0 = black, bit 1 = white).
static inline void setPixel(uint8_t* buf, int x, int y, bool black) {
  if (x < 0 || x >= EInkDisplay::DISPLAY_WIDTH || y < 0 || y >= EInkDisplay::DISPLAY_HEIGHT)
    return;
  const int byte_idx = y * EInkDisplay::DISPLAY_WIDTH_BYTES + x / 8;
  const uint8_t mask = 0x80u >> (x & 7);
  if (black)
    buf[byte_idx] &= ~mask;
  else
    buf[byte_idx] |= mask;
}

// Draw a filled circle using the midpoint algorithm.
static void drawFilledCircle(uint8_t* buf, int cx, int cy, int r, bool black) {
  for (int dy = -r; dy <= r; ++dy) {
    const int half_w = static_cast<int>(sqrtf(static_cast<float>(r * r - dy * dy)));
    for (int dx = -half_w; dx <= half_w; ++dx) {
      setPixel(buf, cx + dx, cy + dy, black);
    }
  }
}

// Move the ball by its own speed. dir: +1 = forward, -1 = backward.
static void ballMove(Ball& b, int dir) {
  b.x += b.dx * dir * b.speed;
  if (b.x - b.radius < 0) {
    b.x = b.radius;
    b.dx = -b.dx;
  } else if (b.x + b.radius > EInkDisplay::DISPLAY_WIDTH - 1) {
    b.x = EInkDisplay::DISPLAY_WIDTH - 1 - b.radius;
    b.dx = -b.dx;
  }

  b.y += b.dy * dir * b.speed;
  if (b.y - b.radius < 0) {
    b.y = b.radius;
    b.dy = -b.dy;
  } else if (b.y + b.radius > EInkDisplay::DISPLAY_HEIGHT - 1) {
    b.y = EInkDisplay::DISPLAY_HEIGHT - 1 - b.radius;
    b.dy = -b.dy;
  }
}

// Clear the framebuffer, draw all balls, upload to display, and log the time.
static RefreshMode drawScene(EInkDisplay& epd, microreader::ILogger& logger) {
  epd.clearScreen(0xFF);
  for (int i = 0; i < kNumBalls; ++i)
    drawFilledCircle(epd.frameBuffer, balls[i].x, balls[i].y, balls[i].radius, true);

  const int64_t t_write0 = esp_timer_get_time();
  const RefreshMode mode = epd.writeBuffers(FAST_REFRESH);
  const int64_t t_write_ms = (esp_timer_get_time() - t_write0) / 1000;

  logger.log(microreader::LogLevel::Info, "upload: " + std::to_string(t_write_ms) + " ms");
  return mode;
}

extern "C" void app_main(void) {
  serial_lut_start();

  static Esp32Logger logger;
  static Esp32InputSource input;
  static EInkDisplay epd;
  epd.begin();

  // Start ball in the centre moving diagonally.
  bool autoMove = false;
  int pending_refreshes = 0;
  RefreshMode pending_refresh_mode = FAST_REFRESH;

  {
    pending_refresh_mode = drawScene(epd, logger);
    pending_refreshes = 1;
  }
  logger.log(microreader::LogLevel::Info, "Button0=fwd  Button1=back  Up/Down=toggle auto");

  while (true) {
    const int64_t loop_start = esp_timer_get_time();

    // Apply any LUT received over serial.
    static uint8_t lut_buf[112];
    if (serial_lut_take(lut_buf)) {
      epd.setCustomLUT(true, lut_buf);
      logger.log(microreader::LogLevel::Info, "LUT applied");
    }

    const microreader::ButtonState state = input.poll_buttons();

    if (state.is_pressed(microreader::Button::Up) || state.is_pressed(microreader::Button::Down)) {
      autoMove = !autoMove;
      logger.log(microreader::LogLevel::Info, autoMove ? "auto ON" : "auto OFF");
    }

    if (autoMove) {
      for (int i = 0; i < kNumBalls; ++i)
        ballMove(balls[i], 1);
      pending_refresh_mode = drawScene(epd, logger);
      pending_refreshes = 6;
    } else if (state.is_pressed(microreader::Button::Button0)) {
      for (int i = 0; i < kNumBalls; ++i)
        ballMove(balls[i], 1);
      logger.log(microreader::LogLevel::Info, "forward");
      pending_refresh_mode = drawScene(epd, logger);
      pending_refreshes = 6;
    } else if (state.is_pressed(microreader::Button::Button1)) {
      for (int i = 0; i < kNumBalls; ++i)
        ballMove(balls[i], -1);
      logger.log(microreader::LogLevel::Info, "backward");
      pending_refresh_mode = drawScene(epd, logger);
      pending_refreshes = 6;
    } else if (state.is_pressed(microreader::Button::Button2)) {
      logger.log(microreader::LogLevel::Info, "clearing screen white");
      epd.clearScreen(0xFF);
      epd.displayBuffer(FAST_REFRESH);
    } else if (state.is_pressed(microreader::Button::Button3)) {
      logger.log(microreader::LogLevel::Info, "clearing screen black");
      epd.clearScreen(0x00);
      epd.displayBuffer(FAST_REFRESH);
    }

    // Drain one refresh per loop.
    if (pending_refreshes > 0) {
      const int64_t t_refresh0 = esp_timer_get_time();
      epd.refreshDisplay(pending_refresh_mode, false);
      const int64_t t_refresh_ms = (esp_timer_get_time() - t_refresh0) / 1000;
      --pending_refreshes;
      logger.log(microreader::LogLevel::Info,
                 "refresh: " + std::to_string(t_refresh_ms) + " ms  remaining: " + std::to_string(pending_refreshes));
    }

    constexpr int64_t kMinLoopMs = 20;
    const int64_t elapsed_ms = (esp_timer_get_time() - loop_start) / 1000;
    if (elapsed_ms < kMinLoopMs)
      vTaskDelay(pdMS_TO_TICKS(kMinLoopMs - elapsed_ms));
  }
}
