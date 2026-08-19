# Game Boy Emulator Application

This component is a Game Boy (DMG) emulator built on the single-header
[Peanut-GB](https://github.com/deltabeard/Peanut-GB) core plus a HighBoy
platform layer. It runs `.gb` / `.gbc` ROMs from the SD card, takes over the
ST7789 panel in landscape, plays audio through the I2S codec, and persists
battery-backed cartridge RAM back to the SD card. It is reached from the games
menu through a ROM picker screen.

## Overview

- **Emulator location:** `components/Applications/gameboy/`
- **ROM picker location:** `components/Applications/ui/screens/games/gb_ui.c`
- **Core:** Peanut-GB (`peanut_gb.h`, single-header) + `minigb_apu` (APU)
- **Dependencies:** `Drivers`, `Service`, `lvgl`, `esp_lcd`, `esp_timer`, `esp_system`, `driver`
- **Build flags:** `ENABLE_SOUND=1`, `MINIGB_APU_AUDIO_FORMAT_S16SYS`, compiled `-O2 -w`
- **Native GB resolution:** 160 x 144 (`LCD_WIDTH` x `LCD_HEIGHT`)
- **Output resolution:** 320 x 240 (full screen, landscape)

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                  ROM Picker (gb_ui.c)                     │
│  Scans SD recursively, lists ROMs, launches emulator,    │
│  polls highboy_gb_finished(), restores panel + returns   │
└───────────────────────────┬──────────────────────────────┘
                            │ highboy_gb_start(path)
                            ▼
┌──────────────────────────────────────────────────────────┐
│                 HighBoy Platform (gb_highboy.c)          │
│                                                          │
│  ┌────────────┐   ┌──────────────┐   ┌────────────────┐  │
│  │ gb_main    │   │  Peanut-GB   │   │  gb_audio      │  │
│  │ task       │──▶│  core        │   │  task          │  │
│  │ (SYS_CORE_ │   │ (peanut_gb.h)│   │ (SYS_CORE_     │  │
│  │  UI)       │   └──────┬───────┘   │  RADIO)        │  │
│  └─────┬──────┘          │           └───────┬────────┘  │
│        │        rom/cram callbacks           │           │
│        ▼                 ▼                    ▼           │
│  ┌──────────┐   ┌────────────────┐   ┌────────────────┐  │
│  │ scale +  │   │ CRAM save/load │   │ minigb_apu     │  │
│  │ blit     │   │ <rom>.sav (SD) │   │ 32768 Hz stereo│  │
│  │ (strips) │   └────────────────┘   │ -> mono I2S    │  │
│  └────┬─────┘                        └────────────────┘  │
└───────┼──────────────────────────────────────────────────┘
        ▼
   ST7789 panel (esp_lcd_panel_draw_bitmap, under LVGL lock)
```

## ROM Picker (`gb_ui.c`)

`ui_gb_open()` builds an LVGL list of the ROMs found on the SD card.

- **Where ROMs come from:** `scan_dir()` walks `/sdcard` recursively to a depth
  of `MAX_DEPTH` (3), collecting up to `MAX_ROMS` (64) files whose name ends in
  `.gb` or `.gbc` (case-insensitive). Dot-files are skipped. Path and name
  tables live in PSRAM (`EXT_RAM_BSS_ATTR`).
- **Navigation:** a repeating LVGL timer (`NAV_MS` = 80 ms) reads buttons. UP /
  DOWN move the selection, OK launches the highlighted ROM, BACK / LEFT return
  to `SCREEN_GAMES_MENU`. With no ROMs found, the screen shows a "copy games
  anywhere on the SD card" message.
- **Launch / return handshake:** OK calls `highboy_gb_start(path)` and sets a
  `launched` flag. The nav timer then polls `highboy_gb_finished()`; once the
  emulator has torn down it restores the panel orientation
  (`lcd_set_rotation(lcd_get_rotation())`) and switches back to the games menu -
  no firmware reboot.

## Emulator Core (`gb_highboy.c`)

### Public API (`gb_highboy.h`)

```c
void highboy_gb_start(const char *rompath);
bool highboy_gb_finished(void);
```

- `highboy_gb_start` resets state and spawns the emulator task. `rompath` is the
  full SD path of the ROM, or `NULL`/`""` to auto-discover the first ROM.
- `highboy_gb_finished` returns `true` only once the task has fully torn down and
  released the panel; it is polled by the ROM picker.

### Tasks

| Task | Stack | Priority | Core |
|------|-------|----------|------|
| `gameboy` (main loop) | 32768 | `SYS_PRIO_SERVICE_HI` | `SYS_CORE_UI` |
| `gb_audio` | 4096 | `SYS_PRIO_SERVICE_HI` | `SYS_CORE_RADIO` |

Both tasks and their buffers are created with `MALLOC_CAP_SPIRAM` caps.

### ROM loading and save RAM

- If no path was supplied, `find_gb_rom()` scans `/sdcard`, `/sdcard/gb`, and
  `/sdcard/roms` for the first `.gb`/`.gbc` file.
- The full ROM is read into PSRAM. `gb_init()` wires the `rom_read` / `cram_read`
  / `cram_write` callbacks.
- Cartridge RAM size comes from `gb_get_save_size_s()`. The save file path is the
  ROM path with its extension replaced by `.sav`, next to the ROM on the SD card.
  CRAM is loaded on start (size must match or it is ignored) and written back
  whenever it has been dirty for `GB_AUTOSAVE_MS` (3 s), plus once more on exit.

### Display integration

- The GB frame is written by the core into an 8-bit shade buffer
  (160 x 144, PSRAM). The DMG palette is four classic greens
  (`0xE0F8D0`, `0x88C070`, `0x346856`, `0x081820`) converted to big-endian
  RGB565 (`to565be`).
- Scaling to 320 x 240 is nearest-neighbor via precomputed `s_sx` / `s_sy`
  lookup tables (no interpolation).
- Output is pushed in 24-row strips (`GB_STRIP_ROWS`) through
  `esp_lcd_panel_draw_bitmap()`, each strip followed by `lvgl_glue_wait_flush()`.
- The emulator takes exclusive control of the panel: it holds the LVGL lock
  (`lvgl_glue_lock(-1)`), enters direct-draw mode (`lvgl_glue_direct_begin()`),
  and rotates to landscape with `esp_lcd_panel_swap_xy(true)` +
  `esp_lcd_panel_mirror(true, false)`. The strip DMA buffer is the only buffer in
  internal DMA RAM (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`).
