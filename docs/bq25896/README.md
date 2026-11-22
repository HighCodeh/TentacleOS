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

# BQ25896 charger and battery service

## High Boy V2.03

Battery profiles are selected only from the P4 console. The Battery screen
shows the active model and nominal capacity; LEFT/RIGHT cannot change them.
The catalog is compiled into `battery_service.c` and cannot accept arbitrary
charge parameters from a command or configuration file.

| Profile ID | Label | Nominal capacity / voltage | Provenance |
| --- | --- | --- | --- |
| `knlypow_604858` | KNLYPOW 604858 | 2000 mAh / 3.7 V | Owner-confirmed label and QR-linked KNLYPOW/Yachen A0 specification, 2026-07-11 |
| `generic_2000` | Generic | 2000 mAh / 3.7 V | Legacy capacity selection; model unknown |
| `generic_2450` | Generic | 2450 mAh / 3.7 V | Legacy default; model unknown |

```text
battery profiles
battery profile
battery profile knlypow_604858
```

The last command saves `battery_profile` in `config/system.conf` through the
existing atomic writer, then applies the selection without rebooting. Failure
keeps the previous runtime selection and reports the storage error. Overlapping system-config
load/save requests are rejected as busy before touching the file. The console reports the
selected profile in `battery` telemetry. Unknown IDs are rejected. Existing
capacity-only configurations migrate to a generic profile of that capacity;
2000 mAh does not identify which KNLYPOW model is installed. The default remains
`generic_2450` until explicitly selected. A known ID overrides a conflicting
legacy capacity field; an unknown/malformed ID falls back to the legacy generic
capacity (2450 mAh when that field is also invalid).

The `knlypow_604858` entry permits normal charging with a fresh measured voltage
strictly above 3000 mV and at most 4200 mV. This fixes the former 3200 mV lockout
at the reported 3024 mV after a normal discharge. Select this profile explicitly;
the two generic entries retain their 3200 mV minimum. The mistakenly reported
504858 is not a catalog entry. A stale/unknown ID follows the generic migration
rule above. The worker snapshots the selected policy once per polling cycle;
changing to a generic profile stops charging below its minimum on the next cycle.

