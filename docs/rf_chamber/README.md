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

# RF chamber service

A P4-local laboratory workflow for High Boy V2.03: CC1101 Sub-GHz first,
SX1262 LoRa second, with existing NFC/LF diagnostics used separately.
The console and on-device RF Lab menu share this service. Product capture and replay remain separate.

## Architecture and ownership

- `Drivers/cc1101/cc1101_bench.c`: checked configuration, explicit PE613050 shunt
  branch, nominal PA code, FIFO packet engine, state deadline and register restore.
- `Drivers/sx1262/sx1262_radio.c`: CW entry and physically checked standby.
- `Service/rf_chamber`: one persistent worker on the radio core, a no-sleep lease,
  synchronized results, finite run duration and stop request.
- `Applications/ui/screens/lora/lora_rnode_ui.c`: menu configuration and synchronized live results; legacy filename, no RNode UART.
- `Service/console/commands/cmd_rf_chamber.c`: argument parsing and machine-readable results.
- `tools/rf_chamber/chamber.py`: host recording, selected matrix execution and stop-on-error.

The worker owns the CC1101 task lease or SX1262 session reservation. Competing
radio applications are refused. Failed hardware/power cleanup retains ownership
(`phase=4`); `rfbench stop` retries on the same worker. An idle result is emitted
only after checked cleanup. The small persistent worker stack remains allocated
for subsequent jobs. No bulk DMA buffer is allocated for these tests.

Commands are asynchronous. USB disconnection does not cancel a job: its on-device
deadline still applies. Stop latency includes the current bounded SPI/packet
operation. This is not an independent hardware RF kill switch; a wedged chip may
require removal of all power. No NRESET connection exists for SX1262 on V2.03.

## On-device menu

Open **Sub-GHz > RF Lab**, or **LoRa > RF Lab** in the protocol picker.
UP/DOWN selects a row; LEFT/RIGHT changes its value. OK on Frequency cycles
315/433.92/868.1/915 MHz presets (LoRa: 868.1/915); the step row sets fine tuning.
Only OK on **Start test** starts a run. Entry always defaults to RX and low power.
OK on the live page requests stop, then returns to settings when idle; BACK stops
and exits after cleanup. UP/DOWN scrolls results. The same automatic deadlines
apply without USB. CW duration is clamped to the limits below when power changes.
The displayed power is the programmed request; RSSI is a receiver estimate.

An active mesh must be ended with **Stop mesh session** before a LoRa bench run.
This is asynchronous and runs on the radio core. The screen owns only its run ID:
a foreign console run is not stopped on navigation. The close hook requests stop
and deletes the refresh timer; hardware ownership remains until checked cleanup.
An OFF NOT CONFIRMED result keeps the stop action available for retry.

## Commands

```
rfbench info
rfbench status
rfbench stop
rfbench cc|sx cw|tx|rx HZ DBM PATH SF BW_KHZ CR MS INTERVAL_MS ARMED
```

`ARMED` makes each requested configuration explicit. CW/TX are for a controlled
laboratory setup with antenna/load and instrument protection arranged by the lab.
No command starts automatically at boot. There is no saved auto-repeat setting.

Examples (one at a time; wait for `phase=0` and `cleanup_error=0`):

```
rfbench cc rx 433920000 -30 1 7 125 1 10000 1000 ARMED
rfbench cc cw 433920000 -30 1 7 125 1 1000 1000 ARMED
rfbench cc tx 433920000 0 1 7 125 1 5000 500 ARMED
rfbench sx rx 868100000 -9 0 7 125 1 10000 10000 ARMED
rfbench sx tx 915000000 -9 0 7 125 1 5000 1000 ARMED
rfbench sx cw 915000000 -9 0 7 125 1 1000 10000 ARMED
```

| Field | CC1101 | SX1262 |
| --- | --- | --- |
| Frequency | 300-348, 387-464, 779-928 MHz | 863-928 MHz (board test range) |
| Nominal power | -30, -20, -10, 0, +5, +10 dBm | -9 to +22 dBm |
| PATH | 1, 2, 3, 4 | 0; DIO2 selects TX/RX |
| SF/BW/CR | Must be 7/125/1 placeholders; not the CC PHY | SF7-12; 125/250/500 kHz; CR1-4 means 4/5-4/8 |
| Duration | 100-120000 ms; CW max 10000 ms | Same; CW above +14 dBm max 1000 ms |
| Packet interval | 250-60000 ms | Same; normal airtime policy can defer submission |

