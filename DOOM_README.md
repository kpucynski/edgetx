# DOOM on EdgeTX — RadioMaster TX15

Port of the DOOM game engine to the latest EdgeTX codebase, targeting the RadioMaster TX15 (STM32H750, 480x272 LCD).

Based on the earlier [edgetx-doom](https://github.com/DavBfr/edgetx-doom) proof-of-concept, updated to work with the current EdgeTX architecture and APIs.

> **Note:** This port was heavily AI-assisted (GitHub Copilot / Claude). All code changes — API migration, build integration, runtime fixes — were produced in an interactive AI-assisted session.

## What it does

- Runs DOOM (shareware or full) on the TX15 hardware
- Reads `DOOM1.WAD` from the SD card at `/DOOM/DOOM1.WAD`
- Renders at 320×200 internal resolution, scaled to 480×272 via nearest-neighbor
- **Sound effects** — all 109 SFX from the WAD, resampled and mixed in real-time
- **Music** — MUS format playback via square-wave synthesis (chiptune style)
- Hardware keys mapped to DOOM controls (D-pad, Enter, Esc)
- Long-press power button to shut down

## Prerequisites

- ARM GNU Toolchain (tested with 14.2.rel1)
- Python 3 with packages: `jinja2`, `pillow`, `lz4`, `clang`, `pyelftools`
- A `DOOM1.WAD` file (shareware or registered)

## How to build

### Configure

```bash
cmake --preset firmware \
  -DPCB=TX15 \
  -DDEFAULT_MODE=2 \
  -DGVARS=YES \
  -DWITH_DOOM=ON
```

### Compile

```bash
cd build/fw
make -j$(nproc) firmware
```

The output `firmware.uf2` is ready to flash.

## How to flash

1. Copy `DOOM1.WAD` to `/DOOM/` on the TX15 SD card
2. Power off the TX15
3. Hold trim buttons and connect USB to enter DFU/UF2 mode
4. Copy `firmware.uf2` to the USB mass storage drive
5. Radio reboots into DOOM

## Controls

| Key | Action |
|-----|--------|
| D-pad Up | Move forward |
| D-pad Down | Move backward |
| D-pad Left | Turn left |
| D-pad Right | Turn right |
| Enter | Fire (in game) / Select (in menu) |
| Esc | Open menu |
| SYS | Use (open doors, switches) |
| Power (long press) | Shut down |

## Changes to EdgeTX core

All changes are gated behind `#if defined(WITH_DOOM)` — zero impact on normal builds.

| File | Change |
|------|--------|
| `radio/src/CMakeLists.txt` | `WITH_DOOM` option, include doom subdirectory |
| `radio/src/tasks.cpp` | DOOM RTOS task (32KB stack) as alternative to menus/audio tasks |
| `radio/src/edgetx.cpp` | Skip LVGL init when `WITH_DOOM` is defined |
| `radio/src/model_init.cpp` | Skip COLORLCD layout factory when `WITH_DOOM` is defined |
| `radio/src/sdcard.h` | `DOOM_PATH` definition |
| `radio/src/audio.h` | `AUDIO_BUFFER_COUNT = 6` for DOOM builds (covers one game frame) |
| `radio/src/gui/colorlcd/mainview/view_statistics.cpp` | Guard stack display with `#ifndef WITH_DOOM` |

## New files

- `radio/src/doom/` — Full DOOM engine (chocolate-doom based), ~180 source files
- `radio/src/doom/edgetx/` — EdgeTX integration layer:
  - `display.cpp` / `display.h` — LCD framebuffer interface
  - `doomgeneric.cpp` / `doomgeneric.h` — Hardware init, input, timing
  - `doom_main.h` — Entry point declaration
  - `sound.cpp` / `sound.h` — SFX audio module (`sound_module_t`)
  - `music.cpp` / `music.h` — MUS music module (`music_module_t`)

## Technical details

- **Display**: DOOM writes directly to the LTDC framebuffer in RGB565, bypassing the LVGL GUI stack entirely
- **D-Cache**: `SCB_CleanDCache()` after framebuffer writes so the LTDC peripheral sees CPU writes to SDRAM
- **Watchdog**: `WDG_RESET()` in game loop, sleep/tick functions, and WAD I/O to survive the 500ms hardware watchdog
- **Memory**: DOOM zone allocator uses 2MB from SDRAM heap via `malloc`
- **WAD I/O**: Uses FatFS (`f_open` / `f_read` / `f_lseek`) to read WAD files from the SD card
- **Scaling**: 320×200 → 480×272 nearest-neighbor, palette-indexed to RGB565 conversion per frame

### Audio

- **Hardware path**: EdgeTX AudioBufferFifo → DMA → SPI2 (I2S) → TAS2505 codec → speaker
- **Format**: 32 kHz, 16-bit signed, mono
- **SFX pipeline**: WAD lump loading → 8-bit unsigned to 16-bit signed conversion → linear-interpolation resampling (11025 → 32000 Hz) → up to 8-channel mixing → AudioBufferFifo
- **SFX cache**: Up to 64 decoded/resampled sounds kept in memory to avoid re-processing
- **Music pipeline**: MUS format parser → 140 tick/sec sequencer → per-sample square-wave synthesis (melodic) + LFSR noise (percussion) → mixed into the same audio buffer as SFX
- **Music voices**: 16 channels (0-14 melodic, 15 percussion), with per-channel volume, pitch bend, and attack/release envelopes
- **Buffer strategy**: 6 × 10 ms buffers (320 samples each) to cover one game frame (~28 ms at 35 fps)

## License

DOOM engine source is licensed under GPLv2. See `radio/src/doom/` file headers for details.
