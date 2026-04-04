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
```
- `bad_usb_init` initializes TinyUSB and registers USB HID callbacks into the HAL.
- `bad_usb_deinit` unregisters callbacks and uninstalls the TinyUSB driver.
- `bad_usb_wait_for_connection` blocks until the USB host mounts the device, then waits 2 seconds for enumeration.

### HID HAL (`hid_hal.h`)

```c
void hid_hal_register_callback(hid_send_cb_t send_cb,
                               hid_mouse_cb_t mouse_cb,
                               hid_wait_cb_t wait_cb);
void hid_hal_press_key(uint8_t keycode, uint8_t modifiers);
void hid_hal_mouse_move(int8_t x, int8_t y);
void hid_hal_mouse_click(uint8_t buttons);
void hid_hal_mouse_scroll(int8_t wheel);
void hid_hal_wait_for_connection(void);
```
- `hid_hal_press_key` sends a key-down + key-up report with ~5 ms per phase.
- Mouse functions use ~2 ms delay for moves and ~5 ms for clicks.
- All functions yield to the scheduler (`vTaskDelay(0)`) to prevent WDT starvation.

### Keyboard Layouts (`hid_layouts.h`)

```c
void type_string_us(const char *str);
void type_string_abnt2(const char *str);
```
- `type_string_us` maps ASCII characters to US keyboard HID keycodes.
- `type_string_abnt2` handles Brazilian Portuguese layout including UTF-8 dead-key sequences for accented characters (e.g. a, e, c, a, o).

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
| `GUI` / `WINDOWS` / `COMMAND` | [key] | Windows/Command key (optionally with a key) |
| `CTRL` / `CONTROL` | [key] | Control + key |
| `SHIFT` | [key] | Shift + key |
| `ALT` | [key] | Alt + key |
| `TAB` | - | Tab key |
| `ESC` / `ESCAPE` | - | Escape key |
| `F1` - `F12` | - | Function keys |
| `UP` / `DOWN` / `LEFT` / `RIGHT` | - | Arrow keys |
| `HOME` / `END` / `INSERT` / `DELETE` | - | Navigation keys |
| `PAGEUP` / `PAGEDOWN` | - | Page navigation |
| `CAPSLOCK` / `NUMLOCK` / `SCROLLLOCK` | - | Lock keys |
| `PRINTSCREEN` / `PAUSE` / `APP` / `MENU` | - | Special system keys |
| `MOUSE_MOVE` | [x] [y] | Move mouse relative (-127 to 127) |
| `MOUSE_CLICK` / `LCLICK` | - | Left mouse click |
| `MOUSE_RIGHT_CLICK` / `RCLICK` | - | Right mouse click |
| `MOUSE_SCROLL` | [amount] | Scroll mouse wheel |

Modifier keys can be combined: `CTRL SHIFT ESC`, `GUI r`, `ALT F4`.

## Supported Layouts

| Layout | Enum | Notes |
|--------|------|-------|
| US (QWERTY) | `DUCKY_LAYOUT_US` | Default. Standard ASCII mapping. |
| ABNT2 (Brazil) | `DUCKY_LAYOUT_ABNT2` | Dead-key accent support, remapped punctuation. |
