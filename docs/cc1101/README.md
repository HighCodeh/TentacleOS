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

## Frequency analyzer sampling

`cc1101_scan_rssi(frequency, &rssi)` performs checked tune/calibration, RX entry,
2 ms settling and a single RSSI read under one bounded SPI3 reservation. Only
that owning task uses polling SPI inside the reservation. Every failure path
releases the bus and leaves the output unchanged; the caller retains radio
ownership for checked IDLE cleanup. Other users retain the existing transaction
path. The analyzer configures manual calibration (MCSM0=0x08) to avoid repeating
SCAL on SRX. See [frequency analyzer](../frequency_analyzer/README.md).

Production-driver host tests cover one reservation per sample, every SPI failure,
failed lock acquisition, calibration timeout, invalid arguments and release.

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
| 868 MHz    | 779 - <900    | `PA_TABLE_868` |
| 915 MHz    | 900 - 928     | `PA_TABLE_915` |

### High Boy V2.03 matching network

GPIO17 drives PE613050 V1 and GPIO18 drives V2. U10 selects a shunt matching
branch on the common antenna feed; these are not four separate antennas.

| Branch | Matching inductor | V1 / GPIO17 | V2 / GPIO18 |
| --- | --- | --- | --- |
| RF1 | L16, 51 nH | 0 | 0 |
| RF2 | L17, 16 nH | 0 | 1 |
| RF3 | L18, 1.8 nH | 1 | 0 |
| RF4 | L19, 1.5 nH | 1 | 1 |

