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

# High Boy Lab terminal

Laboratory tools on `lab/rf`; no production menu, boot setting, charger limit or
CP2105 OTP is changed by opening the tool. Python 3.10+ and `pyserial==3.5` are
required. The terminal prints each command and response as they arrive.

```sh
python3 tools/hardware_lab/menu.py
python3 tools/hardware_lab/lab.py ports
python3 tools/hardware_lab/lab.py plan
```

Choose the CP2105 P4 port explicitly for console tests. Do not infer P4/C5 from a
port suffix on another operating system. The console and firmware identity must
respond before activation. USB tests use the explicitly selected P4/C5 target
and require a fresh matching ROM banner. Close qFlipper and other serial clients.
Use a single operator; do not navigate RF applications during a console test.

## RFID with a Flipper reference

```sh
python3 -u tools/hardware_lab/pair.py \
  --highboy /dev/SELECT_P4_PORT --flipper /dev/SELECT_FLIPPER_PORT \
  --label 'board serial; capacitor reference and value' --mode deep
```

Keep antenna position, distance, firmware and test ID identical between boards.
The fixed synthetic EM4100 ID is `0102030405`; no card is written. Record which
capacitor changed, its old/new values, probe placement and power arrangement.
`read` uses Flipper emulation and High Boy self-test/sweep/field/DA/read commands.
`load` uses Flipper ASK reader and High Boy passive reception/T2 load pulses.
`deep` runs both, with three repetitions of field/read/passive checks.

The installed High Boy command set has no protocol-valid LF tag emulation
command. T2 `modtest` only produces a 1 kHz load pulse for 1 second. Its completion
is **not** EM4100 emulation acceptance. A reader failing to decode that pulse
must not be reported as a failed tag-emulation test.

## Direct firmware commands

At the High Boy `highboy>` prompt, with other reader fields OFF:

```text
rfid status
rfid selftest
rfid field 1000
rfid sweep 118000 132000 1000 100
rfid raw 250
rfid watch 5
rfid off
rfid status
```

With Flipper in `rfid read normal`, and High Boy's own field OFF:

```text
rfid listen 1000
rfid modtest 1000 1000
```

On Flipper, use `rfid emulate EM4100 0102030405` for High Boy reader tests;
Ctrl+C stops either Flipper operation. The paired tool performs and verifies this
cleanup automatically. It uses CDC DTR to open Flipper CLI; High Boy serial opens
with DTR/RTS released. OS/adapter open-time glitches still require measurement.

## Reading results

Each session saves `report.json`, `events.jsonl` (including raw RX bytes as base64)
and `summary.md`. Paired runs also save `comparison.json` and `flipper.json`.
Default output is the ignored `integration/hardware_lab/`; use `--output` to
select another private evidence directory. Generated files get license notices.

`complete` means acquisition and cleanup completed, **not that the board passed**.
Inspect each row: `observed`, `not_detected`, `not_decoded` or `failed`.
A missing response, malformed/truncated capture, unexpected boot, wrong chip,
concurrent operation or unconfirmed cleanup cannot produce a successful test.
Instrument verification remains pending until scope/reference measurements are
recorded separately. USB timestamps are host request times, not measured EN edges.

Carrier frequency comes from digital CL/PCNT feedback. A configured LEDC frequency
is not a measured coil frequency. A sweep of CL counts is not an analog resonance
measurement. DA silence alone does not prove a receiver defect when the tag may
not be energized. Older firmware can print self-test frequency PASS without a
carrier; the host reports `frequency_measured=false` in that case.

## Oscilloscope sequence

1. With reference devices away, run `rfid watch 5`. Check GPIO2 enable and GPIO11
   125 kHz waveform, then T4/T3/T5 and coil drive. Use a differential measurement
   across the coil; do not ground a floating coil terminal with an earth clip.
2. Once a usable field is confirmed, add the known tag and follow U11 pin 1,
   U14 pin 1, U14 pin 7 and DA/GPIO16. Verify the actual capacitor designators
   against the board revision before interpreting a 4.7 uF -> 4.7 nF replacement.
3. Use Flipper reader with High Boy passive listen to distinguish external-field
   detection from own-field generation. GPIO11/T2 load pulses require a reader
   envelope measurement; software completion does not verify RF modulation.

## USB and other diagnostics

`lab.py run usb-reset --port PORT --chip p4` requests RTS reset and checks ROM
identity, flash boot and application startup. `usb-boot` requests the DTR/RTS
sequence, checks download mode plus a read-only ROM SYNC ACK, then resets toward
application boot. It never writes flash, RAM code, eFuses or USB bridge OTP.
Failure to recover is recorded; a manual reset/power cycle can still be needed.
For a blank board, application startup cannot pass until firmware is installed.

Scope CP2105 DTR/RTS, the reset stage, EN and P4 GPIO35/36 or C5 GPIO28/27.
CP2105 GPIO/Modem configuration is an independent hardware provisioning check;
this tool does not read or program that configuration. See
[CP2105 datasheet](https://www.silabs.com/documents/public/data-sheets/CP2105.pdf),
[Espressif boot selection](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/boot-mode-selection.html)
and [ROM protocol](https://docs.espressif.com/projects/esptool/en/latest/esp32p4/advanced-topics/serial-protocol.html).

Other presets capture battery telemetry, qualify 60 P4/C5 SPI requests, measure
NFC field amplitude, receive IR, and run bounded CC1101/SX1262 RX cases through
[RF chamber](../rf_chamber/README.md). RF transmission matrices remain in that
existing tool. Those presets do not constitute a full production acceptance test.

Ctrl+C requests stopping between bounded commands. A synchronous firmware command
finishes its device-side deadline before cleanup, up to about 20 seconds for SPI
qualification. No indefinite `rfid on` or `ir flash on` command is used. On USB
loss, OFF is unconfirmed; inspect the device before hardware rework. A terminated
host process cannot guarantee cleanup of a disconnected or hung device.

## Validation

```sh
python3 -m unittest discover -s tools/hardware_lab -p 'test_*.py' -v
```

Host tests cover parsing, missing/truncated data, no-carrier frequency reporting,
external-field inhibition, cancellation, cleanup failures, ROM SYNC validation,
wrong-chip logs, serial failure evidence and duplicate operations. The self-test
regression compiles the actual C function with synthetic feedback and UBSan.
Mocks do not establish RF performance or physical reset timing. Per-device results
belong in private generated evidence, not this source tree.
