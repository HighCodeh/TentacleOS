# DRV2605L Haptic Driver

This component is a minimal driver for the Texas Instruments DRV2605L haptic motor driver over I2C. It uses the chip's internal ROM waveform library for named effects and exposes RTP (real-time playback) for variable-intensity feedback. The driver is tuned for an ERM (eccentric rotating mass) actuator.

## Overview

- **Location:** `components/Drivers/drv2605l/` (firmware_p4 only)
- **Header:** `include/drv2605l.h`
- **Source:** `drv2605l.c`
- **Dependencies:** `i2c_init`, `pins` (`pin_def.h`), `driver/gpio`, `freertos`
- **Interface:** I2C master bus `I2C_NUM_0` (via `i2c_init`, shared), 100 kHz standard-mode
- **I2C address:** `0x5A` (`DRV2605L_I2C_ADDR`, 7-bit)
- **Enable line:** per the header, EN is on GPIO37 (the driver itself does not toggle it)
- **Effect library:** internal ROM library `TS2200 Library A` (`LIB_SEL = 0x01`)

## Configuration Applied at Init

`drv2605l_init()` adds the device to the shared bus, reads the status register (deriving `DEVICE_ID` from bits [7:5]), and programs:

- Mode: internal-trigger (`0x00`)
- Library select: TS2200 Library A (`0x01`)
- Rated voltage: `0x90` (ERM), overdrive clamp: `0xCC` (ERM)
- Feedback control `0x35`, Control1 `0x93`, Control2 `0xF5`, Control3 `0xA0`

Auto-calibration is not run at boot (it is opt-in via `drv2605l_autocal()`) so it never regresses the working manual tune.

## API Reference

### Initialization

#### `drv2605l_init`
```c
esp_err_t drv2605l_init(void);
```
Initializes the driver as described above. Returns `ESP_OK` on success, otherwise the failing `esp_err_t`. Marks the driver ready; all other calls return `ESP_ERR_INVALID_STATE` until it succeeds.

#### `drv2605l_device_id`
```c
uint8_t drv2605l_device_id(void);
```
Last-read `DEVICE_ID` register value (0 if not yet probed).

### Playback

#### `drv2605l_play_effect`
```c
esp_err_t drv2605l_play_effect(uint8_t effect);
```
Plays a single ROM library waveform effect (1..123) once. Leaves RTP mode first if active, writes the effect into waveform-sequence slot 0 with a terminator in slot 1 (skipping the write when the effect is unchanged from the last call), then strobes GO.

#### `drv2605l_stop`
```c
esp_err_t drv2605l_stop(void);
```
Stops any currently-playing waveform (clears GO).

#### `drv2605l_set_rtp`
```c
esp_err_t drv2605l_set_rtp(uint8_t intensity);
```
Enters RTP mode and applies an intensity value (useful range 0..127 for forward drive).

### Calibration

#### `drv2605l_autocal`
```c
esp_err_t drv2605l_autocal(void);
```
Runs ERM auto-calibration (MODE `0x07`) against the actuator, using the rated/overdrive/feedback registers already programmed at init, then returns to internal-trigger mode. Blocks up to ~1.5 s while polling GO. Returns `ESP_OK` if `DIAG_RESULT` passed (status bit 3 clear), else `ESP_FAIL`. Opt-in; not run at boot.

## Key Config / Tunables

| Macro | Location | Value | Meaning |
|-------|----------|-------|---------|
| `DRV2605L_I2C_ADDR` | `drv2605l.h` | `0x5A` | I2C 7-bit address |
| `MODE_INTERNAL_TRIG` | `drv2605l.c` | `0x00` | Internal-trigger playback mode |
| `MODE_AUTO_CAL` | `drv2605l.c` | `0x07` | Auto-calibration mode |
| `MODE_RTP` | `drv2605l.c` | `0x05` | Real-time playback mode |
| `LIB_TS2200_A` | `drv2605l.c` | `0x01` | TS2200 Library A (ERM) |
| `RATED_V_ERM` | `drv2605l.c` | `0x90` | Rated voltage (ERM) |
| `OD_CLAMP_ERM` | `drv2605l.c` | `0xCC` | Overdrive clamp (ERM) |
| `I2C_TIMEOUT_MS` | `drv2605l.c` | 50 | Per-transfer timeout |
| `AUTOCAL_POLL_INTERVAL_MS` | `drv2605l.c` | 10 | Auto-cal GO poll interval |
| `AUTOCAL_POLL_MAX` | `drv2605l.c` | 150 | Max auto-cal poll iterations (~1.5 s) |
