# Microreader2 — Project Summary

E-ink ebook reader targeting **ESP32-C3 + SSD1677 e-paper** (800×480 physical, rotated 90° → **480×800 portrait**) with a **desktop SDL2 emulator** for rapid development.

## Architecture

```
lib/microreader/          ← shared core (platform-agnostic C++17)
  content/                ← EPUB parsing & layout pipeline
  demos/                  ← IScreen implementations (UI screens)
platforms/desktop/        ← SDL2 desktop emulator
platforms/esp32/          ← real hardware (ESP-IDF + PlatformIO)
test/                     ← Google Test suite (unit + integration)
tools/                    ← Python scripts (LUT editor, upload, etc.)
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

## EPUB content pipeline

Located in `lib/microreader/content/`. This is the chain that turns an `.epub` file into rendered pages:

```
EPUB file (ZIP on SD card)
  → ZipReader        (parse central directory, lazy extraction)
  → Epub::open()     (streaming OPF/NCX parse via XmlReader, builds spine + TOC)
  → Epub::parse_chapter()  (extract XHTML → parse into Chapter)
  → layout_page()    (Chapter + Font → PageContent with positioned words)
  → ReaderScreen     (PageContent → paint lambda → DisplayQueue)
```

### Key content files

| File | Purpose |
|------|---------|
| `ZipReader.h/.cpp` | ZIP central directory reader. `ZipEntry` stores `std::string_view name` + offsets (no data); all names live in a single contiguous `name_blob_` vector inside `ZipReader` (two-pass: count total name bytes, then bulk-allocate). `ZipEntryInput` streams decompression with ~33KB constant memory (32KB LZ dict + 1KB input buffer). `extract()` decompresses fully into `std::vector<uint8_t>`. |
| `MrbReader.h/.cpp` | Reads `.mrb` (pre-processed binary) files. Loads chapter table + image refs into RAM on `open()`, but the paragraph index is **not loaded into RAM** — each entry (8 bytes) is seeked on demand in `load_paragraph()`. This avoids allocating `paragraph_count * 8` bytes (e.g. ~136KB for War and Peace). `MrbChapterSource` wraps `MrbReader` as an `IParagraphSource` with lazy paragraph caching per chapter. |
| `XmlReader.h/.cpp` | Streaming XML SAX parser with configurable buffer (typically 4–8KB). Emits `StartElement`, `EndElement`, `Text`, `CData` events. Pluggable `IXmlInput` for data source. |
| `EpubParser.h/.cpp` | `Epub` class: `open()` streams OPF via `ZipEntryInput` → `XmlReader` (never loads full OPF into RAM). Builds compact manifest (FNV-1a hashes, ~6 bytes/entry). `parse_chapter()` extracts full XHTML into memory then parses to `Chapter`. Also resolves image dimensions by extracting image files. |
| `ContentModel.h` | Core data structures: `Run` (styled text span), `TextParagraph` (runs + alignment/indent), `Paragraph` (Text/Image/Hr/PageBreak), `Chapter` (title + paragraphs), `SpineItem`, `TocEntry`, `EpubMetadata`. |
| `TextLayout.h/.cpp` | `IFont` interface for font metrics; `FixedFont` for testing. `layout_paragraph()` → word-wrap runs into `LayoutLine`s. `layout_page()` → fill one page from chapter position → `PageContent` with positioned `PageTextItem`/`PageImageItem`/`PageHrItem`. |
| `CssParser.h/.cpp` | Minimal CSS parser for EPUB stylesheets. `CssStylesheet` cascades by specificity. |
| `ImageDecoder.h/.cpp` | JPEG/PNG detection and dimension reading. Skipped on ESP32 (`MICROREADER_NO_IMAGES`). |
| `Book.h/.cpp` | High-level wrapper: owns `StdioZipFile` + `Epub`. Methods: `open(path)`, `load_chapter(index, Chapter&)`, `decode_image()`. |

### Memory constraints (critical for ESP32-C3)

The ESP32-C3 has **~320KB total RAM**, of which **~155KB is free after SD card init**. The EPUB pipeline must fit within this budget:

| Component | Typical Cost | Notes |
|-----------|-------------|-------|
| `Book::open()` metadata | 30–45KB | ZIP entries × ~60 bytes each (struct + string_view into contiguous name blob — 2 allocations total) + spine + TOC + stylesheet. War and Peace (377 entries) verified on device: 126KB free after open. |
| `MrbReader::open()` | 3–5KB | Chapter table (368 × 8 = ~3KB) + image refs + metadata/TOC. Paragraph index is NOT loaded (seeked on demand). Previously loaded full index (~136KB for War and Peace), causing OOM. |
| `parse_chapter()` XHTML extract | up to 218KB | Full chapter XHTML decompressed into `std::vector<uint8_t>` + 33KB work buffer |
| `parse_chapter()` image dim resolve | up to 200KB | Extracts each image file to read JPEG/PNG headers |
| `Chapter` in memory | up to 250KB+ | 268 paragraphs × 684 runs × `std::string` text (177KB text alone for largest chapter) |
| `layout_page()` | ~5–10KB | One page at a time, returns `PageContent` with word pointers into `Chapter` |

**Known crash**: ohler.epub ("Der totale Rausch") has 487 spine items mapping to only ~10 XHTML files, the largest being 218KB. The current `parse_chapter()` approach of extracting the full XHTML + resolving image dimensions exceeds available heap. **This needs to be fixed** — likely by pre-processing EPUBs into a compact on-device format or by streaming the XHTML parsing.

## Screens (IScreen implementations)

All inherit from `IScreen` (`lib/microreader/demos/IScreen.h`): `name()`, `start()`, `stop()`, `update()`.

| Screen | File | Description |
|--------|------|-------------|
| `MenuDemo` | `MenuDemo.h/.cpp` | Main navigation menu. Owns all child screens. Items: Select Book, demos, settings. |
| `BookSelectScreen` | `BookSelectScreen.h/.cpp` | Lists `.epub`/`.mrb` files from a directory. Up/down navigation (no scroll — flat list via Canvas). Owns a `ReaderScreen`. |
| `ReaderScreen` | `ReaderScreen.h/.cpp` | EPUB page viewer. 2×-scaled 8×8 bitmap font (16×16 glyphs). Next/prev page, chapter transitions. |
| `BouncingBallDemo` | `BouncingBallDemo.h/.cpp` | Bouncing ball + random shapes. |
| `TextShowcaseDemo` | `TextShowcaseDemo.h/.cpp` | Typography demo at multiple scales. |
| `PatternDemo` | `PatternDemo.h/.cpp` | Scrolling checkerboard pattern. |

### Screen navigation flow

```
Application
  └─ ScreenManager (push/pop stack, max depth 8)
       └─ MenuDemo (always at bottom)
            ├─ "Select Book" → push BookSelectScreen
            │     └─ user picks → push ReaderScreen (via Application chain)
            ├─ "Bouncing Ball" → push BouncingBallDemo
            ├─ "Text Showcase" → push TextShowcaseDemo
            └─ "Pattern Demo" → push PatternDemo

