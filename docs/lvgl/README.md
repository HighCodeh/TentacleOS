# LVGL Service Documentation

This component integrates the **LVGL v9** graphics library with the Highboy
hardware: the ST7789 display (via ESP-LCD) and the GPIO buttons. It is the
single home for all LVGL setup on the P4.

## Overview

- **Location:** `components/Service/lvgl/`
- **Main headers:**
    - `include/lvgl_glue.h` (core + display + lock)
    - `include/lv_port_indev.h` (input device)
- **Dependencies:** `lvgl`, `esp_lvgl_port`, `esp_lcd`, `st7789`, `buttons_gpio`

The service has two parts:
1. **Core + display (`lvgl_glue`):** brings LVGL up over Espressif's
   `esp_lvgl_port` and registers the ST7789 display.
2. **Input (`lv_port_indev`):** maps the physical GPIO buttons to LVGL logical
   keys (Keypad) for UI navigation.

> **Note:** the old hand-written display port (`lv_port_disp`) was removed. The
> display is now driven through `esp_lvgl_port` via `lvgl_glue`, so there is a
> single display path. `esp_lvgl_port` owns the LVGL task, the tick source and
> the thread-safety lock; we no longer manage those by hand.

> **Scheduling:** the LVGL render task runs at `SYS_PRIO_RENDER` (6) pinned to
> `SYS_CORE_UI` (core 1), set in `lvgl_glue_init` from
> [`sys_prio.h`](../sys_prio/README.md). Radios and services live on core 0 so
> they cannot stall rendering.

### Bring-up order

```c
st7789_init();        // creates the esp_lcd panel handles (io_handle/panel_handle)
lvgl_glue_init();     // lv_init + LVGL task + tick + display registration
lv_port_indev_init(); // keypad indev + navigation group (under the glue lock)
ui_init();            // build the UI (all LVGL access goes through the glue lock)
```

---

## Core + Display (`lvgl_glue`)

Thin wrapper over `esp_lvgl_port`. It configures the port for this board and
exposes a small API the rest of the firmware uses.

### `lvgl_glue_init`
```c
esp_err_t lvgl_glue_init(void);
```
1. **Requires `st7789_init()` first** - it reads the panel handles (`io_handle`,
   `panel_handle`) the driver publishes; returns `ESP_ERR_INVALID_STATE` if they
   are not ready.
2. **`lvgl_port_init`:** starts the managed LVGL task (calls `lv_init`
   internally), the periodic tick, and a recursive mutex (the lock).
3. **`lvgl_port_add_disp`:** registers the display - a partial DMA double buffer
   of `LVGL_BUF_LINES` (`LCD_PANEL_H / 4` = 80) lines in internal DMA-capable
   RAM, RGB565 with byte swap.
4. **PSRAM draw buffers:** overrides LVGL's draw and image buffer handlers with
   `draw_buf_psram_malloc`, which routes allocations larger than
   `DRAWBUF_PSRAM_THRESHOLD` (48 KB) to PSRAM (`MALLOC_CAP_SPIRAM`) and falls
   back to the default heap otherwise. This keeps big off-screen / image buffers
   off the scarce internal heap. The DMA render double buffer above stays in
   internal DMA RAM (PSRAM is not DMA-reachable by the LCD).

### Thread safety
```c
bool lvgl_glue_lock(int timeout_ms); // -1 = wait forever
void lvgl_glue_unlock(void);
```
Any task that touches LVGL (UI, screen capture, etc.) must hold this lock. It is
the `esp_lvgl_port` recursive mutex; `ui_manager`'s `ui_acquire`/`ui_release`
wrap it.

> **Do not pass `-1` from application code.** `ui_acquire()` uses a finite
> 1000 ms timeout on purpose: a task that grabs the lock and never returns can no
> longer freeze every other UI caller forever. `ui_acquire()` returns `false` on
> timeout (and logs a warning); **every callsite must check the return** and skip
> its work when it is `false`.