- `ui_render_beat_kick()` is called every loop iteration so `sys_monitor` sees
  the render path as alive while the standard LVGL render loop is suspended.

### Audio integration

- `minigb_apu` renders at `AUDIO_SAMPLE_RATE` (32768 Hz). The `gb_audio` task
  pulls stereo interleaved samples, downmixes to mono, and writes them to the
  I2S stream (`audio_i2s_stream_*`). Sample buffers are allocated in PSRAM.
- If the APU or its buffers cannot be allocated, the emulator runs silently.

### Frame pacing

The loop targets 60 fps (`GB_FRAME_US`). When it falls behind it sets the core's
`frame_skip` and skips the blit; a blit happens at most once every two frames
(so the panel refresh is capped near 30 fps). If it drifts more than
`GB_RESYNC_US` (250 ms) behind, the frame clock is resynced.

### Controls / input mapping

Input is polled from the GPIO buttons each frame. Because the panel is rotated
to landscape, the D-pad is remapped accordingly:

| Physical button | Game Boy input |
|-----------------|----------------|
| RIGHT | D-pad UP |
| LEFT | D-pad DOWN |
| UP | D-pad LEFT |
| DOWN | D-pad RIGHT |
| OK | A |
| BACK | B |
| OK + BACK | START |
| hold BACK ~1.2 s | exit emulator |

### Teardown

On exit the task saves CRAM, stops the audio task and I2S stream, drains DMA,
frees all buffers, leaves direct-draw mode, sets the finished flag, releases the
LVGL lock, and deletes itself. If the core buffers cannot be allocated at start,
the task calls `esp_restart()` rather than continuing.

## Performance / PSRAM notes

- ROM image, the `gb_s` state struct, the shade framebuffer, the APU context, and
  cartridge RAM all live in PSRAM (`MALLOC_CAP_SPIRAM`); only the 320 x 24 strip
  buffer is in internal DMA RAM.
- The core is performance-critical, so the whole component is built at `-O2` with
  warnings silenced (`-w`).
- The main task uses a large 32 KB stack; the emulator pins to the UI core and
  the audio task to the radio core (see `sys_prio.h`).
