<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# CC1101 Sub-GHz Radio Driver

This component provides a full driver for the Texas Instruments CC1101 low-power sub-GHz RF transceiver. It handles SPI communication, frequency configuration, modulation presets, and TX/RX operations.

## Overview

- **Location:** `components/Drivers/cc1101/`
- **Header:** `include/cc1101.h`
- **Dependencies:** `spi`, `pin_def`, `driver/gpio`, `freertos`
- **Interface:** SPI (via `spi` component, device `SPI_DEVICE_CC1101`)
- **Crystal:** 26 MHz (used for frequency calculations)

## Supported Frequency Bands

| Band       | Range (MHz)   | PA Table |
|------------|---------------|----------|
| 315 MHz    | 300 - 348     | `PA_TABLE_315` |
| 433 MHz    | 387 - 464     | `PA_TABLE_433` |
| 868 MHz    | 779 - 899     | `PA_TABLE_868` |
| 915 MHz    | 900 - 928     | `PA_TABLE_915` |

## Modulation Presets (`cc1101_preset_t`)

| Preset                     | Mode    | RX Bandwidth |
|----------------------------|---------|--------------|
| `CC1101_PRESET_IDLE`       | Idle    | -            |
| `CC1101_PRESET_OOK_270KHZ`| ASK/OOK | 270 kHz     |
| `CC1101_PRESET_OOK_650KHZ`| ASK/OOK | 650 kHz     |
| `CC1101_PRESET_OOK_800KHZ`| ASK/OOK | 812 kHz     |
| `CC1101_PRESET_2FSK_2KHZ` | 2-FSK   | 58 kHz       |
| `CC1101_PRESET_2FSK_47KHZ`| 2-FSK   | 270 kHz      |
| `CC1101_PRESET_2FSK_95KHZ`| 2-FSK   | 540 kHz      |

## API Reference

### Initialization

#### `cc1101_init`
```c
void cc1101_init(void);
```
Adds the CC1101 to the SPI bus (SPI3_HOST, 4 MHz), performs a hardware reset, verifies chip presence via version register, and sets the default frequency to **433.92 MHz**.

### Frequency & Calibration

#### `cc1101_set_frequency`
```c
void cc1101_set_frequency(uint32_t freq_hz);
```
Sets the carrier frequency in Hz after a bounded IDLE check. High Boy V2.03 uses
RF1 for 300–348/387–464 MHz and RF2 for 779–928 MHz, restoring the branch choices
that performed best on prototype 0201891B in the November 13 comparison. This is
assembly-specific evidence, not a general antenna-match measurement. The chamber
backend still exposes all four branches for independent comparison.
Calculates FREQ2/FREQ1/FREQ0 from the 26 MHz reference, calibrates, and reapplies
the active PA request with the target band's table.

#### `cc1101_calibrate`
```c
void cc1101_calibrate(void);
```
Performs frequency synthesizer calibration with band-specific TEST0/FSCAL2.
FSCTRL0 remains zero: the legacy unmeasured linear frequency offset is disabled.
Any future crystal correction requires a measured reference, recorded per board.

### Preset Management

#### `cc1101_set_preset`
```c
void cc1101_set_preset(cc1101_preset_t preset, uint32_t freq_hz);
```
Configures the radio with a predefined modulation/bandwidth combination. Internally calls `cc1101_enable_async_mode` (OOK presets) or `cc1101_enable_fsk_mode` (FSK presets) and then applies preset-specific tuning.

#### `cc1101_get_active_preset_id`
```c
uint8_t cc1101_get_active_preset_id(void);
```
Returns the ID of the currently active preset.

### Operating Modes

#### `cc1101_enable_async_mode`
```c
void cc1101_enable_async_mode(uint32_t freq_hz);
```
Configures the CC1101 for **ASK/OOK async serial output** on GDO0 (for RMT-based sniffing). Sets infinite packet length, max sensitivity AGC, 812 kHz RX bandwidth, and enters RX.

#### `cc1101_enable_fsk_mode`
```c
void cc1101_enable_fsk_mode(uint32_t freq_hz);
```
Configures the CC1101 for **2-FSK async serial output** on GDO0. Same async architecture as OOK mode but with FSK modulation.

#### `cc1101_enter_rx_mode` / `cc1101_enter_tx_mode`
```c
void cc1101_enter_rx_mode(void);
void cc1101_enter_tx_mode(void);
```
Transitions the radio to RX or TX state (via IDLE first).

### Data Transmission

