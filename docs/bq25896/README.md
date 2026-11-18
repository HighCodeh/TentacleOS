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

The installed replacement was reported by the owner as 2475 mAh, 3.7 V nominal,
4.2 V maximum on 2026-11-18. `power_profile.h` records these nominal ratings;
they are not automatic pack identification or a measured capacity. The prior
KNLYPOW/Yachen 604858 specification (2000 mAh; A0 dated 2026-10-29, SHA-256
`29d4047b57c25d427abdca5d18d1c8fb26f18cda71ac23370102598fe95ba8c5`)
does not qualify the replacement's recovery/termination current or temperature
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

Ship mode still uses BATFET_DIS;
it cannot turn off a system held up by USB. Generic setters remain subject to
the V2.03 driver guard.

### Telemetry and presentation

`bq25896_read_telemetry()` starts a fresh one-shot ADC conversion, observes
start/completion and reads live/latched faults. It is bounded to 1.2 seconds plus
I2C transfers. The charger mutex covers the transaction while the shared bus
is released between polls. Invalid samples must not be consumed.

`battery_service_get()` publishes a mutex-protected snapshot with these rules:

- `valid`: fresh charger telemetry, independent of a calibrated percentage.
- `present`: a valid voltage above the charger's 2304 mV ADC floor.
- `soc`: qualified gauge value 0–100, or -1. The gate requires explicit
  model qualification, the reported 2475 mAh design capacity and plausible
  voltage/capacity fields. Neither an old 2000 mAh model nor a capacity-only
  rewrite can enable percentage reporting.
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
Power shows `--%` and measured
voltage when the gauge is unqualified, never the old linear voltage percentage.

The battery information card shows voltage when percent is unavailable (without
an empty 0% ring), labeled net current and supply, nominal capacity,
source, actual charging/paused/full state, and the blocked reason. OK pauses or
requests charging; all guards remain active. Header and dropdown show measured voltage with an unqualified gauge, or
percentage when qualified. Their static fill never simulates a charge level; a
lightning/USB symbol reports actual charging/external power separately. Unknown
header readings use a neutral battery, and unavailable telemetry clears stale
voltage and charging indication. No per-battery animation timer is needed. Low-cell shutdown uses 3000 mV
when no percentage is available; critical warning uses 3200 mV. Existing sample
count/debounce and USB exemption are preserved.

The LED resumes a dimmed red charging indication, green full indication, or a
brief amber blocked indication after transient notifications. State-change logs
are deduplicated. See [LED](../led/README.md).

## Legacy target

Non-V2.03 builds retain the voltage estimate and USB-transition filter from
`c6ff7cf`: a source/charging transition settles for 30 polls and anchors the
estimate across the voltage step. Subsequent changes are limited to one
percentage point per 30 polls; idle USB cannot increase the estimate, and 100%
requires charge completion. Startup and later samples require valid ADC data.
Their linear 3200–4200 mV curve is not a calibrated fuel gauge. It is not used
to manufacture a percentage on the integrated V2.03 target.

## Validation

`python3 tools/highboy/tests/test_battery_filter.py` retains the upstream
regression cases for repeated cable transitions, stable voltage, real changes,
empty/full endpoints and idle USB in the legacy branch.

`python3 tools/highboy/tests/test_power_feedback.py` runs the production V2.03
service against mocked telemetry/control, covering unqualified SoC with valid
voltage/current, USB, charging, paused requests, low voltage, ADC errors, OTG,
current-limit and fault rejection. `test_battery_ui.py` exercises the production
renderer against charging/full/paused/unavailable states and timer cleanup.
`test_battery_consistency.py` executes the header, dropdown and Power renderers
against contradictory charger/gauge values, invalid samples, paused/charging
states and queued charge requests. These tests do not validate physical
temperature sensing, battery capacity or processor sleep/wake. The integrated
processor PM configuration remains disabled pending bench wake/current testing;
see [power manager](../power_manager/README.md).
