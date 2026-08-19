# BadUSB Application

This component implements a modular HID injection tool capable of emulating keyboard and mouse input to execute automated payloads. It features a 3-layer architecture that decouples script parsing, keyboard layouts, and hardware transport.

## Overview

- **Location:** `components/Applications/bad_usb/`
- **Dependencies:** `tinyusb`, `tusb_desc`, `storage_api`, `freertos`
- **Transport:** USB HID via TinyUSB (Bluetooth planned)

## Architecture

```
┌─────────────────────────────────────────────────┐
│               BadUSB Application                │
│                                                 │
│  ┌─────────────────────────────────────────┐    │
│  │        DuckyScript Parser               │    │
│  │  (ducky_parser.c)                       │    │
│  │  Parses scripts, dispatches commands    │    │
│  └────────┬──────────────┬─────────────────┘    │
│           │              │                      │
│  ┌────────┴────────┐  ┌─┴──────────────────┐   │
│  │  HID Layouts    │  │  HID HAL           │   │
│  │  (hid_layouts)  │  │  (hid_hal)         │   │
│  │  US / ABNT2     │  │  Callback-based    │   │
│  │  char -> HID    │  │  abstraction       │   │
│  └────────┬────────┘  └─┬──────────────────┘   │
│           │              │                      │
│           └──────┬───────┘                      │
│                  │                              │
│  ┌───────────────┴─────────────────────────┐    │
│  │        Transport Backend                │    │
│  │  USB: bad_usb.c (TinyUSB)              │    │
│  │  BLE: (planned)                         │    │
│  └─────────────────────────────────────────┘    │
└─────────────────────────────────────────────────┘
```

**Layer 1 - HAL (`hid_hal`):** Manages the registration of transport drivers and provides a common interface for sending key reports, mouse movements, and waiting for connections. The parser never calls USB directly.

**Layer 2 - Layouts (`hid_layouts`):** Translates characters and strings into HID keycodes. Hardware-independent and reusable by any transport registered in the HAL.

**Layer 3 - Parser (`ducky_parser`):** Processes DuckyScript files and calls the HAL/Layout functions to execute commands.

## API Reference

### BadUSB Driver (`bad_usb.h`)

```c
esp_err_t bad_usb_init(void);
esp_err_t bad_usb_deinit(void);
void bad_usb_wait_for_connection(void);
bool bad_usb_wait_for_connection_ex(bad_usb_abort_cb_t should_abort);
```
- `bad_usb_init` initializes TinyUSB (via `busb_init`) and registers USB HID callbacks (keyboard, mouse, wait, report-ready) into the HAL.
- `bad_usb_deinit` unregisters callbacks and uninstalls the TinyUSB driver.
- `bad_usb_wait_for_connection` blocks until the USB host mounts the device, then waits 2 seconds (`USB_SETTLE_DELAY_MS`) for the host OS to enumerate.
- `bad_usb_wait_for_connection_ex` is the abortable variant. It polls `tud_mounted()` with an **8 second mount timeout** (`USB_MOUNT_TIMEOUT_MS`), calling `should_abort` (a `bad_usb_abort_cb_t`) between polls and during the post-mount settle. It returns `true` if the host mounted and settled, `false` if the timeout elapsed or the abort predicate fired. `bad_usb_wait_for_connection` is just `_ex(NULL)`, which never times out on the abort path but still stops after the mount timeout. Use `_ex` so a run waiting for a host that never arrives can be cancelled.

### HID HAL (`hid_hal.h`)

```c
void hid_hal_register_callback(hid_send_cb_t send_cb,
                               hid_mouse_cb_t mouse_cb,
                               hid_wait_cb_t wait_cb,
                               hid_ready_cb_t ready_cb);
void hid_hal_press_key(uint8_t keycode, uint8_t modifiers);
void hid_hal_mouse_move(int8_t x, int8_t y);
void hid_hal_mouse_click(uint8_t buttons);
void hid_hal_mouse_scroll(int8_t wheel);
void hid_hal_wait_for_connection(void);
```
- Reports are paced by a report-readiness callback (`ready_cb`), not fixed delays. Before and after each report the HAL calls `wait_report_ready`, which spins on `tud_hid_ready()` (yielding with `vTaskDelay(1)` between checks) until the previous report has been delivered to the host, so an unpolled report is never overwritten (which would drop keys). This paces output to the host's ~1 ms poll rate.
- The wait is capped by `REPORT_READY_TIMEOUT_MS` (1000 ms): it must exceed how long a busy host/editor can stall HID polling, so only a real disconnect hits it. Raising it to 1 s is what keeps keys (e.g. a leading `HOME`) from being dropped when the host stalls.
- If no `ready_cb` is registered, the HAL falls back to fixed `ets_delay_us` delays (`FALLBACK_KEY_DELAY_US` = 5000 us for keys, `FALLBACK_MOUSE_DELAY_US` = 2000 us for mouse).

### Keyboard Layouts (`hid_layouts.h`)