#### `cc1101_send_data`
```c
void cc1101_send_data(const uint8_t *data, size_t len);
```
Sends a packet (max 61 bytes) via the TX FIFO. Flushes the FIFO, writes length + payload, strobes TX, and blocks until transmission completes (polls MARCSTATE).

### Modem Tuning

#### `cc1101_set_rx_bandwidth`
```c
void cc1101_set_rx_bandwidth(float khz);
```
Sets the RX filter bandwidth in kHz by calculating the MDMCFG4 register fields.

#### `cc1101_set_data_rate`
```c
void cc1101_set_data_rate(float baud);
```
Sets the data rate in kBaud (range: ~0.025 - 1621.83). Writes MDMCFG4 (exponent) and MDMCFG3 (mantissa).

#### `cc1101_set_deviation`
```c
void cc1101_set_deviation(float dev);
```
Sets frequency deviation in kHz (range: 1.59 - 380.86) for FSK modulation.

#### `cc1101_set_modulation`
```c
void cc1101_set_modulation(uint8_t modulation);
```
Sets the modulation format: `0` = 2-FSK, `1` = GFSK, `2` = ASK/OOK, `3` = 4-FSK, `4` = MSK. Automatically adjusts FREND0 and reapplies PA settings.

#### `cc1101_set_pa`
```c
void cc1101_set_pa(int dbm);
```
Sets the output power in dBm. Automatically selects the correct PA table for the current frequency band. Handles ASK/OOK PATABLE indexing (index 0 = 0x00, index 1 = power).

#### `cc1101_set_channel`
```c
void cc1101_set_channel(uint8_t channel);
```
Sets the channel number (CHANNR register).

#### `cc1101_set_chsp`
```c
void cc1101_set_chsp(float khz);
```
Sets channel spacing in kHz (range: 25.39 - 405.46).

#### `cc1101_set_sync_mode`
```c
void cc1101_set_sync_mode(uint8_t mode);
```
Configures sync word detection mode (0-7). See CC1101 datasheet for mode descriptions.

#### `cc1101_set_fec`
```c
void cc1101_set_fec(bool enable);
```
Enables or disables Forward Error Correction.

#### `cc1101_set_preamble`
```c
void cc1101_set_preamble(uint8_t preamble_bytes);
```
Sets the number of preamble bytes (2-24, mapped to register encoding).

#### `cc1101_set_dc_filter_off` / `cc1101_set_manchester`
```c
void cc1101_set_dc_filter_off(bool disable);
void cc1101_set_manchester(bool enable);
```
Toggles DC blocking filter and Manchester encoding respectively.

### Utilities

#### `cc1101_convert_rssi`
```c
float cc1101_convert_rssi(uint8_t rssi_raw);
```
Converts a raw RSSI register value to dBm.

### Low-Level SPI Access

```c
void cc1101_strobe(uint8_t cmd);
void cc1101_write_reg(uint8_t reg, uint8_t val);
uint8_t cc1101_read_reg(uint8_t reg);
void cc1101_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len);
void cc1101_read_burst(uint8_t reg, uint8_t *buf, uint8_t len);
```
Direct SPI register access: single read/write, burst read/write, and strobe commands.

## Persistent app-worker lease

Bounded app RX/TX uses a task-owned lease and a non-preempting SYSTEM grant on
`RES_SUBGHZ`. Existing native RX, TX and spectrum grants prevent acquisition.
All 29 hardware/state-facing public CC1101 functions check that lease; foreign
callers cannot initialize, recover, read, retune or transmit while it is held.
In-flight native calls also prevent acquisition. Pure RSSI conversion remains
available. Rejected burst reads zero their caller's output.

SPI errors during a leased operation are latched so legacy void setters cannot
turn a failed configuration into a successful app call. Failed RMT/SPI/power
cleanup retains buffers, handles and the grant on the persistent worker. Its
access scope is then paused: even unrelated companion commands on that same
worker cannot use the retained lease. A later bounded app operation reopens the
scope and retries cleanup before touching a new capture/transmission.

This lease is internal, not an app-visible raw driver API. See
[Sub-GHz ABI](../app_sdk/README.md#bounded-sub-ghz-api-abi-113) for bounds and
[Sub-GHz](../SubGhz/README.md) for native worker shutdown.

## Chamber backend

The separate checked backend exposes CW, fixed-packet TX/RX and explicit shunt
branch selection under the task lease. It saves/restores register configuration
and PATABLE and reports failures instead of assuming successful SPI writes.
See [RF chamber service](../rf_chamber/README.md) for exact PHY, bounds and validation.