The confirmed [QR page](https://qr61.cn/oY4Nxr/qirer5U?format=md) links the
[604858 A0 specification](https://ncstatic.clewm.net/rsrc/2026/1028/10/f365eb385c4c48af9d816db416b71eba.pdf),
cover dated 2026-07-11, SHA-256
`29b87db04d15839c23b57efe58296de13437a242f02c7a2aa7c269c79aaaf8da`.
Page 3 specifies 2000 mAh / 3.7 V, 400 mA standard charge, a 4.2 V charge ceiling,
3.0 V discharge cutoff and 10–45 C charge temperature. Section 9.4 on page 12
requires 0.01 C (20 mA) precharge for overdischarged cells, with a 15–30 minute
limit to reach above 3.0 V. Normal charging above the cutoff is distinct from
that recovery procedure. This source replaces the earlier specification hash
recorded here; no claim is made that the two PDF revisions are identical.

Selection does not change charge current/voltage register limits, the voltage
estimate, the gauge model or temperature qualification. The catalog's recovery
current is specification metadata, not a programmed current; zero means unknown.
All profiles remain subject to the charger protections below.

The gauge's observed 3000 mAh design/full capacity does not match this battery.
`POWER_PROFILE_GAUGE_QUALIFIED` remains false: matching the nominal capacity
alone is insufficient to qualify the cell's CEDV model, board current sensing
and temperature input. No live gauge RAM, OTP or chemistry profile is changed
by profile selection. The read-only BQ27220 driver cannot write the gauge capacity or chemistry.

The BQ25896 is I2C0 address 0x6B, with active-low CE on P4 GPIO33.
The BQ27220 gauge is sampled independently. `power_profile.h` records the
reported nominal capacity and retained charge limits; the runtime retains 384 mA
and 4176 mV. The profile-specific lower voltage boundary changes normal-charge eligibility;
charger register limits, the gauge model, NTC interpretation, precharge, OTG,
partitions and C5 firmware are unchanged.

### Charging and protection

`battery_service_request_charge(bool)` queues a request. Its background worker
owns I2C control; a UI request cannot bypass the guard. Charging requires a
fresh ADC sample, valid USB power, battery voltage in the selected profile's
range, no live charger fault, board TS ratio 50–65% REGN, OTG off, and valid
existing current/voltage limits. A read/control failure stops charging. The
service rechecks the post-enable sample and stops if a guard changed. Board TS
is **not validated cell temperature**: the 10–45 C cell requirement has not been
qualified on hardware. There is no claim of fully validated charging safety or
calibrated voltage/current accuracy from host tests.

`bq25896_set_charge_enable_for_profile()` independently enforces the selected
minimum with fresh conversions before enabling charge. The existing
`bq25896_set_charge_enable()` API retains the generic 3200 mV guard. Invalid
profiles fail closed. Neither API accepts arbitrary voltage/current values.
The lowest qualifying integer reading for the 604858 is 3001 mV; the BQ25896
ADC advances in 20 mV steps from 2304 mV, so its first qualifying code is 3004 mV.
These are measured-voltage decisions, not guarantees about the cell voltage's
measurement error. The 3024 mV incident is covered by register-level host tests.

A fresh reading of a present cell (above the ADC floor of 2304 mV) at or below
3000 mV latches `is_recovery_required` in the driver for the rest of that boot.
The service reports `Low cell: recovery unsupported`. A voltage rebound, profile
change, Pause/Resume or driver reinitialization cannot clear that latch. It is
RAM state, not persistent cell history: rebooting does not establish that a cell
has recovered. Missing/invalid ADC samples do not create a recovery latch.

The 604858 specification calls for 20 mA recovery and 40 mA termination. The
charger's minimum programmable precharge and termination currents are 64 mA;
the retained settings are 128 mA precharge and 64 mA termination. The change does
not implement the manufacturer's overdischarge recovery procedure or claim exact
termination compliance. No PWM averaging or protection bypass is used.
See [TI BQ25896 datasheet, REG05](https://www.ti.com/lit/ds/symlink/bq25896.pdf).

### Manual shutdown

`bq25896_power_off()` has a dedicated masked-write path for ship mode. The V2.03
ADC-only generic writer remains restricted; using it for REG09 would reject every
shutdown with `ESP_ERR_NOT_SUPPORTED`. Shutdown waits up to five seconds for the
driver mutex, allowing an in-flight one-shot ADC/snapshot to finish instead of
failing at the ordinary 300 ms timeout.

Under that mutex it clears only `REG03.OTG_CONFIG` with checked readback, drives
the OTG pin low, reads REG09, then sends the final command with `BATFET_DIS=1`,
`BATFET_DLY=1` and `BATFET_RST_EN=0`. All other register bits and charge limits are
preserved. Preparation failures stop before arming the cut. The final I2C write
must be acknowledged; there is no subsequent fallible readback after arming it.
The UI retains storage and application navigation on reported command failure,
releases the bridge guard and allows retry. A timeout has its own on-screen
message; errors also retain their ESP error code in the log.

The delay is specified as 10–15 seconds in the
[TI BQ25896 datasheet, timing table and §9.2.10](https://www.ti.com/lit/ds/symlink/bq25896.pdf).
USB can still hold SYS up; the shutdown screen asks for the cable to be removed.
Generic charge/OTG setters remain subject to the V2.03 driver guard. A successful
I2C command is not a physical measurement that power has fallen.

`python3 tools/highboy/tests/test_shutdown.py` runs the actual charger and UI
shutdown function under ASan/UBSan with mocked I2C/GPIO/RTOS, for V2.03 and the
generic build. It covers the previous REG09 rejection, unchanged charge limits,
bridge contention, long ADC ownership, allocation/lock/read/write/GPIO failures,
retry, duplicate requests and the USB presentation path. Physical battery-only
cutoff and wake-up remain device checks.

### Telemetry and presentation

`bq25896_read_telemetry()` starts a fresh one-shot ADC conversion, observes
start/completion and reads live/latched faults. It is bounded to 1.2 seconds plus
I2C transfers. The charger mutex covers the transaction while the shared bus
is released between polls. Invalid samples must not be consumed.

`battery_service_get()` publishes a mutex-protected snapshot with these rules:

- `valid`: fresh charger telemetry, independent of a calibrated percentage.
- `present`: a valid voltage above the charger's 2304 mV ADC floor.
- `soc`: qualified gauge value or filtered voltage estimate, 0–100; -1 when
  no valid battery reading is available. Gauge values still require explicit
  model qualification, a design capacity matching the selected nominal capacity and plausible
  voltage/capacity fields. Otherwise the service uses the voltage filter below.
- `is_soc_estimated`: true for the voltage fallback. A capacity-only gauge rewrite
  does not mark an estimate as calibrated.
- `charging` and `vbus_present`: actual charger state. USB alone is not charge.
- `low`: on battery only, hysteresis at 15/20% if a qualified gauge is present;
  otherwise 3300/3500 mV. Unknown percentage is never treated as negative charge.

`battery_service_get_charge()` also returns the request, reason, full charger
telemetry, and independently valid raw gauge current. An unqualified
model may supply a voltage/current reading but not a claimed calibrated percent.
No voltage-to-percent substitution is disguised as calibration.

Battery and Power both consume the service snapshot. Power no longer starts
ADC conversions or initializes the charger on the UI thread; its charge toggle
queues the same service-owned request as Battery. A pause survives telemetry
polls and screen changes; a reboot starts with charging requested again.
Power shows the same filtered estimate as the header and dropdown when the
gauge is unqualified, prefixed with `~`; unavailable readings show `--%`.

The battery information card shows percent, with `~` and "Estimated from voltage"
for the fallback, alongside measured voltage, labeled net current and supply,
active model and nominal capacity, actual charging/paused/full state and the
blocked reason. The screen is read-only for profile selection. The config service
validates a catalog ID and calls `battery_service_set_profile()` only after saving;
the immutable catalog index is shared atomically with the worker and renderers.
This setting changes normal-charge eligibility for the confirmed 604858,
but not the voltage-to-percent curve, charger current/voltage limits or gauge calibration. OK
pauses or requests charging; all guards remain active. Header and dropdown use
that same estimate to fill the battery icon and display `~NN%`. A lightning/USB
symbol reports actual charging/external power separately. The header does not
substitute voltage beside the icon when percentage is unknown; it shows `--`.
Voltage remains available in Battery, Power and the dropdown. Unavailable telemetry
clears stale percentage, voltage and charging indication. The four icon levels
cover 0–24%, 25–49%, 50–74% and 75–100%; a full icon is the top quarter, not a
claim that charging has completed. While actually charging, the live header
cycles the four frames at approximately 350 ms per frame, retaining the measured
percentage text. Paused, full, missing and invalid states stop the animation.
LVGL owns the animation on its image object and deletes it with the object;
rebinding a new header cancels the old animation. Static overlays keep a static
snapshot. Animation does not add I2C or SPI polling.

V2.03 low-cell decisions remain based on voltage when the percentage is estimated:
shutdown at 3000 mV, critical warning at 3200 mV, and low hysteresis at 3300/3500 mV.
The filtered estimate cannot delay these thresholds or cause an early shutdown.
Existing sample count/debounce and USB exemption are preserved.

The normal charging LED stays off while the backlight is on. With the screen
dark, it pulses red for 200 ms every 10 seconds. Entering charge-done permits
one brief green indication, then off; repeated telemetry does not extend it.
Application notifications and activity patterns take priority, including their
dark gaps. Blocked/low warning patterns remain available. State-change logs are
deduplicated. See [LED](../led/README.md) for brightness and priority rules.

Host regression coverage includes register-level normal-charge startup at 3024 mV,
generic-profile rejection, boundary values, low-cell/rebound latching, USB/fault/
ADC/I2C failures, second-conversion voltage drops, and all catalog profiles across a real config
save/load, console list/show/select, invalid/legacy fields, serializer allocation
failures, failed-save rollback, read-only UI controls, icon band boundaries,
charge animation transitions and static overlay independence. Native LVGL checks
cover 240x320/320x240, light/dark themes and repeated screen teardown. These tests use
host I/O and telemetry mocks; they do not validate physical battery accuracy.

## Voltage estimate and filtering

Both targets reuse the voltage filter from `c6ff7cfa`. The V2.03 path falls back
to it when a qualified gauge value is unavailable. Raw percent is the existing
linear 3200–4200 mV curve, clamped to 0–100; 3700 mV starts at approximately 50%.
This is an estimate under the current load, not a calibrated fuel gauge.

The EMA rejects short voltage changes. A source/charging transition settles for
30 valid polls and anchors the estimate across the voltage step. Subsequent
changes are limited to one percentage point per 30 valid polls; idle USB cannot
increase the estimate, and the filter reserves 100% for charge completion.
Invalid readings are not fed into the filter and publish unavailable SoC;
confirmed battery absence clears the filter for a future insertion. The estimate
and its anchors are RAM-only and initialize again after a reboot.

## Validation

### Reported hardware charging, 2026-11-22

After selecting and saving `knlypow_604858` and requesting charge, the owner
reported `status=Charging`, charge enabled in fast-charge mode, 3564 mV VBAT
and 350 mA measured by the BQ25896. The programmed limits remained 384 mA and
4176 mV, with both live and latched charger faults zero. The console confirmed
that the selected profile was saved to `/sdcard/config/system.conf`.
This is one reported charging observation after the previous 3024 mV lockout;
it does not qualify a full charge cycle, capacity, gauge accuracy, cell
thermometry or overdischarge recovery.

### Host checks

`python3 tools/highboy/tests/test_battery_filter.py` retains the upstream
regression cases for repeated cable transitions, stable voltage, real changes,
empty/full endpoints and idle USB in the shared filter.

`python3 tools/highboy/tests/test_power_feedback.py` runs the production V2.03
service against mocked telemetry/control, covering voltage fallback with an
unqualified or unavailable gauge, USB transition filtering, invalid/absent battery
readings, charging, paused requests, low voltage, ADC errors, OTG,
current-limit and fault rejection. `test_battery_ui.py` exercises the production
renderer against charging/full/paused/unavailable states and timer cleanup.
`test_battery_consistency.py` executes the header, dropdown and Power renderers
against contradictory charger/gauge values, invalid samples, paused/charging
states, queued charge requests and voltage-based V2.03 shutdown with an estimated
percentage. These tests do not validate physical
temperature sensing, battery capacity or processor sleep/wake. The integrated
processor PM configuration remains disabled pending bench wake/current testing;
see [power manager](../power_manager/README.md).
