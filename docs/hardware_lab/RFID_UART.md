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

# Standalone High Boy RFID UART lab

This separate P4 application is for High Boy V2.03 / P4 silicon v3.1–v3.99
bench work on `lab/rf`. It does not replace the production build or add lab
commands to `dev`. It starts with GPIO2/GPIO11 LOW and waits for a typed command.
The display stays unused; operate through the CP2105 P4 UART. No Flipper USB
connection or host automation is needed. C5 firmware, charger configuration,
NVS, OTA selection, assets and microSD are outside the app-only update.

## Connect and use

Open the P4 CP2105 UART at **115200 baud, 8 data bits, no parity, 1 stop bit,
no hardware/software flow control**. Release DTR/RTS. Use CR, LF or CRLF to send
a command. Windows COM numbering does not establish which CP2105 interface is
P4: select the interface whose banner is `HIGH BOY RFID LAB` / prompt `rfid>`.
The supplied `terminal.py` lists ports and asks for an explicit selection; it
never starts an RF test. An ordinary serial terminal such as PuTTY also works.
UART electrical pins are P4 TX GPIO38, RX GPIO37, 3.3 V logic and common GND;
use the on-board USB bridge normally, never connect an RS-232 voltage output.

| Command | Action |
| --- | --- |
| `help` | List commands and limits. |
| `pins` / `info` / `status` | Pin roles, build identity, current state and input counters. No waveform starts. |
| `field 125 50 5000` | GPIO2, 125 kHz, 50% duty, 5000 ms. GPIO11 LOW. |
| `field 80 30 2000` | GPIO2, 80 kHz, 30% duty, 2000 ms. |
| `sweep` | 20 to 200 kHz, 10 kHz steps, 5 seconds per step, 50% duty. |
| `sweep 120 130 1 1000` | Narrow sweep, 1 second per step. |
| `pulse 1000 50 1000` | GPIO11, 1 kHz, 50%, 1 second; GPIO2 LOW. Scope stimulus, not valid tag data. |
| `gpio 2 1 100` | Hold GPIO2 HIGH for 100 ms, then LOW; GPIO11 LOW. |
| `gpio 11 1 100` | Hold GPIO11 HIGH for 100 ms, then LOW; GPIO2 LOW. |
| `listen 1000` | Passive CL/DA measurement and DA capture; both outputs LOW. |
| `read 5000` | 125 kHz field and EM4100 RF/64 decoder, maximum 5 seconds. |
| `raw 1000` | 125 kHz field and DA capture; print first 256 intervals after OFF. |
| `emulate 5000 0` | Synthetic EM4100 RF/64 ID `0102030405`, with external reader field. |
| `emulate 5000 1` | Same waveform, inverted load polarity. |
| `off` or Ctrl+C | Cancel pending/active operation and return both outputs LOW. |
| `reboot` | OFF then normal reboot. |
| `romboot ARMED` | OFF then volatile ROM downloader request; no eFuse change. |

`field` frequency is in **kHz**; `pulse` frequency is in **Hz**; duration is
always **milliseconds**. Defaults: `field` = 125/50/5000; `pulse` = 1000/50/1000;
`read`/`emulate` = 5000 ms; `raw`/`listen` = 1000 ms. Portuguese aliases:
`campo`, `pulso`, `varrer`, `ler`, `parar`, `ajuda`.

GPIO15/CL and GPIO16/DA are **inputs only**. Their attached comparator outputs
must not be driven by a GPIO command. Arbitrary GPIO remapping is intentionally
not exposed. `pulse` can run without an external reader to inspect GPIO11/T2;
`emulate` requires measured CL between 110 and 140 kHz. Own-field commands first
listen passively for 50 ms and refuse a detected external carrier >=10 kHz.
This is an interlock, not a guarantee against a field applied after the check.

