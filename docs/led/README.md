# LED Status Driver

This component drives the single RGB status LED. The two firmwares use different
hardware, so the driver differs between them: on P4 the LED is a Texas Instruments
**LP5816** I2C current-sink driver, while on C5 it is an addressable RMT LED strip
(single pixel). The P4 build additionally exposes a semantic signal API
(`led_signal_info/warning/error`) with a dedicated blink-off task; the C5 build
only exposes the fixed-color blink helpers.

# P4

RGB status LED backed by the LP5816 4-channel I2C current-sink driver.

## Overview

- **Location:** `components/Drivers/led/`
- **Header:** `include/led_control.h`
- **Chip:** Texas Instruments LP5816 (U22), 4-channel I2C current-sink LED driver
- **Interface:** I2C (shared bus via the `i2c_init` component, `i2c_get_bus()`)
- **I2C address:** `0x2C` (7-bit)
- **I2C speed:** `I2C_MASTER_FREQ_HZ` (100 kHz, from `i2c_init.h`)
- **I2C timeout:** 50 ms per transfer
- **Dependencies:** `i2c_init`, `sys_prio`, `esp_timer`, `freertos`

## Hardware

The LEDs are common-anode (anode to 3.3V); the LP5816 sinks each cathode. Channel
map (schematic sheet 6):

| Channel | Color | LED |
| :--- | :--- | :--- |
| OUT0 | Red | D10.1 |
| OUT1 | Green | D10.2 |
| OUT2 | Blue | D10.3 |
| OUT3 | (unused) | D11 |

Only OUT0..OUT2 (RGB) are enabled. Channels run in manual 8-bit PWM mode with the
dot-current ceiling set to full scale (25.5 mA MAX_CURRENT) for saturated color;
fade and exponential dimming are disabled so PWM updates take effect immediately.

## Configuration / Tunables

- `LP5816_ADDR` = `0x2C`: I2C target address.
- `I2C_TIMEOUT_MS` = `50`: per-transfer timeout.
- `OUT_ENABLE_RGB` = `0x07`: enables OUT0..OUT2.
- `LED_DC_LEVEL` = `0xFF`: per-channel dot-current ceiling (full scale).
- `SIGNAL_BLINK_US` = `250000`: semantic-signal flash duration (250 ms).
- Signal defaults: info `0xFF00FF`, warning `0xFFFF00`, error `0xFF0000`,
  brightness `10`. These are overridden via `led_set_signal_config()`.

## Blink-off Task

Turning the LED off is a blocking I2C write, so it does not run in an
`esp_timer` callback (that task has a ~3.5 KB stack the I2C path overflows, and
blocking there stalls other timers). Instead, the first semantic signal lazily
creates a dedicated task `led_sig` (3072-byte stack, `SYS_PRIO_BACKGROUND`,
pinned to `SYS_CORE_RADIO`). Each signal sets the color and an off-deadline, then
notifies the task; the task sleeps until the (possibly re-armed) deadline and then
clears the LED once. The flash itself is therefore non-blocking to the caller.

## API Reference

### Initialization

#### `led_rgb_init`
```c
esp_err_t led_rgb_init(void);
```
Assumes the shared I2C bus is already up. Adds the LP5816 to the bus, resets it,
enables the RGB channels in manual PWM mode, and starts with the LED off. Returns
`ESP_OK`, or an `esp_err_t` if the LP5816 does not respond on I2C.

### Direct Color Control

```c
void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_clear(void);
void led_blink(uint8_t r, uint8_t g, uint8_t b, int duration_ms);
```
- `led_set_color`: sets the RGB PWM duty (0-255 per channel).
- `led_clear`: turns the LED off (all channels to 0).
- `led_blink`: sets the color, blocks for `duration_ms`, then clears.

### Fixed-color Blink Helpers

```c
void led_blink_red(void);    // 255,0,0   for 500 ms (error)
void led_blink_green(void);  // 0,150,0   for 220 ms (success)
void led_blink_blue(void);   // 0,0,255   for 500 ms (info)
void led_blink_purple(void); // 200,0,220 for 500 ms (info)
```
Each blocks for its duration.

### Semantic Signal API

```c
void led_set_signal_config(uint32_t info, uint32_t warning, uint32_t error, int brightness);
void led_signal_info(void);
void led_signal_warning(void);
void led_signal_error(void);
```
- `led_set_signal_config`: pushes the signal colors (0xRRGGBB) and global
  brightness (0-100) in from the Service layer, so the driver never depends on the
  config module. The brightness scales each channel before it is written.
- `led_signal_info` / `led_signal_warning` / `led_signal_error`: flash the status
  LED once in the configured color, then turn it off. Non-blocking; the off runs on
  the dedicated `led_sig` task described above.

---

# C5

RGB status LED backed by an addressable RMT LED strip (single pixel), driven
through the `led_strip` component. This is not the LP5816 used on P4, and there is
no I2C, signal API, or dedicated off task.

## Overview

- **Location:** `components/Drivers/led/`
- **Header:** `include/led_control.h`
- **Interface:** RMT (single-pixel addressable LED via the `led_strip` component)
- **Data pin:** `GPIO_LED_RGB_PIN` (GPIO 27, from `pin_def.h`)
- **Dependencies:** `led_strip`, `pin_def`, `freertos`

## Configuration / Tunables

- `RMT_RESOLUTION_HZ` = `10 MHz`: RMT channel resolution.
- Strip: `max_leds = 1`, `RMT_CLK_SRC_DEFAULT`, DMA disabled.
- Blink colors/durations:

| Helper | RGB | Duration |
| :--- | :--- | :--- |
| `led_blink_red` | 255,0,0 | 500 ms |
| `led_blink_green` | 0,150,0 | 220 ms |
| `led_blink_blue` | 0,0,255 | 500 ms |
| `led_blink_purple` | 200,0,220 | 500 ms |

## API Reference

### Initialization

#### `led_rgb_init`
```c
void led_rgb_init(void);
```
Creates the single-pixel RMT LED strip device on `GPIO_LED_RGB_PIN` and clears it.
Uses `ESP_ERROR_CHECK`, so a failure aborts.

### Fixed-color Blink Helpers

```c
void led_blink_red(void);
void led_blink_green(void);
void led_blink_blue(void);
void led_blink_purple(void);
```
Each sets the pixel to its color, refreshes, blocks for the color's duration, then
clears and refreshes. No-op if the strip is not initialized.
