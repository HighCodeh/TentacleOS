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
The host sends CRLF so commands also complete when ESP-IDF selects its plain
terminal mode after startup without a terminal emulator.

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

The lab firmware now provides `rfid emutest 3000 0`: a bounded EM4100 RF/64
Manchester waveform with synthetic ID `0102030405`. Remove physical tags, place
High Boy over the Flipper LF antenna, close the Flipper RFID UI application and
run `rfid read normal` on its CLI before starting the High Boy command. Verify
that Flipper prints **EM4100 0102030405**, then confirm `rfid status` shows idle,
field OFF, GPIO2=0 and GPIO11=0. Flipper stops reading automatically on success;
otherwise stop its reader with Ctrl+C. The optional final argument `1` inverts
the modulation polarity for hardware comparison. Duration is 100–5000 ms.

Without a detected external field, emulation is refused. `reset_required=1`
means cleanup was uncertain and the antenna/no-sleep hold remains reserved:
reset the device before continuing. Do not navigate RF apps during the test.
The saved-tag emulation UI remains unavailable. `pair.py` read/load/deep modes
do not run `emutest`; their T2 `modtest` pulse is not tag emulation acceptance.

## Direct firmware commands

For manual oscilloscope activation without reflashing, use the dedicated panel:

```sh
python3 -u tools/hardware_lab/scope.py \
  --highboy /dev/SELECT_P4_PORT --flipper /dev/SELECT_FLIPPER_PORT
```

Opening this panel does not open serial ports or start RF. `emular` runs synthetic
EM4100 `0102030405` for 5 seconds, `invertido` reverses its polarity, `pulso` toggles
T2 at 1 kHz for 1 second, and `externo` measures the Flipper field for 1 second.
With a selected, connected `--flipper` port these commands start and stop its
reader automatically and close its running application first. Without that USB
connection, the panel controls only High Boy: any external reader can supply the
field manually, including a battery-powered Flipper. Missing Flipper USB never
blocks High Boy-only tests. Modulation still requires a detected external field.
`campo` generates High Boy's own field for
1 second and `ler` reads a physical tag for 5 seconds, with the Flipper reader off
and an external-field check before activation. `status` checks firmware and idle
outputs; `parar` requests OFF on both connected devices; `sair` closes the panel.
An optional count, such as `emular 6`, repeats 1–12 bounded cycles with OFF checks
between them. Ctrl+C during a test cancels remaining cycles, lets the current
bounded firmware command finish, then checks OFF; it is not an immediate hardware
stop. Closing the terminal requests the same cleanup via SIGHUP. USB loss or a
forced process kill cannot guarantee OFF. Unconfirmed cleanup ends the panel.

Each activation saves a separate `scope-*` directory with raw serial events and
the Flipper transcript. Natural Flipper completion after decoding is handled
without requiring a second stop response. Command completion and a decoded ID
are reported separately; neither replaces GPIO11/gate and coil scope captures.
Use `--command status` for a single status check, or another panel action for one
bounded activation. Probe GPIO11 relative to board GND and the coil differentially.
Manchester half-bit timing is approximately 256 us at 125 kHz; `pulso` is a load
path test, not a protocol-valid tag waveform.

For a direct High Boy console, with the panel idle and no other serial client:

```sh
python3 -m serial.tools.miniterm --dtr 0 --rts 0 --eol CRLF --raw /dev/SELECT_P4_PORT 115200
```

Press Enter to obtain `highboy>`, then use the firmware commands below. Ctrl+]
exits this direct terminal; it does not request RF OFF. Prefer bounded `field`,
`watch` and `emutest` commands and verify `rfid off` / `rfid status` before closing.
The direct console has no Flipper USB dependency; load modulation still needs an
external reader field. Do not run both this console and the panel concurrently.

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

`rfid drive <frequency_hz> <duration_ms>` provides a bounded GPIO2-only bench
carrier at 20–200 kHz and 50% duty, for 10–5000 ms. GPIO11 stays low; the command
holds a no-sleep lease and turns the outputs off before returning. It refuses an
occupied frontend or detected external LF carrier. Normal `rfid on` and `sweep`
retain their 110–140 kHz limits. For the requested 19-step scope test, issue
`rfid drive 20000 5000`, then 30000 through 200000 Hz in 10000 Hz increments,
checking idle between commands. This gives 95 seconds of requested active time
plus short OFF/USB intervals. `configured` is an LEDC setting; scope measurements
are required for amplitude, actual frequency and resonance. No ID is transmitted.

1. With reference devices away, run `rfid watch 5`. Check GPIO2's 125 kHz waveform
   and GPIO11 held low, then T4/T3/T5 and coil drive. GPIO2 must not stay statically
   high during reading. Use a differential measurement
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
The drive regression executes the actual carrier/load routines, checking GPIO2
PWM with GPIO11 low, passive outputs, isolated load pulses, start failure cleanup
and a new read after load testing. The emulation regression checks four independent frame vectors, both polarities,
110–140 kHz timing, duration bounds, missing-field/busy refusal, RMT allocation,
transmit/timeout failures and retained ownership after cleanup errors with UBSan.
Mocks do not establish RF performance or physical reset timing. Per-device results
belong in private generated evidence, not this source tree.
