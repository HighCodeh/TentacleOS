# ST7789 Display Driver

This component initializes and manages the ST7789 LCD controller using the ESP-IDF `esp_lcd` component. It handles the SPI interface configuration and the display initialization sequence.

## Overview

- **Location:** `components/Drivers/st7789/`
- **Header:** `include/st7789.h`
- **Dependencies:** `esp_lcd`, `driver/gpio`, `driver/ledc`, `spi`

## Hardware Configuration
- **Resolution:** 240x240
- **Color Depth:** 16-bit (RGB565)
- **Interface:** SPI (via `spi` component driver)

## Internal Backlight Control
Although a separate `backlight` component exists, this driver currently includes its own internal PWM initialization (`init_backlight_pwm`) and control logic using `LEDC_TIMER_0` / `LEDC_CHANNEL_0`.
*Note: This overlaps with the standalone `backlight` component. Verify project integration to avoid timer conflicts.*

## API Reference

### `st7789_init`
```c
void st7789_init(void);
```
Initializes the display.
1.  Creates the SPI device interface on `SPI3_HOST`.
2.  Configures the ST7789 panel (Reset pin, RGB order, etc.).
3.  Resets and initializes the panel.
4.  Inverts colors (standard for many ST7789 IPS panels).
5.  Turns the display ON.
6.  Initializes the backlight PWM and **applies** (does not re-save) the saved
    brightness/rotation.

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
