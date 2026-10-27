# Power Manager - P4 (esp_pm DFS / light-sleep)

System power manager for `firmware_p4`. It owns `esp_pm` and drives automatic light sleep gated by a shared `NO_LIGHT_SLEEP` lock, so the CPU only powers off when the device is genuinely idle (screen off, no active resource, not plugged in).

The integrated High Boy profile currently keeps `CONFIG_PM_ENABLE` disabled
because UART/USB wake-up is not validated. The enabled branch below is host-tested
policy support, not permission to enable sleep or a claim of device acceptance.

## Overview

- **Location:** `firmware_p4/components/Service/power_manager/`
- **Header:** `include/power_manager.h`
- **Sources:** `power_manager.c` (esp_pm setup + lock), `power_manager_usb.c` (TinyUSB bus callbacks)
- **Dependencies:** `esp_pm`, `esp_log`, `esp_err`, `tusb` (for the USB callbacks)
- **Gated by:** `CONFIG_PM_ENABLE`. When PM is off, all entry points are inert no-ops (USB state is still tracked); the CPU stays at a fixed frequency and never sleeps.
- **Why Service (not Core):** `power_policy` lives in Applications, which cannot depend on Core (Core already REQUIRES Applications, so that edge would be a cycle). Both Core (transitively) and Applications reach Service.

## Power model

- **Frequency is pinned:** `esp_pm_configure` is called with `min_freq_mhz == max_freq_mhz == 360`, so there is **no DFS**. DFS would scale the APB clock and break the console UART baud (the IDF UART driver excludes the console UART from PM). The power win here is light sleep (CPU fully off when idle), not frequency scaling.
- **Light sleep** (`light_sleep_enable = true`) engages only when nobody holds the shared `ESP_PM_NO_LIGHT_SLEEP` lock **and** all tasks are blocked.
- **The `NO_LIGHT_SLEEP` lock** is created once at init and is not held by the manager itself. Resource owners that need the CPU awake acquire it and release it themselves (screen on, active radio, host-link/OTA session, ...). It is ref-counted: while the count is `> 0` the system never light-sleeps.

## USB / external-power gating (item 41)

Two independent inputs feed a single internal wake hold:

- **VBUS / external power** from the charger (battery service) via `power_manager_set_external_power`.
- **USB bus lifecycle** via `power_manager_set_usb_suspended`.

The internal rule is `want = external_power && !usb_suspended`. When that transitions true the manager takes one `NO_LIGHT_SLEEP` hold; when it transitions false it releases it. So the device never light-sleeps while plugged in and the host has not suspended the bus (mid charge, host-link, or OTA session).

`power_manager_usb.c` provides the strong TinyUSB bus-lifecycle callbacks (TinyUSB declares them weak). They only fire when native P4 USB is up (mux switched to native); on the default UART-bridge path TinyUSB is not installed and plugged-in state comes solely from VBUS. Mapping:

| TinyUSB callback | USB suspended input | Effect |
|------------------|---------------------|--------|
| `tud_mount_cb` / `tud_resume_cb` | not suspended | hold wake lock (if external power) |
| `tud_umount_cb` / `tud_suspend_cb` | suspended | release wake lock |

## API Reference

### `power_manager_init`
```c
void power_manager_init(void);
```
Call early in boot. Creates and acquires the shared guard before enabling fixed-frequency light sleep. The guard becomes the USB hold when required, or is released after publication. Repeated successful init is idempotent. Failed release/delete keeps a pending handle for a later init retry. No-op when `CONFIG_PM_ENABLE` is off.

### `power_manager_no_sleep_acquire`
```c
esp_err_t power_manager_no_sleep_acquire(void);
```
Hold the CPU out of light sleep. Ref-counted: every acquire needs a matching release. Returns `ESP_ERR_INVALID_STATE` if the lock was never created; `ESP_OK` otherwise (also `ESP_OK` as a no-op when PM is disabled).

### `power_manager_no_sleep_release`
```c
esp_err_t power_manager_no_sleep_release(void);
```
Release one hold taken with `power_manager_no_sleep_acquire`. Same return contract as acquire.

### `power_manager_set_external_power`
```c
void power_manager_set_external_power(bool present);
```
Report external power (USB/charger VBUS) presence. Idempotent; recomputes the internal USB wake hold.

### `power_manager_set_usb_suspended`
```c
void power_manager_set_usb_suspended(bool suspended);
```
Report USB bus suspend state (from `tud_suspend`/`tud_resume`, and treated as suspended on umount). A suspended host drops the plugged-in hold. Idempotent.

### `power_manager_external_power`
```c
bool power_manager_external_power(void);
```
True while external power (VBUS) is present. Always valid regardless of `CONFIG_PM_ENABLE`, for battery-vs-plugged policy decisions.

## Tunables

| Symbol | Location | Value | Meaning |
|--------|----------|-------|---------|
| `PM_FREQ_MHZ` | `power_manager.c` | `360` | Pinned CPU frequency (min == max, no DFS). |
| `CONFIG_PM_ENABLE` | sdkconfig | - | Master gate. Off: all calls are inert no-ops. |


## Hold accounting and failure behavior

USB/VBUS inputs, the applied USB hold and client reference counts are serialized
with one critical section. The IDF PM acquire/release operations are ISR-safe;
creation/configuration/deletion and logs remain outside that critical section.
Input values received before init are retained and reconciled after configuration.
The applied USB hold changes only after a successful PM operation. Repeating the
same input retries a failed operation instead of silently treating it as applied.
Clients cannot release the manager's USB/initial guard: a release without an
outstanding client acquire returns `ESP_ERR_INVALID_STATE`. Failed operations
preserve their reference counts, and reference overflow is rejected.

An acquire failure leaves the prior applied state unchanged and logs the error;
it is not reported as a successful wake hold. A failed initial guard acquisition
prevents light-sleep configuration. Init failure releases/deletes only resources
it owns, retains failed cleanup, and retries it before another configuration.
This does not validate UART/peripheral wake sources or enable PM in this profile.

`python3 tools/power_policy/test_power_policy.py` tests the real manager with
ASan/UBSan, PM enabled/disabled and real concurrent host threads. Coverage includes
pre-init inputs, init/guard/configure failures, retained cleanup/retry, USB input
retry, client balance, concurrent USB/client transitions and the disabled no-op
contract. Actual sleep/wake, USB/UART behavior and current draw remain #135 bench
acceptance.