Carrier range: 20–200 kHz. Load pulse range: 1–10000 Hz. Duty: 10–90%.
Each active interval: 20–5000 ms; static HIGH: at most 1000 ms (100 ms default).
Sweep: ascending, integral steps, at most 100 steps and 100 seconds total active
time. Default sweep has 19 steps and 95 seconds active, plus OFF/precheck gaps.
Every step stops before the next; the console remains available during tests.
Invalid commands are rejected, and a second test cannot replace an active test.

The single event loop owns all output peripherals and checks deadlines between
UART characters and on a 5 ms receive timeout. This is software-bounded timing,
not a certified hard real-time cutoff: UART/reporting and scheduler latency add
to it. The task watchdog remains enabled. Closing/disconnecting the terminal
does not restart a test; the already running command reaches its deadline.
Check `off` / `status` before disconnecting. An OFF cleanup error blocks new tests.

## Measurements and limitations

GPIO2 drives the carrier; GPIO11 switches the load. CL frequency is measured by
an accumulating PCNT rising-edge counter using actual elapsed time. A second
PCNT counts both DA edges. `configured` is the LEDC setting, **not** a measured
coil voltage, resonance, field strength or tag energization result. Use the
scope at GPIO2, T4/T3/T5 and differentially across the coil. Ground clips belong
on board GND, not an arbitrary floating coil terminal.

DA pulse timing uses MCPWM hardware capture with a free-running 1 MHz timer,
not periodic GPIO sampling. The callback records the captured edge timestamp
and completed logic level into a bounded 1024-entry internal-RAM ring. The main
loop drains it into the decoder; software ring overflow invalidates the capture
and ends the test. The 256-entry raw sample prefix is diagnostic, not a complete
recording. Hardware interrupt overruns under pathological high-rate noise are
not independently certified by the ring overflow counter.

The new bench decoder accepts EM4100 Manchester **RF/64 only**. It tracks pulse
durations (short 156–356 us, long 412–612 us), both polarities and both half-bit
phases; it validates header, row/column parity and stop bit, and requires three
matching consecutive frames. `read` ignores the first 450 ms for decoding while
recording input activity. RF/16, RF/32, PSK, writing and other LF protocols are
not implemented by this bench application. None of these software checks proves
that a physical tag was read; confirm the exact ID on a known reference tag.

Emulation uses RMT on GPIO11 with GPIO2 LOW. It emits only fixed synthetic ID
`0102030405`. Half-bit duration is computed from the initial CL measurement, then
runs from the internal RMT clock; it is **not phase locked** to every external
carrier cycle. Waveform generation does not establish acceptance by Flipper or
another reader. No card contents or keys are written.

No PSRAM is initialized: the small ISR ring, raw prefix and RMT payload need
bounded internal memory and this target has no large UI/media buffers. P4 uses
the SDK 400 MHz setting, with no early clock override. No GPS, NFC, Sub-GHz,
LoRa, display, audio or UI workers are started by the P4 lab app.

## Build and installation

Use the pinned ESP-IDF v6.1 environment, then from the repository:

```sh
idf.py -C firmware_lab_rfid -B build build
python3 -m unittest tools/hardware_lab/test_rfid_uart.py tools/hardware_lab/test_rfid_uart_state.py -v
```

Output: `build/HighBoy_RFID_Lab.bin`. The target uses the exact existing
`firmware_p4/partitions.csv` (8 MiB, 3 MiB OTA slots). **Do not use generic
`idf.py flash`**: installation must use a reviewed app-only update with target
identity, security, installed partition map, active slot, installed image,
candidate hash, post-write hash and preserved-range checks. No bootloader,
partition migration or C5 update is part of this bench target. Restore the
normal application using the same guarded app-only procedure.

Host validation covers the actual command parser, waveform encoding, all
single-bit corruptions of sample EM4100 frames, polarity/phase/timing vectors,
resynchronization, and the actual event-loop logic with virtual peripherals:
OFF/deadlines, busy interlock, 19 sweep steps, external-field checks, partial
start failure and capture overflow. These are software tests, not electrical
measurements. Record the target serial, app hash and boot/console checks for
each physical installation; scope and reference-tag acceptance remain separate.