> **Never hold the lock across long or blocking work, and never run a busy loop
> on the UI thread.** A held lock stops the LVGL task from rendering, which
> stalls the render-progress beat (`ui_render_beat()`) that `sys_monitor` polls;
> a stalled beat triggers a controlled restart. A busy loop that spins a core
> past 5 s also trips the Task Watchdog panic (`CONFIG_ESP_TASK_WDT_PANIC=y`).
> Offload long work to its own task (the SubGhz receiver is the reference) and
> push results back to the screen with an `lv_timer` or `lv_async_call`, which
> run inside the LVGL task already under the lock. See
> [ui: long-running work](../ui/README.md#long-running-work-and-the-watchdog).

### Direct draw (panel takeover)
```c
void lvgl_glue_direct_begin(void);
void lvgl_glue_direct_end(void);
void lvgl_glue_wait_flush(uint32_t timeout_ms);
```
For a full-screen app (e.g. the Game Boy emulator) that holds the LVGL lock and
drives the ST7789 itself with raw `esp_lcd_panel_draw_bitmap()` calls. The panel
SPI is async - `draw_bitmap` queues the transfer and returns - so reusing one
blit buffer for the next strip while the previous DMA is still reading it
corrupts the image. Between `_begin()` and `_end()` the shared
color-transfer-done ISR raises a semaphore instead of signalling LVGL flush-ready;
the app calls `lvgl_glue_wait_flush()` after each draw to block until that
strip's DMA finished, making single-buffer reuse safe. Call `_begin()` right
after taking the lock and `_end()` before releasing it; outside this window the
ISR drives LVGL exactly as before.

### Screenshot capture
```c
typedef void (*lvgl_glue_strip_cb_t)(
    int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t *data, int32_t stride);
void lvgl_glue_capture_begin(lvgl_glue_strip_cb_t cb);
void lvgl_glue_capture_end(void);
```
Tees each rendered strip to `cb` from the LVGL flush path, handing over the
strip's area plus the active buffer in native RGB565 (pre byte-swap).
`_capture_begin(cb)` starts the tee (pass `NULL` to disable); `_capture_end()`
stops it.

### Rotation
```c
bool lvgl_glue_toggle_rotation(void); // returns true if now landscape
bool lvgl_glue_is_landscape(void);
```
Toggles between portrait (0deg) and landscape (270deg) and invalidates the
active screen. The input port reads `lvgl_glue_is_landscape()` to remap the
arrow keys accordingly.

### Status
```c
bool lvgl_glue_is_ready(void); // true once lvgl_glue_init succeeded
```

---

## Input (`lv_port_indev`)

Integrates the physical buttons as an LVGL "Keypad" input device, enabling
navigation through groups and widgets.

### `lv_port_indev_init`
```c
void lv_port_indev_init(void);
```
1. Acquires the LVGL lock via `lvgl_glue_lock` (so it is safe to call after
   `lvgl_glue_init`).
2. Creates the default navigation group (`main_group`) and sets it as default.
3. Creates an `LV_INDEV_TYPE_KEYPAD` device with `keypad_read` as its read
   callback, bound to `main_group`.

### Global variables
- `indev_keypad`: the created input device.
- `main_group`: the main navigation group. Widgets added to it are button-driven.

### Key mapping

Physical button states (from `buttons_gpio.h`) map to LVGL logical keys. In
landscape the up/down/left/right are remapped to match the rotated screen.

| Physical Button | LVGL Key | Function |
| :--- | :--- | :--- |
| **Up** | `LV_KEY_PREV` | Focus previous item |
| **Down** | `LV_KEY_NEXT` | Focus next item |
| **OK** | `LV_KEY_ENTER` | Click/Select |
| **Back** | `LV_KEY_ESC` | Back/Close |
| **Left** | `LV_KEY_LEFT` | Decrease value / move left |
| **Right** | `LV_KEY_RIGHT` | Increase value / move right |

### Internal logic
`keypad_read` is polled periodically by the LVGL task. It reads the hardware
buttons and updates `data->state`/`data->key`, remembering the last pressed key
until all keys are released.
