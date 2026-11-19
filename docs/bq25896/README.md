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

Battery settings offer the owner-selected 2000 and 2450 mAh nominal capacities.
2450 mAh is the default, superseding the earlier reported 2475 mAh value.
Both use the existing single-cell 3.7 V nominal / 4.2 V maximum voltage estimate;
capacity alone does not identify a discharge curve or calibrate the gauge.
The prior KNLYPOW/Yachen 604858 specification (2000 mAh; A0 dated 2026-10-29,
SHA-256 `29d4047b57c25d427abdca5d18d1c8fb26f18cda71ac23370102598fe95ba8c5`)
does not qualify a replacement's recovery/termination current or temperature
sensor. Existing lower charge guards remain unchanged.

The gauge's observed 3000 mAh design/full capacity does not match this battery.
`POWER_PROFILE_GAUGE_QUALIFIED` remains false: matching the nominal capacity
alone is insufficient to qualify the cell's CEDV model, board current sensing
and temperature input. No live gauge RAM, OTP or chemistry profile is changed
by this UI update. The read-only BQ27220 driver cannot write the gauge capacity or chemistry.

The BQ25896 is I2C0 address 0x6B, with active-low CE on P4 GPIO33.
The BQ27220 gauge is sampled independently. `power_profile.h` records the
reported nominal capacity and retained charge limits; the runtime retains 384 mA
and 4176 mV. This update does not change charger register limits, the gauge
model, NTC interpretation, precharge, OTG, partitions or C5 firmware.

### Charging and protection

`battery_service_request_charge(bool)` queues a request. Its background worker
owns I2C control; a UI request cannot bypass the guard. Charging requires a
fresh ADC sample, valid USB power, 3200–4200 mV battery voltage, no live charger
fault, board TS ratio 50–65% REGN, OTG off, and valid existing current/voltage
limits. A read/control failure stops charging. Board TS is **not validated cell
temperature**. No unattended low-cell recovery is implemented.

The old 604858 specification calls for 20 mA recovery and 40 mA termination. The charger's minimum
programmable precharge and termination currents are 64 mA; the retained
precharge setting is 128 mA. These cannot be made compliant by changing a UI
indicator or rounding up. Low voltage therefore remains blocked pending a
compatible recovery method and physical battery/temperature validation.
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
nominal capacity, actual charging/paused/full state and the blocked reason.
LEFT/RIGHT selects 2000 or 2450 mAh and saves `battery_capacity_mah` in
`config/system.conf` through the existing atomic storage writer. A failed save
keeps the previous selection and shows an error; an old/missing/invalid field
loads 2450 mAh. `battery_service_set_capacity()` accepts only these two values
and shares the nominal capacity atomically with the worker. This setting changes
neither the voltage-to-percent curve nor charger limits or gauge calibration. OK
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

Host regression coverage includes both capacity choices across a real config
save/load, invalid/legacy fields, failed-save rollback, icon band boundaries,
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
