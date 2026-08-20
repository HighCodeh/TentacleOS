# BQ25896 Charger / PMIC Driver

This component drives the Texas Instruments BQ25896 single-cell battery charger and power-path IC over I2C. It exposes battery voltage, charge and VBUS status, charge enable control, and a real ship-mode power-off. A thin `battery_service` layer sits on top and publishes a single smoothed snapshot that the UI reads instead of polling the charger directly.

## Overview

- **Location:** `components/Drivers/bq25896/` (firmware_p4 only)
- **Headers:** `include/bq25896.h`, `include/battery_service.h`
- **Sources:** `bq25896.c` (register I/O + controls), `bq25896_ext.c` (telemetry aggregator), `battery_service.c` (poll task + smoothing)
- **Dependencies:** `i2c_init`, `pins` (`pin_def.h`), `sys_prio`, `driver/gpio`, `freertos`
- **Interface:** I2C master bus `I2C_NUM_0` (via `i2c_init`, shared), 100 kHz standard-mode
- **I2C address:** `0x6B` (`BQ25896_I2C_ADDR`, 7-bit)
- **CE pin:** `GPIO_CHARGER_CE_PIN` (GPIO33), active-low charge enable, driven at init

## Charge Status (`bq25896_charge_status_t`)

| Value | Meaning |
|-------|---------|
| `CHARGE_STATUS_NOT_CHARGING` | Not charging |
| `CHARGE_STATUS_PRECHARGE` | Pre-charge phase |
| `CHARGE_STATUS_FAST_CHARGE` | Fast-charge phase |
| `CHARGE_STATUS_CHARGE_DONE` | Charge complete |

## VBUS Status (`bq25896_vbus_status_t`)

| Value | Meaning |
|-------|---------|
| `VBUS_STATUS_UNKNOWN` | No/unknown VBUS source |
| `VBUS_STATUS_USB_HOST` | USB host (SDP) input |
| `VBUS_STATUS_ADAPTER_PORT` | Dedicated adapter input |
| `VBUS_STATUS_OTG` | OTG (boost) output |

## BQ25896 API Reference

### Initialization

#### `bq25896_init`
```c
esp_err_t bq25896_init(void);
```
Adds the charger to the shared I2C bus, probes it (REG14), and marks the chip present. Drives CE (GPIO33) low to enable charging, disables the charge watchdog and sets `JEITA_ISET=0` (REG07), lowers `SYS_MIN` to 3.0 V (REG03) so the battery ADC keeps converting down to the 3.0 V knee, and enables the continuous ADC (REG02). Returns `ESP_OK` on success, or the failing `esp_err_t`.

#### `bq25896_is_present`
```c
bool bq25896_is_present(void);
```
True once the charger has answered on I2C at init.

### Battery State

#### `bq25896_get_battery_voltage`
```c
uint16_t bq25896_get_battery_voltage(void);
```
Battery voltage in mV from REG0E (base 2304 mV, 20 mV/step), or 0 on read failure.

#### `bq25896_get_battery_percentage`
```c
int bq25896_get_battery_percentage(uint16_t voltage_mv);
```
Linear voltage-to-percent estimate (0-100) between 3200 mV and 4200 mV.

#### `bq25896_get_charge_status` / `bq25896_get_vbus_status`
```c
bq25896_charge_status_t bq25896_get_charge_status(void);
bq25896_vbus_status_t   bq25896_get_vbus_status(void);
```
Decode the CHG_STAT and VBUS_STAT fields of the status register (REG0B).

#### `bq25896_is_charging`
```c
bool bq25896_is_charging(void);
```
True while pre-charging or fast charging.

#### `bq25896_get_fault`
```c
uint8_t bq25896_get_fault(void);
```
Raw fault register (REG0C): `CHRG_FAULT[5:4]`, `BAT_FAULT[3]`, `NTC_FAULT[2:0]`.

### Charge Control

#### `bq25896_get_charge_enable`
```c
bool bq25896_get_charge_enable(void);
```
Reads REG03 `CHG_CONFIG` (bit 4).

#### `bq25896_set_charge_enable`
```c
esp_err_t bq25896_set_charge_enable(bool enable);
```
Drives both gates: the active-low CE pin (GPIO33) and REG03 `CHG_CONFIG`, so the state is unambiguous.

### Ship Mode / Power Off

#### `bq25896_power_off`
```c
esp_err_t bq25896_power_off(void);
```
Real power-off: sets `BATFET_DIS` (REG09 bit 5) to disconnect the battery. Also sets `BATFET_DLY` (bit 3) so the MCU finishes the I2C write before the rail collapses, and clears `BATFET_RST_EN` (bit 2) to disarm the /QON auto-reset. Without disarming, a /QON pulse (the BACK button on this board) would cycle BATFET back on and cold-boot the device a few seconds later. Has no effect while VBUS (USB) is present: the part keeps the system powered from USB.