Button0 = back (screen returns false → pop)
Button1 = select
Button2 = down / next page
Button3 = up / prev page
```

## Core systems

- **DisplayQueue**: dual-buffer (ground_truth + target) phase-based animation. Commands progress over N phases before committing. After all commands finish, a one-shot **settle refresh** fires on the next tick using a dedicated `lut_settle` waveform.
- **Canvas**: z-ordered scene graph with damage-rect redraw. Elements: `CanvasRect`, `CanvasCircle`, `CanvasText`.
- **Font** (`Font.h`): Static 8×8 bitmap font — `detail::kFont8x8[95][8]`, 95 printable ASCII glyphs (0x20–0x7E). Each glyph = 8 bytes, one byte per row, MSB = leftmost pixel. `draw_char()` / `draw_text()` helpers.
- **Input**: `ButtonState` carries `current` + `pressed_latch`. Auto-repeat at hardware layer (5ms sample on ESP32). Screens use `is_pressed()`.
- **Loop**: `run_loop()` polls input → app.update() → queue.tick() → wait_next_frame().

## Build commands

### Desktop
```bash
cd microreader2
cmake -S platforms/desktop -B build/desktop-debug -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5"
cmake --build build/desktop-debug --config Debug
```

### ESP32 (PlatformIO)
```bash
cd microreader2
# Build only:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run
# Build + flash:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe run -t upload
# Monitor serial:
$env:USERPROFILE\.platformio\penv\Scripts\pio.exe device monitor --baud 115200
```
- Board: ESP32-C3-DevKitM-1, 16MB flash, COM4
- Upload baud: 921600, monitor baud: 115200
- Main task stack: 16KB
- ESP32 sources are **explicitly listed** in `platforms/esp32/CMakeLists.txt` (NOT auto-discovered by PIO LDF). When adding new `.cpp` files, you must add them to the source list.
- Compile definition: `MICROREADER_NO_IMAGES` (no stb_image on ESP32)

### Tests
```bash
cd microreader2/test
cmake -B build2 -DCMAKE_BUILD_TYPE=Debug
cmake --build build2 --config Debug

