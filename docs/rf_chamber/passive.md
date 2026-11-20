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

# Passive RF switch and quick radio presets

## Device controls

The full laboratory firmware starts this profile from **Sub-GHz > RF Lab >
Switch + tags / restart > OK**. It remembers the profile across reboot.
The screen offers:

- **Manual RF switch**: LEFT/RIGHT select RF1–RF4. The display shows the selected
  branch, actual GPIO readback and save errors. BACK returns to the lab menu.
- **Read NFC identity**: OK actively polls NFC-A/B/F/V for up to 30 seconds and
  displays the UID/PUPI/IDm and technology. This reads identity, not card memory.
- **Read RFID 125 kHz**: OK drives GPIO2 at 125 kHz/50%, keeps GPIO11 low and
  searches for EM4100 RF/64. It shows measured CL, captured DA edges, invalid
  pulses, lost edges and attempts. A valid result requires three equal,
  parity-valid frames. An idle DA is reported without inventing a UID.
- **Exit lab / restart**: OK clears the profile and restarts normal firmware.

Opening a read page does not start a field. OK stops an active search; BACK
requests cancellation and waits for confirmed OFF before returning. UID results
are green only after valid decoding and successful cleanup. A timeout is not a
pass. UP/DOWN scroll long details in either display orientation.

NFC and LF share exclusive antenna reservation. The test worker owns it across
initialization, acquisition and shutdown. Failed OFF retains the worker and
reservation, displays **OFF NOT CONFIRMED** and retries cleanup; it never starts
the other technology. Switch selection and normal-mode exit are blocked during
an active tag test. C5 stays held, CC1101 and SX1262 stay idle throughout. This
profile is passive except during an explicitly started NFC/LF search.

Normal RF Lab retains **Quick setup** and explicit bounded **Start test**. Its
Sub-GHz/LoRa transmissions remain disabled in this boot profile.

## Fixed switch map

| State | GPIO17 / V1 | GPIO18 / V2 | Branch |
|---|---:|---:|---|
| RF1 | 0 | 0 | L16, 51 nH |
| RF2 | 0 | 1 | L17, 16 nH |
| RF3 | 1 | 0 | L18, 1.8 nH |
| RF4 | 1 | 1 | L19, 1.5 nH |

Sources: V2.03 schematic sheet 3; top assembly sheet 12, 19-Oct-2026; PE613050 DOC-32614-9.01 table 4 (column order V2,V1). The top assembly drawing shows L18, L19, L17, L16 from left to right. Match the PCB revision and fitted values before measurements. Passive selection does not remap branches by frequency.

## Boot and hardware

- `Drivers/rf_lab`: C5 reset, LF outputs low, NFC OP_CTRL=0, CC SIDLE/SPWD, SX standby/sleep; control GPIO readback.
- `Service/rf_chamber/rf_passive`: NVS profile, selected state, status and no-sleep lock.
- `Core/kernel`: load profile before starting radio services; normal boot releases C5. Passive boot skips bridge, Wi-Fi, BLE, host-link and audio startup.
- `Applications/ui/screens/lora/lora_rnode_ui`: controls and feedback, no hardware calls.
- Console in passive profile exposes screen controls and RF bench only. `rf_chamber_start()` rejects every active session. UI navigation is restricted to the integrated switch/tag screen.

NVS namespace `rf_lab`, key `passive`: zero = normal; 1..4 = passive branch. Writes commit before reboot. A corrupt value or NVS read failure keeps C5 held and reports an error. A failed branch/save operation is reported; no transmission fallback exists.

Sleep acknowledgements and control readback are not RF spectrum measurements. P4 application code holds C5; RF behavior during ROM/bootloader startup still needs a spectrum check. Battery-only operation and complete battery removal/reinsertion require a physical acceptance test. Saved selection uses internal NVS, not microSD.

## Active presets

| Radio | Frequencies (MHz) | Signal | Initial setting |
|---|---|---|---|
| CC1101 | 315, 433.92, 868.35, 915 | CW | -30 dBm, 1 s |
| CC1101 | 433.92, 868.35, 915 | synthetic 2-FSK packets | -30 dBm, 5 s |
| SX1262 | 868.1, 915 | CW | -9 dBm, 1 s |
| SX1262 | 868.1, 915 | synthetic LoRa packets | -9 dBm, 5 s; SF7/BW125/CR4-5 |

CC defaults RF1 for 315/433 and RF2 for 868/915 from unit 0201891B comparisons; these are not universal antenna optima. All four branches remain selectable. Existing receive, SF/BW/CR, duration, interval and power controls remain available. Preset selection never starts TX.

## Verification

Run `python3 tools/rf_chamber/test_passive.py`, `test_menu.py`, `test_bench.py` and `test_lifecycle.py`. These compile production logic with fault injection and test validation, preset configuration, persistence, readback mismatch, errors and radio cleanup. Hardware acceptance must additionally check the four states, persistence after reboot, prohibited Sub-GHz/LoRa TX, explicit NFC/LF polling, normal-mode recovery, quick presets, battery power-cycle and RF noise floor.

Console: `rfpassive status`; `rfpassive path 1..4`; `rfpassive on RESTART`; `rfpassive off RESTART`. `rfbench` retains its existing bounded command syntax, but cannot start in passive mode.

## Packet completion under scheduler load

CC packet completion requires a loaded 32-byte FIFO before STX, then a drained FIFO and confirmed IDLE. It does not require polling to observe the brief TX state. Repeated status reads reject inconsistent values; FIFO underflow and an unaccepted STX remain errors. See [TI CC1101 SWRS061I](https://www.ti.com/lit/ds/symlink/cc1101.pdf), sections 10.3 and 26. Host tests include completion before the first poll, rejected STX and FIFO underflow. This changes completion accounting; it does not increase RF output power.

`python3 tools/rf_chamber/test_tag_scan.py` checks identity extraction, one active
reader, timeouts, cancellation before/during reads, initialization and read
failures, retained OFF and recovery. `test_bench_ui.py HOST_UI_BUILD` runs the
actual screen with native LVGL in 240x320/320x240 and light/dark colors; hardware
services are faked. On-board tag acceptance must be recorded separately.