Power values are register requests, not measured conducted power or EIRP.
The product application uses the qualified prototype branch map described in
[CC1101](../cc1101/README.md); this laboratory menu can explicitly compare all four branches. Laboratory branch
selection restores only the registers it changes, plus PATABLE and the saved
branch. It never writes back the unused FSTEST/PTEST/AGCTEST registers.
Each restored configuration byte is checked; a mismatch retains ownership.

### CC1101 packet and CW definition

The packet PHY is fixed **2-FSK**, approximately 9595.9 bit/s, 20.63 kHz deviation,
101.5625 kHz RX bandwidth, four-byte preamble, sync D391, fixed 32-byte payload,
hardware CRC, no whitening, no Manchester/FEC/address filter, channel zero.
MDMCFG4/3/2=`C8/83/02`; MDMCFG1=`20`; DEVIATN=`35`;
PKTCTRL0/1=`04/04`; MCSM1=`00` returns to idle after a packet.
These packet tests are not a replay of an existing remote protocol.

CW uses asynchronous OOK with GDO0 held high, PA[0]=0 and PA[1]=selected code,
FREND0=`11`, no software pulse generation. Configuration and PATABLE are read back.
CC FIFO TX counts completion only after observing TX then IDLE and empty TX FIFO.
RX rearms after each packet; its polling/turnaround creates a blind interval.
Use a sparse, known generator cadence (at least 250 ms) for PER trials.
The conservative board AGC (`C7/00/91`) is retained: sensitivity is of this profile,
not a claim of datasheet maximum sensitivity.

### SX1262 definition and limitations

32 MHz crystal, **LDO mode**, DIO2 automatic RF switch, PA parameters retained
from the board driver, 200 us ramp, preamble 12, explicit header, CRC on,
normal IQ and private sync word 0x1424. SF/BW/CR are explicit for both peers.
Only TxDone callback increments `done`; standby alone is not a transmitted packet.
A packet still pending at the session deadline is stopped and remains submitted,
not done. Normal LoRa airtime accounting remains enabled and is reported as
`deferred`. The CW command is a separate bounded laboratory mode.

The current board uses POR calibration. Explicit full/image calibration is still
deferred because previous prototypes stuck BUSY and have no P4-controlled reset.
This is an **open receiver qualification issue**, especially when switching
between 868 and 915 MHz. Capture it in the laboratory log; do not describe this
build as fully calibrated. A separate calibrated SX1262 peer or vector generator
is needed for LoRa packet/sensitivity measurements. CC1101/Flipper cannot decode LoRa.

### Results

`rfbench info` identifies the ELF SHA-256 and IDF version. `STATUS` is one JSON
object; phases: 0 idle, 1 starting, 2 running, 3 stopping, 4 retained cleanup.
Each accepted request increments `id`. `error` and `cleanup_error` are separate.

RSSI min/max/sum are in **half-dBm units**, valid only when `samples>0` in RX.
These are uncalibrated chip indications. TX never invents an RSSI or delivered
power reading. `pa` is the CC byte; `radio_status` is MARCSTATE for CC and the
raw GetStatus byte for SX, both from the last active sample before cleanup.
They are not a post-stop chip-state reading. `freqerr_raw` is signed CC FREQEST (26 MHz/2^14 Hz per
count), not a substitute for a frequency counter. SX packet RSSI/SNR is also
printed per packet. CRC/header errors and foreign payloads are separate.

Payload: bytes 0-3 ASCII `HBRF`; bytes 4-7 unsigned big-endian sequence, starting
at zero for each run; bytes 8-31 equal `0xA5 XOR byte_index`.
`rx_test` counts matching CRC-valid frames, including repetitions. `seq_gaps`
counts forward jumps and `seq_repeats` immediate repeats. They are **not a PER
estimate**: use the external generator's sent count, matching sequence IDs and
acquisition window, and account for duplicates/reordering and RX dead time.

## Host runner

Python 3 with `pyserial`. Select the P4 CP2105 port ending in 0; no automatic
reset, flashing or port guessing. All invocations below are from the repo root.

