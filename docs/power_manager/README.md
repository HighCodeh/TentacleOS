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

# Power management

The P4 service owns the persistent **Battery saver** preference and the display
state forwarded to the C5. It defaults to enabled and is saved in
`/sdcard/config/system.conf`; missing keys in older configurations use that default.
Settings > Power toggles it with checked persistence and rollback on save failure.

## USB input budget on V2.03

`Service/power_manager/usb_power.c` owns the system-lifetime USB input worker.
After charger and LED initialization, it polls the HUSB320 source snapshot and
programs only the BQ25896 input budget. The policy applies with the display on
or off, on either the UART bridge or native USB path; it does not depend on a
screen or on Bluetooth/Wi-Fi. Initialization failure leaves the boot input limit
at 500 mA and is recorded in the boot report.

| Observed source | BQ25896 input ceiling |
| --- | --- |
| Normal sink, VBUS present, consistent CC advertisement of 1.5 A or 3 A for at least 250 ms | 900 mA |
| Default USB current, disconnected, accessory, invalid/inconsistent reading or I2C failure | 500 mA fallback |

The fallback preserves the board's existing 500 mA policy; it is not proof of
USB enumeration or suspend-current compliance. CC default does not distinguish
a computer from a wall charger. USB-A chargers without a higher CC advertisement
stay at the fallback. The service never requests USB-PD voltages or enables OTG.