# Fast unit tests (~330 tests, <1 second):
.\build2\Debug\unit_tests.exe

# Full suite including real EPUB integration tests:
.\build2\Debug\microreader_tests.exe

# Specific test filter:
.\build2\Debug\microreader_tests.exe --gtest_filter="DebugOhler.*"
```

Two test binaries:
- `unit_tests`: ZipReader, XmlReader, CssParser, EpubParser, ImageDecoder, TextLayout tests
- `microreader_tests`: above + RealBookTest, BulkBookTest, DebugOhlerTest, HtmlExportTest

Test fixtures in `test/fixtures/` (synthetic EPUBs). Real books in `microreader/resources/books/`.

## ESP32 hardware

- **MCU**: ESP32-C3 (RISC-V single core, 160MHz)
- **RAM**: ~320KB total, ~155KB free after SD init
- **Flash**: 16MB, dual OTA partitions (6.4MB each)
- **Display**: SSD1677 e-ink, 800×480 physical, rotated → 480×800 portrait, 1-bit packed (100-byte row stride)
- **SD card**: FAT32 via SPI (shares bus with display)
- **Buttons**: ADC-based with hardware auto-repeat

## Known issues & gotchas

- **ESP32 CMake source list**: New `.cpp` files MUST be added to `platforms/esp32/CMakeLists.txt` explicitly. PIO's LDF auto-discovery does not work for this project structure.
- **CMake exit code 1**: PIO/CMake may return exit code 1 from miniz deprecation warnings on stderr. This is not an actual error — build succeeds.
- **Desktop CMake policy warning**: Use `-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5` to silence.
- **COM4 port busy**: Kill any running monitor terminal before uploading firmware.
- **ohler.epub OOM**: 487 spine items, XHTML files up to 218KB, causes heap exhaustion on ESP32 during `parse_chapter()`. See "Memory constraints" section. (Note: `Book::open()` itself no longer OOMs thanks to ZipReader name_blob_ optimization.)
- **Free heap monitoring**: `ESP_LOGI("mem", "Free heap: %lu", (unsigned long)esp_get_free_heap_size());`

## Device testing

To verify changes on real hardware:
1. Upload test books via `python tools/upload_epub.py --port COM4 <path>.epub`
2. Add temporary test code to `platforms/esp32/main.cpp` (e.g. a `test_book_open()` function that logs heap before/after operations via `ESP_LOGI`)
3. Build + flash: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload`
4. Monitor serial: `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor --baud 115200`
5. **Always revert test code and reflash clean firmware afterward.**

Useful heap logging pattern:
```cpp
#include "esp_heap_caps.h"
ESP_LOGI("test", "Free heap: %lu largest=%lu",
         (unsigned long)esp_get_free_heap_size(),
         (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
```

## Agent rules

- **Keep this file up to date.** When you add, rename, or restructure files, interfaces, or systems, update this summary to reflect the change. This file is the single source of truth for new chat sessions.
- **Record useful discoveries.** When you learn something non-obvious about the codebase (e.g. hardware quirks, tricky build steps, important constraints, or gotchas), add it to the relevant section above so future sessions benefit.