```
python3 tools/rf_chamber/chamber.py --output test_matrix.json matrix
python3 tools/rf_chamber/chamber.py --port /dev/cu.usbserial-0201891B0 status
python3 tools/rf_chamber/chamber.py --port /dev/cu.usbserial-0201891B0 --output rx.json run --radio cc --mode rx --hz 433920000 --dbm -30 --path 1 --duration-ms 10000
python3 tools/rf_chamber/chamber.py --port /dev/cu.usbserial-0201891B0 --output cw.json run --radio sx --mode cw --hz 915000000 --dbm -9 --armed-lab
python3 tools/rf_chamber/chamber.py --port /dev/cu.usbserial-0201891B0 --output selected.json execute test_matrix.json --first 0 --count 1 --armed-lab
python3 tools/rf_chamber/chamber.py --port /dev/cu.usbserial-0201891B0 stop
```

The generated matrix has 206 **planned**, not measured, cases. Execute selected
blocks after the lab has set up its instruments. It is not a full qualification
plan by itself. The runner records raw console, request, firmware identity,
USB identity, timestamps and final counters; stops on error and attempts checked
shutdown. A serial failure is retained as an error, never an assumed RF-off result.

## Validation

Host UBSan fixtures compile production code: config limits and 10000 payload
vectors; all four CC bands/branches; PA code mapping; packet timeout; failed
restore retained and retried; SX chip-mode versus software-state distinction;
checked CW stop; CC/SX cleanup ownership. Existing airtime vectors/concurrency
checks also pass. These are mocked hardware tests, not conducted RF measurements.

On unit 0201891B, 16 November 2026, the qualified candidate passed 58 low-power
control cases (36 CC, 22 SX), 8 invalid-command rejections, early stop and busy
rejection on both radios, and the on-device RX deadline while the host serial
connection was closed (USB power remained attached). Three Type 2 emulation
start/stop cycles also passed afterwards. No independent RF peer or calibrated
instrument was used in these control cases; they do not establish delivery,
sensitivity, antenna matching or radiated power. The preceding candidate was
rejected on CC restore; the backend now restores only changed registers and
avoids unused test-register writes. Exact binary hashes and raw logs are in
`outputs/RF_Chamber_20261116` in the local task workspace.

The subsequent app-only menu candidate (`58A_app_update.json`) was verified on
the same unit. Physical menu actions passed RX, test-packet TX and bounded CW
for both radios at low power, with checked cleanup; BACK stopped a running CC
receive test. Radio Config selections reached native Read at 433.92, 868.35 and
915 MHz. Portrait and landscape screenshots were inspected, followed by five
landscape entry/exit cycles, a successful C5 ping and final radio idle status.
The accepted combined record is `62_menu_acceptance.json`; record 61 was rejected
because its harness mistook portrait rotation value 1 for landscape value 2.
These checks validate menu routing, register selection and lifecycle, not RF
delivery or range. The final device was left in the RF Lab menu with RF stopped.

## Primary references

- [TI CC1101 datasheet](https://www.ti.com/lit/ds/symlink/cc1101.pdf): supported bands, packet engine, register meanings, nominal PA tables.
- [Semtech SX126x driver](https://github.com/Lora-net/sx126x_driver): CW opcode and driver reference.
- [Semtech SX1262 documents](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262): datasheet and application notes.
- [pSemi PE613050](https://www.psemi.com/pdf/datasheets/pe613050ds.pdf): tuning switch truth table and electrical limits.
- [pSemi PE4259](https://www.psemi.com/pdf/datasheets/pe4259ds.pdf): TX/RX switch truth table.
- [ST AN5276](https://www.st.com/resource/en/application_note/dm00591454-antenna-design-for-st25r3916-st25r3917-and-st25r3920-devices-stmicroelectronics.pdf): NFC matching design.
- [ST AN5322](https://www.st.com/resource/en/application_note/an5322-automatic-antenna-tuning-aat-for-st25r391616b-and-st25r392020b-devices-stmicroelectronics.pdf): AAT requires the corresponding tuning network.

## Passive switch and quick presets

See [passive RF switch](passive.md) for the Sakari bench profile, fixed PCB map, automatic boot and active presets.
