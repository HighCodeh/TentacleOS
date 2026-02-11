# LVGL Port Component Documentation

This component implements the **porting layer** required to run the **LVGL v9** graphics library on the Highboy hardware. It connects the generic LVGL engine with the specific drivers for the display (ST7789 via ESP-LCD) and input devices (GPIO Buttons).

## Overview

- **Location:** `components/Service/lvgl_port/`
- **Main Headers:** 
    - `include/lv_port_disp.h` (Display)
    - `include/lv_port_indev.h` (Input Device)
- **Dependencies:** `lvgl`, `esp_lcd`, `st7789`, `buttons_gpio`

The port is divided into two main parts:
1.  **Display Port (`lv_port_disp`):** Handles rendering, buffers, and flushing pixels to the screen using DMA.
2.  **Input Port (`lv_port_indev`):** Maps physical GPIO buttons to LVGL logical keys (Keypad) for UI navigation.

---

## Display Port (`lv_port_disp`)

This module configures the LVGL display driver to work with the ST7789 controller using the `esp_lcd` component.

### Initialization

#### `lv_port_disp_init`
```c
void lv_port_disp_init(void);
```
Initializes the display interface for LVGL.
1.  **Display Creation:** Creates an LVGL display object with resolutions defined by `LCD_H_RES` and `LCD_V_RES`.
2.  **Callback Registration:** Sets `disp_flush` as the flush callback.
3.  **Buffer Allocation:** Allocates two buffers (Double Buffering) in DMA-capable internal memory.
    -   **Buffer Size:** `1/5` of the screen height (configurable via `LVGL_BUF_PIXELS`).
4.  **DMA Synchronization:** Registers an `on_color_trans_done` callback with `esp_lcd` to notify LVGL when the DMA transfer is complete (`lv_display_flush_ready`).

### Internal Callbacks

#### `disp_flush`
Called by LVGL when it wants to render a part of the screen.
-   Swaps color bytes (RGB565 big-endian to little-endian) using `lv_draw_sw_rgb565_swap`.
-   Calls `esp_lcd_panel_draw_bitmap` to send data to the display controller via SPI DMA.

#### `notify_lvgl_flush_ready`
Called by the ESP-LCD driver (ISR context) when the DMA transfer finishes. It calls `lv_display_flush_ready()` to tell LVGL it can render the next frame.

---

## Input Port (`lv_port_indev`)

This module integrates the physical buttons of the Highboy device as a "Keypad" input device for LVGL, enabling navigation through groups and widgets.

### Initialization

#### `lv_port_indev_init`
```c
void lv_port_indev_init(void);
```
Initializes the input subsystem.
1.  **Device Creation:** Creates an `lv_indev_t` of type `LV_INDEV_TYPE_KEYPAD`.
2.  **Callback Registration:** Sets `keypad_read` as the function to poll button states.
3.  **Group Management:**
    -   Creates a default `lv_group_t` (`main_group`) for focus management.
    -   Associates the keypad input device with this group.

### Global Variables
-   `indev_keypad`: Pointer to the created input device.
-   `main_group`: Pointer to the main navigation group. New widgets added to this group can be controlled via buttons.

### Key Mapping (`keypad_get_key`)

The port maps physical button states (from `buttons_gpio.h`) to LVGL logical keys:

| Physical Button | LVGL Key | Function |
| :--- | :--- | :--- |
| **Up Button** | `LV_KEY_PREV` | Focus previous item |
| **Down Button** | `LV_KEY_NEXT` | Focus next item |
| **OK Button** | `LV_KEY_ENTER` | Click/Select |
| **Back Button** | `LV_KEY_ESC` | Back/Close |
| **Left Button** | `LV_KEY_LEFT` | Decrease value / Move Left |
| **Right Button** | `LV_KEY_RIGHT` | Increase value / Move Right |

### Internal Logic
The `keypad_read` function is called periodically by LVGL. It polls the hardware buttons and updates the `data->state` and `data->key`. It implements a simple state machine where the last pressed key is remembered until all keys are released.