```c
void hid_layouts_type_string_us(const char *str);
void hid_layouts_type_string_abnt2(const char *str);
```
- `hid_layouts_type_string_us` maps ASCII characters to US keyboard HID keycodes.
- `hid_layouts_type_string_abnt2` types the Brazilian Portuguese (ABNT2) layout with full accented-character coverage, not just a few dead-key accents. It decodes UTF-8 two-byte sequences via a lookup table (`ABNT2_UTF8_MAP`) that covers:
  - Accented vowels **a e i o u**, lowercase and uppercase, in all five diacritics: acute, grave, circumflex, tilde, and diaeresis (each emitted as the ABNT2 dead key + base letter, with Shift for uppercase).
  - **c-cedilha** (c) and **n-tilde** (n), both lowercase and uppercase.
  - AltGr (RightAlt) symbols: cent, pound, section, not-sign, and superscripts 1 / 2 / 3.
  - Bracket/brace/backslash/pipe (`[ ] { } \ |`) via RightAlt (with Shift for the brace/pipe forms).
  - The symbols that a naive US mapping gets wrong on ABNT2 hardware are fixed: `^` and `~` are typed as dead key + space to emit the literal, and `<` / `>` use the correct ABNT2 keys. Apostrophe / double-quote / backtick are also remapped to their ABNT2 positions.

### DuckyScript Parser (`ducky_parser.h`)

```c
void ducky_set_output_mode(ducky_output_mode_t mode);
void ducky_set_layout(ducky_layout_t layout);
void ducky_set_progress_callback(ducky_progress_cb_t cb);
void ducky_parse_and_run(const char *script);
esp_err_t ducky_run_from_assets(const char *filename);
esp_err_t ducky_run_from_sdcard(const char *path);
void ducky_abort(void);
```
- `ducky_parse_and_run` executes a script line-by-line with 20 ms inter-line delay.
- `ducky_run_from_assets` loads a script from the internal flash asset partition.
- `ducky_run_from_sdcard` loads a script from the SD card (max 8 KB).
- `ducky_abort` sets a flag that stops execution at the next line boundary.
- Progress callback is invoked after each line with current/total counts.

## Supported DuckyScript Commands

| Command | Arguments | Description |
|---------|-----------|-------------|
| `REM` | [comment] | Comment line (ignored) |
| `DELAY` | [ms] | Pause execution for N milliseconds |
| `STRING` | [text] | Type text using the active keyboard layout |
| `ENTER` / `RETURN` | - | Press Enter |
| `GUI` / `WINDOWS` / `SUPER` / `COMMAND` | [key] | GUI (Windows/Command/Super) modifier, optionally with a key |
| `CTRL` / `CONTROL` | [key] | Control + key |
| `SHIFT` | [key] | Shift + key |
| `ALT` | [key] | Alt + key |
| `TAB` | - | Tab key |
| `ESC` / `ESCAPE` | - | Escape key |
| `SPACE` | - | Space bar |
| `BACKSPACE` | - | Backspace |
| `F1` - `F12` | - | Function keys |
| `UP` / `DOWN` / `LEFT` / `RIGHT` (and `UPARROW` / `DOWNARROW` / `LEFTARROW` / `RIGHTARROW`) | - | Arrow keys |
| `HOME` / `END` / `INSERT` / `DELETE` | - | Navigation keys |
| `PAGEUP` / `PAGEDOWN` | - | Page navigation |
| `CAPSLOCK` / `NUMLOCK` / `SCROLLLOCK` | - | Lock keys |
| `PRINTSCREEN` / `PAUSE` / `APP` / `MENU` | - | Special system keys (`APP` and `MENU` both map to the Application key) |
| `MOUSE_MOVE` | [x] [y] | Move mouse relative (-127 to 127) |
| `MOUSE_CLICK` / `LCLICK` | - | Left mouse click |
| `MOUSE_RIGHT_CLICK` / `RCLICK` | - | Right mouse click |
| `MOUSE_SCROLL` | [amount] | Scroll mouse wheel |

Modifier keys can be combined: `CTRL SHIFT ESC`, `GUI r`, `ALT F4`.

## Supported Layouts

| Layout | Enum | Notes |
|--------|------|-------|
| US (QWERTY) | `DUCKY_LAYOUT_US` | Default. Standard ASCII mapping. |
| ABNT2 (Brazil) | `DUCKY_LAYOUT_ABNT2` | Full accented-character coverage (see the layouts API above), remapped punctuation, AltGr symbols. |

## Payload Sources

Scripts run either from the internal flash asset partition (`ducky_run_from_assets`) or from the SD card (`ducky_run_from_sdcard`, max 8 KB per script). In the UI menu, SD payloads are scanned **recursively across subfolders** and listed together in a single view, so scripts organised into folders on the card are all discoverable.

Example payloads ship in the asset partition under `firmware_p4/assets/storage/bad_usb_scripts/`:

- `rickroll.txt`
- `amiga.txt`