The branch wiring follows the supplied **High Boy_V2_03 Schematic
2026-10-19.PDF**, page 3, and **P4679_HighBoy Prototype2_measurement
report_summary.pdf**, pages 10–12 (Radientum, 10 September 2026). The
[PE613050 specification, table 4](https://psemi.com/wp-content/uploads/pdf/datasheets/pe613050ds.pdf)
lists its columns as **V2, V1**; the GPIO columns above deliberately use V1, V2.
The report's nominal band assignments are RF1/315, RF2/433, RF4/868 and RF3/915 MHz;
they are distinct from the current prototype selection below. The report notes
high measurement uncertainty at 315/433 MHz.
These supplied hardware documents are reference inputs, not packaged firmware assets.

The current lab profile uses the 20 November 2026 comparison on unit **02018913**:

| Supported tuning range | Selected branch | V1 / GPIO17 | V2 / GPIO18 |
| --- | --- | --- | --- |
| 300–348 MHz (315 band) | RF1 | 0 | 0 |
| 387–464 MHz (433 band) | RF2 | 0 | 1 |
| 779–<900 MHz (868 band) | RF4 | 1 | 1 |
| 900–928 MHz (915 band) | RF2 | 0 | 1 |

At 433.92 MHz, RF2 improved received Flipper signal by 11.5 dB over RF1;
High Boy carrier transmission at nominal 0 dBm improved by 13 dB. At
868.35 MHz RF4 led RF2 by 4.5 dB for High Boy transmission. These are uncalibrated
RSSI comparisons with adjacent devices on an open bench, not range or antenna
efficiency certification. The 315/915 selections are retained from the earlier
unit 0201891B comparison; only configuration/noise was checked there on 20 November.
The upper-band Flipper-to-High-Boy known-signal test remains unverified.

The 13 November profile used RF1 at 315/433 and RF2 at 868/915 on another unit.
Different assembly and positioning produced different optima. Do not generalize
this prototype profile to all units without conducted and over-the-air measurements.
All four paths remain available in RF Lab. PA tables remain frequency-specific.
Invalid frequencies are rejected before touching GPIOs or the radio.

Changes to either switch pin require a confirmed IDLE state, followed by the
switch's 5 us maximum settling interval before tuning.

### Power and OOK gain profile

The nominal power steps are -30, -20, -15, -10, 0, 5, 7 and 10 dBm.
The 868/915 MHz tables follow TI SWRS061I table 37, retaining the existing
wire-wound reference profile; 315/433 MHz use table 39. The former upper-band
arrays included an extra -6 dBm entry but used the eight-step index, so requests
for 0/5/7/10 dBm selected -6/0/5/7 dBm codes. The corrected indices select the
requested nominal step, including C5/C3 for 10 dBm at 868/915 MHz. These are
reference register values; output on this PCB still requires conducted calibration.
See [TI CC1101 datasheet, tables 37 and 39](https://www.ti.com/lit/ds/symlink/cc1101.pdf).

The standard OOK270/OOK650 and 2-FSK 2.38/47.6 kHz modem profiles follow
[official Flipper revision a8a24de3](https://github.com/flipperdevices/flipperzero-firmware/blob/a8a24de3ff394f77d599ea8ba9c073398670c880/lib/subghz/devices/cc1101_configs.c).
Each profile programs its complete modem, AGC, FIFO retention, frequency compensation
and frontend settings after reset, before tuning/calibration and RX entry.

| Profile | MDMCFG4/3/2/1/0 | AGCCTRL2/1/0 | FIFOTHR | FOCCFG |
| --- | --- | --- | --- | --- |
| OOK270 | 67/32/30/00/00 | 03/00/40 | 47 | 18 |
| OOK650 | 17/32/30/00/00 | 07/00/91 | 07 | 18 |
| 2-FSK 2.38 | 67/83/04/02/00 | 07/00/91 | 07 | 16 |
| 2-FSK 47.6 | 67/83/04/02/00 | 07/00/91 | 07 | 16 |

Values are hexadecimal. OOK modem rate is 3.793716 kbaud; 2-FSK is 4.79794 kbaud.
OOK uses FREND1=B6; FSK uses 56. All four use BSCFG=6C, WORCTRL=FB,
MCSM0=18 and async PKTCTRL0=32. OOK270 no longer shares OOK650's gain target
and hysteresis. FSK 2.38 now uses the reference 270.833 kHz receive filter;
its previous 58 kHz filter was not the Flipper profile. Stored numeric preset IDs
remain stable, but their radio settings have changed. The OOK812 and FSK95
extensions, including the legacy generic async entry points, retain their prior
modem behavior and are not claimed to be Flipper presets.

PA tables, High Boy switch GPIOs, GDO2 high impedance and checked synthesizer
calibration remain board-specific. AGC is automatic; these settings do not prove
maximum sensitivity or equivalent range. The 21 November candidate is host-tested
and requires new physical acceptance; earlier hardware results used the prior
profiles. Host checks cover register values, nominal PA steps, IDLE ordering and
failure at every SPI transaction. No increased radiated power is inferred.

## Modulation Presets (`cc1101_preset_t`)

| Preset                     | Mode    | RX Bandwidth |
|----------------------------|---------|--------------|
| `CC1101_PRESET_IDLE`       | Idle    | -            |
| `CC1101_PRESET_OOK_270KHZ`| ASK/OOK | 270 kHz     |
| `CC1101_PRESET_OOK_650KHZ`| ASK/OOK | 650 kHz     |
| `CC1101_PRESET_OOK_800KHZ`| ASK/OOK | 812 kHz     |
| `CC1101_PRESET_2FSK_2KHZ` | 2-FSK   | 270 kHz      |
| `CC1101_PRESET_2FSK_47KHZ`| 2-FSK   | 270 kHz      |
| `CC1101_PRESET_2FSK_95KHZ`| 2-FSK   | 540 kHz      |

## API Reference

Initialization, tuning, preset/mode changes, register writes and FIFO transmission
return `esp_err_t`. Native workers check these results before enabling dependent
hardware; the first failure is also retained by the app-worker lease. Legacy
byte-returning reads and ancillary void setters retain the lease error latch.

### Initialization

#### `cc1101_init`
```c
esp_err_t cc1101_init(void);
```
Adds the CC1101 to SPI3 at 1 MHz, checks RF-switch GPIO initialization, resets the
radio, verifies PARTNUM/VERSION, and tunes to **433.92 MHz**. Initialization is
reported complete only after successful calibration and PA configuration.
`cc1101_is_initialized()` reflects that completed initialization, not merely an
allocated SPI handle. Failed initialization can be retried; `cc1101_recover()`
performs the same checked initialization again and returns its error.

### Frequency & Calibration

#### `cc1101_set_frequency`
```c
esp_err_t cc1101_set_frequency(uint32_t freq_hz);
```
Sets the carrier frequency after a bounded IDLE check and selects the matching
branch above. Calculates `FREQ = floor(frequency_hz * 65536 / 26000000)` using a
64-bit intermediate, calibrates, and reapplies the active PA request using the
target band's table. Frequency and band selection use integer Hz, including
899,999,999 Hz, without a floating-point gap below 900 MHz. The operation leaves
the radio in IDLE; callers explicitly resume RX/TX.

#### `cc1101_calibrate`
```c
esp_err_t cc1101_calibrate(void);
```
Performs frequency synthesizer calibration with band-specific TEST0/FSCAL2.
When manual high-VCO selection is required, FSCAL2 is set **before** SCAL.
The driver waits for MARCSTATE to return to IDLE instead of assuming a fixed
delay is sufficient. State polling has a 100 ms deadline; SPI bus-lock waits
are separately bounded. A tuning failure prevents subsequent RX/TX strobes
until tuning succeeds. Failed mode entry attempts to return to IDLE.
FSCTRL0 remains zero: the unmeasured linear frequency-offset calculation was removed.
Any future crystal correction requires a measured reference, recorded per board.

### Preset Management

#### `cc1101_set_preset`
```c
esp_err_t cc1101_set_preset(cc1101_preset_t preset, uint32_t freq_hz);
```
Configures the complete modulation/bandwidth/deviation profile in IDLE, selects
the matching branch, calibrates, applies the PA table, and then enters RX.
Calibration results are not overwritten by later profile writes. Invalid preset
IDs are rejected; the active preset is published only after RX entry succeeds.
The idle preset stops the radio without requiring a carrier frequency.

#### `cc1101_get_active_preset_id`
```c
uint8_t cc1101_get_active_preset_id(void);
```
Returns the ID of the currently active preset.

### Operating Modes

#### `cc1101_enable_async_mode`
```c
esp_err_t cc1101_enable_async_mode(uint32_t freq_hz);
```
Configures **ASK/OOK async serial output** on GDO0 for RMT capture, with infinite
packet length, no sync-word qualification, and approximately 812 kHz RX bandwidth.
Retains the extension AGCCTRL2/1/0 profile `07/00/91`; this is not a maximum-gain
claim. OOK disables frequency-offset compensation (`FOC_LIMIT=0`), as required
by the CC1101 datasheet. The current PA request is retained across presets.

#### `cc1101_enable_fsk_mode`
```c
esp_err_t cc1101_enable_fsk_mode(uint32_t freq_hz);
```
Configures **2-FSK async serial output** on GDO0, with no sync-word qualification.
Uses the same checked configuration sequence and preserves FSK frequency-offset
compensation.

#### `cc1101_enter_rx_mode` / `cc1101_enter_tx_mode`
```c
esp_err_t cc1101_enter_rx_mode(void);
esp_err_t cc1101_enter_tx_mode(void);
```
Transitions the radio to RX or TX state (via IDLE first).

### Data Transmission

#### `cc1101_send_data`
```c
esp_err_t cc1101_send_data(const uint8_t *data, size_t len);
```
Sends 1–61 bytes through the TX FIFO. Requires normal FIFO mode with variable
packet length; async serial and fixed-length profiles return `ESP_ERR_NOT_SUPPORTED`.
Invalid/oversized payloads are rejected rather than truncated. Flushes the FIFO,
writes the length and payload, and checks both TXBYTES and MARCSTATE. An initial
IDLE sample with queued bytes is not completion. Underflow is an error; polling
yields and times out after 2 seconds. Every started attempt finishes with checked
IDLE/flush cleanup. The original transfer error takes precedence over cleanup
errors. This helper is separate from native RMT transmission.

### Modem Tuning

#### `cc1101_set_rx_bandwidth`
```c
esp_err_t cc1101_set_rx_bandwidth(float khz);
```
Chooses the closest of the 16 representable RX filter bandwidths using
`BW = 26 MHz / (8 * (4 + M) * 2^E)`, preserving MDMCFG4's data-rate exponent.

#### `cc1101_set_data_rate`
```c
esp_err_t cc1101_set_data_rate(float baud);
```
Accepts **baud**, not kBaud. Chooses the closest exponent/mantissa using
`rate = (256 + M) * 2^E * 26 MHz / 2^28`, preserving the bandwidth fields.
Rounding across a mantissa boundary advances the exponent instead of wrapping
the mantissa and halving the requested rate.

#### `cc1101_set_deviation`
```c
esp_err_t cc1101_set_deviation(float dev);
```
Chooses the nearest representable FSK deviation using
`deviation = (8 + M) * 2^E * 26 MHz / 2^17` (API input in kHz).

#### `cc1101_set_modulation`
```c
esp_err_t cc1101_set_modulation(uint8_t modulation);
```
Sets the modulation format: `0` = 2-FSK, `1` = GFSK, `2` = ASK/OOK, `3` = 4-FSK, `4` = MSK. Automatically adjusts FREND0 and reapplies PA settings.

#### `cc1101_set_pa`
```c
esp_err_t cc1101_set_pa(int dbm);
```
Sets the output power in dBm. Automatically selects the correct PA table for the current frequency band. Handles ASK/OOK PATABLE indexing (index 0 = 0x00, index 1 = power).

#### `cc1101_set_channel`
```c
void cc1101_set_channel(uint8_t channel);
```
Sets the channel number (CHANNR register).

#### `cc1101_set_chsp`
```c
esp_err_t cc1101_set_chsp(float khz);
```
Chooses the nearest channel spacing using
`spacing = (256 + M) * 2^E * 26 MHz / 2^18` (API input in kHz), preserving
the other MDMCFG1 fields. All four numeric setters reject non-finite/non-positive
inputs and saturate positive out-of-range requests to register limits. Register
representability alone does not establish a valid RF profile for every modulation;
the device's modulation-specific operating limits still apply.

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
esp_err_t cc1101_strobe(uint8_t cmd);
esp_err_t cc1101_write_reg(uint8_t reg, uint8_t val);
esp_err_t cc1101_get_register(uint8_t reg, uint8_t *out_value);
uint8_t cc1101_read_reg(uint8_t reg);
esp_err_t cc1101_write_burst(uint8_t reg, const uint8_t *buf, uint8_t len);
void cc1101_read_burst(uint8_t reg, uint8_t *buf, uint8_t len);
```
Direct SPI register access: single read/write, burst read/write, and strobe commands.
Use `cc1101_get_register` when a caller must distinguish a valid zero from a read
failure; it preserves the output on failure. The legacy byte-returning read remains
available for existing lease-based consumers.

## Persistent app-worker lease

Bounded app RX/TX uses a task-owned lease and a non-preempting SYSTEM grant on
`RES_SUBGHZ`. Existing native RX, TX and spectrum grants prevent acquisition.
All hardware/state-facing public CC1101 functions check that lease; foreign
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

## References and validation

Register equations, OOK compensation requirements and calibration states follow
the [TI CC1101 datasheet](https://www.ti.com/lit/ds/symlink/cc1101.pdf).
The supplied driver checkouts were consulted for comparison:

- [SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib/tree/de6757ef633b3e626dae5c7835cdb66ab53f9a1d): band-specific TEST0 and PA handling.
- [esp-idf-cc1101](https://github.com/nopnop2002/esp-idf-cc1101/tree/1a8f0c7842828acb82b3e20127b60756ad03910f): ESP-IDF register configuration.
- [Momentum](https://github.com/next-flip/momentum-firmware/tree/d3f89dfe2ef6b01839201598e9be1590cba80322): bounded calibration/state sequencing. Its board's RF-switch GPIO mapping is not used on High Boy.

`python3 tools/subghz_api/test_subghz_api.py` compiles the production driver with
explicit SPI/GPIO/clock fakes and address/undefined-behavior sanitizers. It checks
band boundaries and gaps, switch levels and IDLE ordering, carrier quantization,
PA indexing, all presets, modem rounding, initialization/recovery, FIFO
completion/timeout/underflow, checked RSSI reads, all 256 RSSI conversions,
and injected SPI/GPIO/allocation/timeouts. The existing ownership, RX/TX and spectrum tests
remain part of that suite. These are host checks, not RF measurements; physical
RX sensitivity, TX output, antenna matching and repeated band changes must still
be verified on the assembled board.
