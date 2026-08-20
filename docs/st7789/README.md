# ST7789 Display Driver

This component initializes and manages the ST7789 LCD controller using the ESP-IDF `esp_lcd` component. It handles the SPI interface configuration and the display initialization sequence.

## Overview

- **Location:** `components/Drivers/st7789/`
- **Header:** `include/st7789.h`
- **Dependencies:** `esp_lcd`, `driver/gpio`, `driver/ledc`, `spi`

## Hardware Configuration
- **Resolution:** 240x320 (`LCD_H_RES` 240 / `LCD_V_RES` 320; `LCD_PANEL_W`/`LCD_PANEL_H` match).
- **Color Depth:** 16-bit (RGB565)
- **Interface:** SPI on `SPI3_HOST`, 20 MHz pixel clock (`LCD_PIXEL_CLOCK_HZ`).

## SPI drive-strength hardening
`st7789_init` bumps the SPI3 `SCLK`/`MOSI` pins to `LCD_SPI_DRIVE_CAP`
(`GPIO_DRIVE_CAP_3`, the strongest) via `gpio_set_drive_capability`. The display
FFC is long, capacitive and unterminated: at the IDF default the 20 MHz edges
barely settle, so a radio's EMI corrupts the still-settling edge and garbles long
transfers. Do not lower this - a weaker cap starves the FFC so hard the panel
will not even init.

## Internal Backlight Control
This driver owns its own PWM backlight init (`init_backlight_pwm`) and control
logic using `LEDC_TIMER_0` / `LEDC_CHANNEL_0`, 13-bit resolution at 5 kHz on
`GPIO_ST7789_BL_PIN`.

## API Reference

### `st7789_init`
```c
esp_err_t st7789_init(void);
```
Initializes the display. Returns `ESP_OK`, or the failing `esp_err_t` from the
panel IO / panel create / reset / init step (cleaning up any handles it created).
1.  Creates the SPI panel IO on `SPI3_HOST`.
2.  Hardens the SPI3 `SCLK`/`MOSI` drive strength (see above).
3.  Configures the ST7789 panel (Reset pin, RGB order, etc.).
4.  Resets and initializes the panel.
5.  Inverts colors (standard for many ST7789 IPS panels).
6.  Turns the display ON.
7.  Initializes the backlight PWM and applies the saved brightness/rotation.

### `lcd_apply_brightness`
```c
void lcd_apply_brightness(uint8_t percent);
```
Applies a backlight duty (0-100%) **without persisting**. Use for transient
changes such as the auto-dim fade in `power_policy`, and for the live preview
while the user drags the brightness bar. Uses LEDC Timer 0, Channel 0, 13-bit.

### `lcd_set_brightness`
```c
void lcd_set_brightness(uint8_t percent);
```
Applies the brightness **and persists** it. Kept for callers that want the old
apply+save behaviour in one call; the display settings screen instead saves
through `tos_config` (see below).

### `lcd_get_brightness`
```c
uint8_t lcd_get_brightness(void);
```
Reads the persisted brightness back from the config file.

### `lcd_set_rotation`
```c
void lcd_set_rotation(uint8_t rotation);
```
Sets the panel rotation (index `1`-`4`, clamped). Applies the matching
mirror / swap-xy / gap for the 240x320 panel and persists the value. Rotations
3 and 4 apply a `ROTATION_GAP_OFFSET` (80) to line the visible window up.

### `lcd_get_rotation`
```c
uint8_t lcd_get_rotation(void);
```
Reads the persisted rotation index (1-4) back from the config file.

### `lcd_display_sleep`
```c
void lcd_display_sleep(bool sleep);
```
Turns the panel off (`sleep = true`, sleep-in / display-off) or on. Pair with
the backlight: the panel command cuts the pixels, the backlight cuts the light.
`power_policy` calls this once the auto-dim fade reaches zero.

## Config file ownership

`st7789` and `tos_config` both address the same file (`FLASH_CONFIG_SCREEN`).
To avoid one clobbering the other's fields, **`tos_config` is the sole writer**
of the `screen` schema (brightness, rotation, theme, `auto_lock_seconds`,
`auto_dim`). `st7789_init` only **reads and applies** it; it never re-saves, so
the auto-lock / auto-dim / theme fields survive a display init. The display
settings screen writes via `tos_config_save(FLASH_CONFIG_SCREEN, "screen")`.

## Global Handles
- `panel_handle`: Handle to the abstract LCD panel.
- `io_handle`: Handle to the underlying IO interface.