R11 = 390 ohms on the V2.03 schematic limits input through the BQ25896 ILIM pin
(355 A-ohms nominal / 390 ohms, approximately 910 mA; silicon tolerance applies).
The 900 mA software ceiling retains EN_ILIM; it cannot override that hardware
limit. See the [TI BQ25896 datasheet](https://www.ti.com/lit/ds/symlink/bq25896.pdf),
Input Current Limit and ILIM specifications. This is USB input current, shared
by the system and charger, not cell current. The battery policy still limits
charge to 384 mA / 4176 mV. The sampled effective limit may be lower because
of ICO, input-voltage regulation or ILIM; 900 mA is a ceiling, not a guaranteed
measured draw. ICO and VINDPM remain enabled as previously configured. Board TS has not been qualified as cell temperature;
this change does not enable the cell's 1 A maximum-charge specification.

The high-budget polling interval is 20 ms; fallback polling is 250 ms. A lower,
missing or invalid advertisement drops the request on the first observed sample;
an upgrade requires fresh stability again. The HUSB320 driver checks sink role,
enable, attachment, orientation, VBUS, TYPE and matching STATUS reads. Failed
sink initialization is retried at most every 2 s, after reducing the input budget.
I2C transfers have bounded waits, but shared-bus scheduling and physical downgrade
latency still require hardware measurement; these are not hard real-time claims.

`bq25896_set_usb_input_limit()` owns REG00 after boot under a separate mutex.
This prevents the main driver's long ADC conversion wait from holding up a
source downgrade. Generic V2.03 register writes still reject REG00 mutations.
The worker reads back every applied limit, preserving external HIZ and forcing
EN_ILIM on. A transfer/readback error attempts HIZ input isolation and then the
worker retries 500 mA; the published applied current stays zero until verified.
A persistent bus failure cannot guarantee that a write reached silicon. The
worker clears only its own isolation on successful retry, never enables cell
charging, and leaves the battery fault/voltage/recovery checks in force.

`usb_power_get()` returns a synchronized snapshot, rejecting data older than
one second. `battery` reports CC advertisement, attachment, verified input budget
and separate `source=` / `control=` errors, plus the last HUSB320 register
accessed on a source failure. `control=ESP_OK` confirms the input-limit operation,
not successful USB-C detection. These are separate from the BQ25896 PSEL source code. The Battery
screen shows the actual sampled input register limit beneath USB voltage;
net cell current remains a separate field. Logs and LED feedback are emitted on
policy changes and rate limited together to one event per five seconds.

Host validation: `tools/power_policy/test_usb_power.py` covers source changes,
stability, downgrade, stale snapshots, initialization and control failures.
`tools/board_control/test_consumers.py` exercises the actual HUSB320 transactions;
`tools/highboy/tests/test_charge_profile.py` covers the actual REG00 path while
the ADC mutex is held, HIZ ownership, readback/transfer failures and unchanged
cell limits. These run with ASan/UBSan and are included in `tools/highboy/test.command`.

Reported hardware validation on 2026-11-22: the corrected app passed the USB-C
boot stage, detected a 3 A CC advertisement and raised the verified input limit
from 500 to 900 mA. The charger reported an effective 900 mA input limit, 350 mA
cell current with ICHG still 384 mA, no input-current DPM and no latched/live
faults. These are device register/ADC readings, not external current measurements.
Hardware acceptance still requires both cable orientations, charger-to-computer
swaps, disconnect/reconnect, screen sleep and real input/cell current measurements.

## C5 idle sleep

`Service/power_manager` checks boot readiness, Wi-Fi (including capture), BLE
resources (including failed cleanup), an active app session, Grok, ESP-NOW,
remote IR, queued asynchronous scans, OTA and the board outputs. Any active or uncertain owner vetoes sleep.
IR must be closed with hardware released; the amplifier must be disabled and
haptic reset asserted with known GPIO state. Radio enable preferences and active
connections are never changed to obtain sleep. The P4 skips idle requests while
amp/haptic outputs are active, unknown or pending; a confirmed BUSY reply also
avoids unnecessary wake handshakes during active radio work.

After the existing P4 board worker refreshes controls, it may send the revisioned
`SPI_ID_SYSTEM_POWER_STATE` request if the peer advertises `SPI_BOARD_CAP_SLEEP`.
`IDLE` permits a 100 ms parked interval; a fully off display selects `SLEEP`, up
to 1000 ms. Dimming alone does not select the longer interval. Disabling the
preference stops these requests; the next normal board poll wakes the peer.
Older C5 firmware receives no sleep requests.

The C5 sends the reply without queuing the usual next RX. Only after the TX
completion has returned DMA ownership does `Drivers/spi_slave` disable SPI,
release its wake guard and block the bridge task. Tickless idle/ESP-IDF decides
when all tasks and PM locks permit actual light sleep. Pending or unknown DMA
ownership, incomplete replies and failed PM initialization prohibit suspension.
The activity checks run again after the reply. Cached reply fetches do not repeat
the sleep operation.

GPIO23 (bridge CS, active low) wakes the C5. Its interrupt disables itself and
notifies the bridge task; timeout also resumes it. The task reacquires its wake
guard and enables SPI before arming RX. The P4 marks a potentially sleeping peer
before sending the idle request, including uncertain outcomes, and requires a
successful retryable transport HELLO before its next application command. A
wake-up clock can therefore be discarded without replaying a mutating command.
Transport sessions, response caching, RAM and board epochs survive light sleep.
The board heartbeat and supervisor continue with their existing budgets.

C5 defaults enable PM and tickless idle. Maximum CPU frequency remains the
configured 240 MHz; idle DFS can use 80 MHz. A permanent bridge wake guard is
released only inside the negotiated parked interval. Peripheral power-down is
disabled, preserving the digital GPIO wake path. Board output pins opt out of
GPIO sleep isolation so amp/reset/USB/IR levels remain stable; INTN retains its pull-up. A partial PM initialization
keeps the bridge awake and omits its sleep capability. Direct C5 UART OTA is
covered by the OTA guard; ordinary P4 console use remains available.

This is light sleep, not deep sleep or zero current. Timers and housekeeping
still wake the C5, and the P4 remains running for input, charging, LED feedback
and its applications. P4 CPU sleep remains disabled because its UART/USB wake
path has not been validated. Deep sleep would reboot the C5 and invalidate its
sessions, so it is not used for this mode.

## C5 memory placement

Enabling the SDK PM code increases linked internal RAM use. To retain the
existing CI floor and C5 radio-memory configuration, the bridge stream queue
and wire-format AP scan snapshots use checked PSRAM allocations. The queue is
allocated before bridge workers start; snapshots are allocated before accepting
their first scan. Allocation failure rejects the dependent operation, with no
internal-RAM fallback. Both allocations live until reboot because persistent
workers and the generic result pipe retain references. Stream producers are
task-only (including Wi-Fi task callbacks); ISR calls are rejected. SPI DMA
buffers/descriptors, PM locks, counters and synchronization remain internal.

## Validation

Host tests execute the production idle guards, PM/DMA transition and P4 transport
helpers with fake clocks/SDK calls: pending DMA, an asserted CS, delayed wake,
PM failures, every activity veto, configuration round-trip and failed-save
rollback. These are not electrical wake-up or current measurements. Both app
images are required for this feature; app-only updates do not install assets.
Validate on hardware with Wi-Fi/BLE disabled, screen on/off and rapid wake plus
audio/haptic commands; then repeat with each active operation and confirm it
continues. Compare current draw and check that no false link resets occur.

## P4 CPU power-manager support

The existing P4 `esp_pm` branch below is host-tested support and remains disabled
in the integrated profile. Companion policy/state APIs work independently of
that compile-time gate.

## Overview

- **Location:** `firmware_p4/components/Service/power_manager/`
- **Header:** `include/power_manager.h`
- **Sources:** `power_manager.c` (esp_pm setup + lock), `power_manager_usb.c` (TinyUSB bus callbacks)
- **Dependencies:** `esp_pm`, `esp_log`, `esp_err`, `tusb` (for the USB callbacks)
- **Gated by:** `CONFIG_PM_ENABLE`. When P4 PM is off, its CPU lock/configuration operations are no-ops (USB state and companion policy are still tracked); the P4 stays at a fixed frequency.
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
| `CONFIG_PM_ENABLE` | P4 sdkconfig | - | P4 CPU PM gate; companion policy is independent. |


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

## V2.03 shutdown and C5 recovery

The UI power-off path acquires the bridge maintenance guard before arming the
BATFET cut. If recovery already owns the bridge, shutdown is deferred and can
be retried; an I2C power-off failure releases the guard. Successful shutdown
retains it. The charger uses a dedicated REG09 ship-mode write, preserving the
V2.03 generic-register guard, and waits for ongoing ADC ownership before arming
the delayed cut. A charger timeout is shown separately from other failures; the
failed request leaves application navigation and storage mounted for retry.
See [charger shutdown](../bq25896/README.md#manual-shutdown) for sequencing and
host-test scope. The restart/shutdown hook additionally disables new automatic
C5 recovery. See [bridge manager](../bridge_manager/README.md).
