# BadUSB Application Component

This component implements a modular HID application capable of emulating keyboard and mouse input to execute automated payloads. It features a 3-layer architecture that decouples script parsing, keyboard layouts, and hardware transport.

## Overview

- **Location:** `components/Applications/bad_usb/`
- **Architecture Layers:**
    - **HID HAL:** `include/hid_hal.h` (Hardware Abstraction Layer)
    - **HID Layouts:** `include/hid_layouts.h` (US and ABNT2 translation)
    - **Scripting Engine:** `include/ducky_parser.h` (DuckyScript parser)
    - **USB Driver:** `include/bad_usb.h` (TinyUSB backend)

## Architecture

The component is organized into three distinct layers:

1.  **HAL Layer (hid_hal):** Manages the registration of hardware drivers (USB, etc.) and provides a common interface for sending key reports, mouse movements, and waiting for connections.
2.  **Layout Layer (hid_layouts):** Translates characters and strings into HID keycodes. This layer is hardware-independent and can be reused by any driver registered in the HAL.
3.  **Parser Layer (ducky_parser):** Processes DuckyScript files and calls the HAL or Layout functions to execute commands.

## API Reference

### HID HAL (hid_hal.h)

#### `hid_hal_register_callback`
```c
void hid_hal_register_callback(hid_send_callback_t send_cb, hid_mouse_callback_t mouse_cb, hid_wait_callback_t wait_cb);
```
Registers the functions that handle actual data transmission and connection waiting.

#### `hid_hal_press_key`
```c
void hid_hal_press_key(uint8_t keycode, uint8_t modifiers);
```
Presses and releases a key using the registered driver (approx 10ms cycle).

#### `hid_hal_mouse_move`
```c
void hid_hal_mouse_move(int8_t x, int8_t y);
```
Moves the mouse cursor relative to its current position.

#### `hid_hal_mouse_click`
```c
void hid_hal_mouse_click(uint8_t buttons);
```
Performs a mouse click (press and release).

#### `hid_hal_mouse_scroll`
```c
void hid_hal_mouse_scroll(int8_t wheel);
```
Scrolls the mouse wheel.

#### `hid_hal_wait_for_connection`
```c
void hid_hal_wait_for_connection(void);
```
Blocks execution until the registered driver reports a successful connection.

### Keyboard Layouts (hid_layouts.h)

#### `type_string_us` / `type_string_abnt2`
```c
void type_string_us(const char* str);
void type_string_abnt2(const char* str);
```
Types a full string using the specified layout logic.

### USB Driver (bad_usb.h)

#### `bad_usb_init` / `bad_usb_deinit`
```c
void bad_usb_init(void);
void bad_usb_deinit(void);
```
Initializes the TinyUSB stack and registers the USB driver into the HID HAL.

### DuckyScript Parser (ducky_parser.h)

#### `ducky_parse_and_run`
```c
void ducky_parse_and_run(const char *script);
```
Executes a DuckyScript string line-by-line.

#### `ducky_run_from_assets`
```c
esp_err_t ducky_run_from_assets(const char *filename);
```
Loads and runs a script file from the internal storage.

## Supported DuckyScript Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `REM` | [Comment] | Comment line (ignored). |
| `DELAY` | [Milliseconds] | Pauses execution for the specified time. |
| `STRING` | [Text] | Types the following text using the active layout. |
| `ENTER` | - | Presses the Enter key. |
| `GUI` / `WINDOWS` | [Key] | Presses the Windows/Command key (optionally with a key). |
| `CTRL` | [Key] | Presses Control + Key. |
| `SHIFT` | [Key] | Presses Shift + Key. |
| `ALT` | [Key] | Presses Alt + Key. |
| `TAB` | - | Presses Tab. |
| `ESC` | - | Presses Escape. |
| `F1` - `F12` | - | Function keys. |
| `UP` / `DOWN` / `LEFT` / `RIGHT` | - | Arrow keys. |
| `HOME` / `END` / `INSERT` / `DELETE` | - | Navigation keys. |
| `PAGEUP` / `PAGEDOWN` | - | Page navigation. |
| `CAPSLOCK` / `NUMLOCK` / `SCROLLLOCK` | - | Lock keys. |
| `PRINTSCREEN` / `PAUSE` / `APP` | - | Special system keys. |
| `MOUSE_MOVE` | [x] [y] | Moves mouse relative to current position (-127 to 127). |
| `MOUSE_CLICK` / `LCLICK` | - | Clicks the left mouse button. |
| `MOUSE_RIGHT_CLICK` / `RCLICK` | - | Clicks the right mouse button. |
| `MOUSE_SCROLL` | [amount] | Scrolls the mouse wheel. |

## Implementation Details

- **Decoupling:** By using the HAL, the parser does not need to know if the target is connected via USB or other means.
- **ABNT2 Support:** Includes complex dead-key logic for Brazilian Portuguese characters like accented vowels and the "ç".
- **Performance:** Optimized for speed using `ets_delay_us` for minimal latency between keystrokes (approx 4-5ms per key press/release cycle).
