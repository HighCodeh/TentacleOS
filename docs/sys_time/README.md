# System Time - P4

Central wall-clock owner for `firmware_p4`. The HighBoy V2 has no battery-backed RTC, so wall-clock time is volatile across power cycles: the P4's internal RTC timer runs while powered but resets on power-off. Time must be injected from an external source every power cycle (console, companion app over `host_link`, or SNTP via the C5) and is maintained by the internal timer until the next power loss. The whole system runs in UTC.

## Overview

- **Location:** `firmware_p4/components/Service/sys_time/`
- **Header:** `include/sys_time.h`
- **Source:** `sys_time.c`
- **Dependencies:** `esp_log`, `nvs`, `esp_err`, libc `<time.h>` / `<sys/time.h>`
- **Persistence:** NVS namespace `systime` (keys `last_epoch`, `last_src`)

## Build-date baseline

So nothing is ever dated to 1970, `sys_time_init` seeds the clock with a baseline instead of leaving it at epoch 0:

1. It derives a build-date epoch from the compiler `__DATE__` / `__TIME__` macros (`sys_time_build_epoch`). The device cannot predate its own image, so the build date is a safe floor. If parsing the macros fails, it falls back to `SYS_TIME_EPOCH_MIN`.
2. It reads the last-known time persisted in NVS (`sys_time_saved_epoch`).
3. The baseline is raised to the NVS value **only if** the saved epoch is `>= SYS_TIME_EPOCH_MIN`, otherwise the build-date floor stands.
4. The timezone is fixed to `UTC0`, `settimeofday` applies the baseline, and the state is left at `SYS_TIME_STATE_ESTIMATED` with source `SYS_TIME_SOURCE_NONE`.

The clock stays `ESTIMATED` (a floor, not a real time) until a real source calls `sys_time_set`, which promotes it to `SYS_TIME_STATE_SYNCED`.

## The `SYS_TIME_EPOCH_MIN` guard

`SYS_TIME_EPOCH_MIN` is `1735689600` (2025-01-01 00:00:00 UTC). It is used in two places:

- **Baseline selection:** a persisted NVS epoch below the guard is treated as garbage and ignored, keeping the build-date floor.
- **Rejecting bad sets:** `sys_time_set` returns `ESP_ERR_INVALID_ARG` for any `epoch < SYS_TIME_EPOCH_MIN`, so an implausibly early value can never sync the clock.

## API Reference

### States and sources

```c
typedef enum {
  SYS_TIME_STATE_ESTIMATED = 0, // baseline (build date or last-known NVS); a floor
  SYS_TIME_STATE_SYNCED,        // set from a real external source; time() trustworthy
} sys_time_state_t;

typedef enum {
  SYS_TIME_SOURCE_NONE = 0,
  SYS_TIME_SOURCE_MANUAL, // console `date` command
  SYS_TIME_SOURCE_HOST,   // companion app over host_link
  SYS_TIME_SOURCE_SNTP,   // NTP over WiFi (via the C5)
} sys_time_source_t;
```

### `sys_time_init`
```c
void sys_time_init(void);
```
Fixes the timezone to UTC and seeds the clock to the later of the firmware build date and the last-known NVS time, leaving state at `SYS_TIME_STATE_ESTIMATED`. Call once at boot.

### `sys_time_set`
```c
esp_err_t sys_time_set(time_t epoch, sys_time_source_t source);
```
Apply a wall-clock time from a real source. Sets the clock, marks it `SYNCED`, records `source`, and persists the value to NVS. Returns `ESP_ERR_INVALID_ARG` if `epoch` is below `SYS_TIME_EPOCH_MIN`, `ESP_FAIL` if `settimeofday` fails, `ESP_OK` on success.

### `sys_time_state`
```c
sys_time_state_t sys_time_state(void);
```
Current trust level of the clock.

### `sys_time_source`
```c
sys_time_source_t sys_time_source(void);
```
Source of the last successful sync.

### `sys_time_now`
```c
time_t sys_time_now(void);
```
Current wall-clock (UTC); at least the boot baseline.

### `sys_time_format`
```c
bool sys_time_format(char *out, size_t out_size, const char *fmt);
```
Format the current time with `strftime` (UTC fields) into `out`. Sets `out` to `""` on error. Returns `false` on a bad argument or if nothing was written.

## Tunables

| Symbol | Value | Meaning |
|--------|-------|---------|
| `SYS_TIME_EPOCH_MIN` | `1735689600` | Plausibility floor (2025-01-01 UTC): rejects early sets and stale NVS baselines. |
| `SYS_TIME_NVS_NS` | `"systime"` | NVS namespace for last-known time. |
| `SYS_TIME_NVS_KEY_EPOCH` | `"last_epoch"` | NVS key: last synced epoch (`u64`). |
| `SYS_TIME_NVS_KEY_SRC` | `"last_src"` | NVS key: last sync source (`u8`). |
