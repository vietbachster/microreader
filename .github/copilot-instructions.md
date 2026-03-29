# Microreader2 — Project Summary

E-ink reader framework targeting **ESP32-C3 + SSD1677 e-paper** with a **desktop SDL2 emulator** for rapid development.

## Architecture

```
lib/microreader/          ← shared core (platform-agnostic)
platforms/desktop/        ← SDL2 desktop emulator
platforms/esp32/          ← real hardware (ESP-IDF + PlatformIO)
```

**Core** defines abstract interfaces; each platform provides concrete implementations:

| Interface      | Desktop (SDL2)              | ESP32 (real hardware)     |
|----------------|-----------------------------|---------------------------|
| `IDisplay`     | `DesktopEmulatorDisplay`    | `EInkDisplay`             |
| `IRuntime`     | `DesktopRuntime`            | `Esp32Runtime`            |
| `IInputSource` | `DesktopInputSource`        | `Esp32InputSource` (ADC)  |
| `ILogger`      | `DesktopLogger` (stdout)    | `Esp32Logger` (ESP_LOG)   |

## Key distinction: Desktop vs ESP32

- **`platforms/desktop/`** = **emulator/simulator**. `display.h` has a per-pixel float `sim_` buffer that simulates e-ink particle physics (exponential approach). This is where display simulation tweaks go.
- **`platforms/esp32/`** = **real device**. `epd.h` drives the actual SSD1677 panel over SPI. No simulation needed.
- **`lib/microreader/`** = **shared application logic**. `Application`, `Canvas`, `DisplayQueue`, `Loop`, screens — runs identically on both platforms. Do NOT modify core files for desktop-only display concerns.

## Core systems

- **DisplayQueue**: dual-buffer (ground_truth + target) phase-based animation. Commands progress over N phases before committing. After all commands finish, a one-shot **settle refresh** fires on the next tick using a dedicated `lut_settle` waveform that reinforces B→B / W→W pixels to clean up ghosting from fast partial updates.
- **Canvas**: z-ordered scene graph with damage-rect redraw. Elements: `CanvasRect`, `CanvasCircle`, `CanvasText`.
- **Application**: manages screen lifecycle via `ScreenManager` (push/pop stack). Starts with `MenuDemo` at the bottom; selecting a menu item pushes a screen, pressing Down pops back. Handles power button → deep sleep.
- **ScreenManager** (`ScreenManager.h`): generic stack of `IScreen*`. `push()` stops current top, starts new screen. `pop()` stops top, restarts the one below.
- **Screens** (`lib/microreader/demos/`): `IScreen` interface (`name`, `start`, `stop`, `update`). Screens: `MenuDemo` (text menu for screen selection), `BouncingBallDemo` (bouncing ball + random shapes/text). `update()` returning false triggers a pop.
- **Button conventions**: Button0 = back, Button1 = select, Button2 = down, Button3 = up, Up = context action (e.g. toggle pause).
- **Input**: `ButtonState` carries `current` (instantaneous) + `pressed_latch` (accumulated rising edges). Auto-repeat is generated at the hardware sampling layer — ESP32's 5 ms `sample()` timer and Desktop's `pump_events()` inject synthetic latch bits when a button is held past the delay (constants in `ButtonState::kRepeatDelayMs` / `kRepeatIntervalMs`). Screens just use `is_pressed()` naturally.
- **Loop**: `run_loop()` polls input → app.update() → queue.tick() → wait_next_frame().

## Build

- **Desktop**: CMake (`platforms/desktop/CMakeLists.txt`), fetches SDL2 statically. Task: "CMake Build Desktop Debug".
- **ESP32**: PlatformIO + ESP-IDF (`platformio.ini`). Board: ESP32-C3-DevKitM-1.

## Tools

- `tools/lut_editor.py`: Python GUI for editing SSD1677 LUT waveforms, sends over serial.

## Agent rules

- **Keep this file up to date.** When you add, rename, or restructure files, interfaces, or systems, update this summary to reflect the change. This file is the single source of truth for new chat sessions.
- **Record useful discoveries.** When you learn something non-obvious about the codebase (e.g. hardware quirks, tricky build steps, important constraints, or gotchas), add it to the relevant section above so future sessions benefit.
