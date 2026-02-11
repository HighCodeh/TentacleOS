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
6.  Initializes the backlight PWM and sets it to 80%.

### `lcd_set_brightness`
```c
void lcd_set_brightness(uint8_t percent);
```
Sets the backlight brightness percentage (0-100%).
- **Implementation:** Uses LEDC Timer 0, Channel 0 with 13-bit resolution.

## Global Handles
- `panel_handle`: Handle to the abstract LCD panel.
- `io_handle`: Handle to the underlying IO interface.
