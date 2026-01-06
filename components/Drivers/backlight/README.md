# Backlight Driver

This component manages the display backlight brightness using the ESP32's LEDC (LED Control) peripheral to generate a PWM signal.

## Overview

- **Location:** `components/Drivers/backlight/`
- **Header:** `include/backlight.h`
- **Dependencies:** `driver/ledc`, `st7789` (for pin definition)

## Configuration

- **Timer:** `LEDC_TIMER_1`
- **Mode:** `LEDC_LOW_SPEED_MODE`
- **Channel:** `LEDC_CHANNEL_1`
- **Resolution:** 8-bit (0-255 duty cycle)
- **Frequency:** 5 kHz

## API Reference

### `backlight_init`
```c
void backlight_init(void);
```
Initializes the LEDC timer and channel. It sets the initial brightness to maximum (255) and synchronizes the internal state.

### `backlight_set_brightness`
```c
void backlight_set_brightness(uint8_t brightness);
```
Sets the backlight brightness.
- **Parameters:** `brightness` (0-255). 0 is off, 255 is full brightness.
- **Note:** Updates both the internal state variable and the hardware PWM duty cycle.

### `backlight_get_brightness`
```c
uint8_t backlight_get_brightness(void);
```
Returns the current brightness level stored in the internal state.
