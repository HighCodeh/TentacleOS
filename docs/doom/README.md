<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# DOOM on High Boy P4

The `Applications/doom` component contains doomgeneric and the High Boy platform
layer. The launcher streams a WAD from `/sdcard` or `/sdcard/doom`, takes the LVGL
lock, then enters direct panel mode. It renders a 320×200 image on the landscape
320×240 ST7789 with black letterboxing. Holding OK and BACK for about two seconds
reboots into the launcher; ordinary exit does not return control to LVGL.

## Internal RAM and optional game tables

The initialized state, object, sound-effect and music tables are immutable templates in flash. After
finding a WAD, the launcher allocates their mutable copies explicitly in PSRAM
before calling the engine. No copies are allocated during boot or while on Home,
and absence of a WAD does not allocate them. Allocation failure frees all partial
allocations, reports the failure and restarts the launcher. The engine retains
these tables until its existing reboot-based exit; they are not freed while
actors or saved-game code can reference them. Sound-effect aliases are rebased
to the mutable copy, so sound cache state is never written into a flash template. Other engine BSS remains in PSRAM
through the component linker fragment. This adaptation preserves the original
upstream GPL notices and the mutable `states`/`mobjinfo` engine interface.

`python3 tools/highboy/test_doom_memory.py` checks PSRAM-only allocation, all four
allocation failure paths, exact template copying, mutability and repeated init
under address/undefined-behavior sanitizers. It is a host lifecycle test, not a
WAD gameplay test.

## DMA ownership

The platform allocates one internal, DMA-capable RGB565 strip, at most 20 rows
(12,800 bytes). It checks the largest free block and halves the row count until
allocation succeeds, down to a single 640-byte row. Complete allocation failure
logs an error and reboots instead of drawing with a null buffer.

The same strip clears the panel and converts each part of `DG_ScreenBuffer` from
ARGB8888 to big-endian RGB565. Every queued panel draw is followed by
`lvgl_glue_wait_flush(100)` before reusing the strip. This replaces the former
two PSRAM framebuffers and avoids the SPI driver's per-transfer internal bounce
allocations. The launcher owns the strip until reboot; it is never freed while
DMA could be reading it.

The LVGL lock must be acquired **before** `lvgl_glue_direct_begin()`, so the
completion callback is not redirected while LVGL is still rendering. The
existing flush helper has a timeout and no success result; stalled DMA and
unexpected transfer failures still require hardware testing.

## Preserved local controls

The game drains queued menu input and consumes press events while LVGL is parked.
It waits for the launch button to be released before accepting game input, and
ignores simultaneous opposing directions while preserving diagonals. The D-pad
remains rotated for landscape play. OK maps to fire/enter, BACK to use, and the
OK+BACK chord opens the game menu.

## Verification

Run `node --test tools/highboy/tests/test_doom_dma.mjs` from the repository root.
The host checks cover allocation fallback, RGB565 byte order, frame/letterbox
strip coverage and draw/wait ordering, plus retained input and lock ordering.
They do not measure actual panel DMA completion, frame rate, buttons or audio;
those need a later device smoke test. No firmware build or flash is performed.

## Central scheduling policy

Both the game and button-input tasks use `SYS_PRIO_SERVICE_HI` on
`SYS_CORE_UI`. The input task no longer borrows the renderer-only priority band.
Task stacks retain their explicit PSRAM capabilities. Host strip tests validate
DMA buffer ownership and conversion; input latency still needs device testing.