### Telemetry

#### `bq25896_read_telemetry`
```c
esp_err_t bq25896_read_telemetry(bq25896_telem_t *out);
```
Fills a `bq25896_telem_t` snapshot. Battery voltage/percent/charge/VBUS/fault fields are real; the `vsys_mv`, `vbus_mv`, `ichg_ma`, and `iinlim_ma` diagnostic fields are currently 0 (approximate/unpopulated). Returns `ESP_ERR_INVALID_ARG` if `out` is NULL.

`bq25896_telem_t` fields: `vbat_mv`, `vsys_mv`, `vbus_mv`, `ichg_ma`, `iinlim_ma`, `fault`, `soc`, `chg`, `vbus`, `charging`, `power_good`.

#### `bq25896_reg_raw`
```c
uint8_t bq25896_reg_raw(uint8_t reg);
```
Raw single-register read, or 0 on failure.

## Battery Service API Reference

A background poll task (`battery_svc`, core `SYS_CORE_RADIO`, priority `SYS_PRIO_BACKGROUND`) keeps a mutex-guarded smoothed snapshot so screens do not each poll the charger.

#### `battery_service_init`
```c
void battery_service_init(void);
```
Idempotent. Requires `bq25896_init()` first. Takes one synchronous reading so the first UI paint has real data, then polls in the background.

#### `battery_service_get`
```c
bool battery_service_get(battery_snapshot_t *out);
```
Copies the latest snapshot into `out`. Returns true if a valid reading is available.

#### `battery_service_soc` / `battery_service_is_low`
```c
int  battery_service_soc(void);
bool battery_service_is_low(void);
```
Convenience accessors: smoothed SoC (0-100, or -1 if no valid reading yet) and the latched low-battery flag.

`battery_snapshot_t` fields: `soc`, `vbat_mv`, `present`, `charging`, `vbus_present`, `low`, `valid`, `chg`.

### Filtered SoC and Smoothing

The raw voltage-derived SoC is filtered before it reaches the UI:

- **EMA:** `ema = (ema*3 + raw) / 4` (`SOC_EMA_DEN = 4`) rejects the transient voltage sag when a radio transmits.
- **Slew limit:** at most `SOC_STEP_MAX = 3` % change applied per poll.
- **Monotonic on battery:** off external power, SoC never climbs (a recovering voltage after a load sag would otherwise bounce it up); it only rises while charging or on external power.
- **Low-battery hysteresis:** latches `low` at/below `LOW_ENTER_PCT = 15`, releases at/above `LOW_EXIT_PCT = 20`; never low while charging or on VBUS.
- **Charging debounce:** turns the charging indicator on immediately, off immediately when unplugged, but requires `CHG_OFF_DEBOUNCE = 3` consecutive not-charging polls to drop it while still on external power.

## Key Config / Tunables

| Macro | Location | Value | Meaning |
|-------|----------|-------|---------|
| `BQ25896_I2C_ADDR` | `bq25896.h` | `0x6B` | I2C 7-bit address |
| `I2C_TIMEOUT_MS` | `bq25896.c` | 100 | Per-transfer timeout |
| `I2C_FAIL_RECOVER` | `bq25896.c` | 3 | Consecutive failures before `i2c_bus_recover()` |
| `BATV_BASE_MV` / `BATV_STEP_MV` | `bq25896.c` | 2304 / 20 | REG0E voltage decode |
| `BATTERY_MIN_MV` / `BATTERY_MAX_MV` | `bq25896.c` | 3200 / 4200 | Percent-estimate endpoints |
| `POLL_INTERVAL_MS` | `battery_service.c` | 1000 | Steady poll cadence (first sample at ~1200 ms) |
| `SOC_STEP_MAX` | `battery_service.c` | 3 | Max SoC delta per poll |
| `SOC_EMA_DEN` | `battery_service.c` | 4 | EMA denominator |
| `LOW_ENTER_PCT` / `LOW_EXIT_PCT` | `battery_service.c` | 15 / 20 | Low-battery hysteresis |
| `CHG_OFF_DEBOUNCE` | `battery_service.c` | 3 | Not-charging polls before clearing the indicator |

### Bus Recovery

Reads and writes track a failure streak; after `I2C_FAIL_RECOVER` consecutive failures the driver calls `i2c_bus_recover()` (from `i2c_init`) to clock out a slave that has wedged the bus, so the charger does not stay unreadable for the rest of the session.
