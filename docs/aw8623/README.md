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

# AW8623 — V2.03 haptics

## Actuator and drive reference

The supplied `High Boy_V2_03 BOM 2026-10-19.xlsx`, sheet1 row 60, identifies M1
as DMEGC **DM-YX406A-3**, an ERM motor (10,000 RPM, 0.65 g). Row 128 identifies
U23 as **AW8623DNR**. The schematic M1 symbol has a different part label;
the BOM is the migration reference, pending assembly verification.

On 2026-11-03 the project adopted **3.0 V as its fixed engineering reference**
for normal operation. This is not a manufacturer-verified rating: the exact
motor voltage/current datasheet is unavailable. There is no provisional-mode
gate in the firmware.

Playback uses a finite constant-amplitude RAM waveform at 12 ksample/s, with
no looping, overdrive, braking, external trigger or automatic VBAT compensation.
Intensity is 1–100%, duration 1–500 ms. The conservative full-scale supply model
uses 5.5 V, signed sample maximum 127 and gain register 0x80:

`sample = floor(127 * 3000 * intensity / (5500 * 100))`

The maximum sample is 69. This is an amplitude model, **not closed-loop 3.0 V
regulation**; actual terminal voltage, startup torque and current must be measured
on the assembled board. Low intensity may not overcome the ERM starting torque.
DC waveform protection is disabled to permit the ERM's constant-polarity pulse;
chip overcurrent, overtemperature and undervoltage detection remain enabled.

## Wiring and ownership

P4 I2C0 accesses address `0x5A`. C5 owns active-low RSTN at GPIO5 and INTN at
GPIO1. GPIO32 remains B2/OK, never a haptic trigger. C5 boot holds RSTN low.
INTN is open-drain; the GPIO input has its internal pull-up enabled because no
external INTN pull-up is shown on sheet 6. Confirm its rise time on hardware.

A falling-edge ISR only sets an atomic pending flag. Board STATE returns both
the raw input level and that flag; reading STATE does not clear it, so losing
an SPI response does not consume the event. Successfully asserting RSTN clears
the flag. The P4 reads SYSINT over I2C to retrieve/clear the chip cause. The flag
is a coalesced event indicator since reset, not an event counter or decoded cause.
This extends the board-specific contract to revision 2; the shared SPI command
protocol stays v2 and transport envelope stays revision 1. Update both chips.

## Driver and service contracts

`aw8623_probe(controls, out_result)` identifies ID 0x23 and reads SYSST/SYSINT
without writing any I2C register. It asserts reset before and after probing.
`aw8623_play(controls, request)` resets, identifies, configures ERM mode, loads
the finite waveform and starts it once. Both are synchronous task-only calls,
serialized with `aw8623_stop()`; concurrent calls return INVALID_STATE.

RAM starts at 0x0800 with a five-byte header; the 500 ms maximum ends at 0x1f74,
within the 8 KiB SRAM. The worker checks completion, chip faults, cancellation
and board epoch while playing. Completion has a duration-plus-100-ms deadline;
individual I2C calls also have a timeout. A failed GO acknowledgement is never
retried because the motor might already have started.

Every driver exit requests RSTN assertion, including unknown chip, I2C error,
stale epoch and lost release acknowledgement. Reset failure takes precedence in
the result. A device handle whose removal fails is retained for reuse. Board
confirmation describes the GPIO assignment, not an electrical measurement.
The finite RAM waveform bounds playback even if P4 stops servicing it; the C5
board lease additionally retains its existing five-second lost-P4 fallback.

The `Service/haptic` API accepts nonblocking `haptic_play()` and `haptic_probe()`
requests. ESP_OK means accepted, not completed; inspect `haptic_is_busy()` and
`haptic_last_result()`. One worker owns the driver and rejects overlapping
requests instead of queuing or replaying them. `haptic_stop()` requests cancellation.
If reset cannot be confirmed, the worker retains ownership and retries stop every
100 ms. It never restarts a pulse after reconnection or an epoch change.

## UI and feedback

Vibration settings offer intensity, driver check, stop and a 200 ms test pulse.
Leaving the screen requests cancellation. Driver work runs outside LVGL callbacks.
UI read/write/emulate/SD-connect feedback, game cues and NFC feedback request a
70% / 80 ms pulse when the system vibration setting is enabled. Busy feedback is
dropped. The settings intensity controls its test pulse, not the system feedback.

DRV2605L remains excluded from the build. Its numeric ROM effect IDs are not
translated into AW8623 registers; the new consumers use bounded ERM pulses.

## Sources and validation

Register definitions come from the manufacturer-authored
[AW8623 datasheet V1.0, September 2018](https://datasheet.lcsc.com/datasheet/pdf/44639b59bc14030003c451d337b14e6f.pdf?productCode=C401026),
sections Register Detailed Description and Operating Modes. The
[manufacturer product page](https://www.awinic.com/en/productDetail/AW8623DNR)
identifies this part separately from AW8623X variants. Do not substitute their
register maps. The PDF is a reference only, not copied into the repository.

`python3 tools/aw8623/test_aw8623.py` compiles the production driver and service
with ASan/UBSan. It covers waveform bounds/amplitude, every write failure, lost GO
ACK, cancellation before/during playback, epoch loss, faults, completion timeout,
identification/read failures, lost reset-release ACK, retained I2C handles,
contention and ISR rejection. Service tests cover task allocation failure,
exclusive ownership while reset is unconfirmed, stop retries and no replay.

Board tests cover INTN pull-up, ISR setup errors, latch persistence and reset
clear. Hardware acceptance remains: chip identification, actual voltage/current,
starting torque, intensity, finite completion, INTN levels, stop, and I2C/SPI/C5
reset fault injection on the assembled V2.03. Software tests do not replace it.

Paired ESP-IDF v6.1 build passed on 2026-11-03: C5 `0x1fee30` bytes
(`0x11d0` free), P4 `0x2c7a80` bytes (`0x8580` free). Original partitions
and C5 memory policy are preserved; C5 has only about 4.5 KiB free.
