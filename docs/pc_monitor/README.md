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

# PC Monitor

The Highboy displays CPU utilization, RAM usage, filesystem usage, local IP and
public IP from a computer over **native P4 USB CDC** or **BLE on the C5**, relayed to the P4 over SPI. It does not sample
the Highboy itself. The Python CLI supports macOS, Linux and Windows via psutil;
both ESP-IDF v6.1 firmware builds and the complete Clang host regression suite
passed on macOS. Hardware acceptance is pending.

## Start with USB

1. Install Python 3.10+ and create a virtual environment on the computer:

   ```sh
   python3 -m venv .venv-pc-monitor
   . .venv-pc-monitor/bin/activate
   pip install -r tools/host_link/pc_monitor_requirements.txt
   ```

   On Windows, activate with `.venv-pc-monitor\Scripts\Activate.ps1` in PowerShell.
2. Connect a data cable to the P4 native USB connector (not a debug UART or C5).
   Close other programs using that CDC port. Open **Developer > PC Monitor**.
3. Run:

   ```sh
   python tools/host_link/pc_monitor.py --port /dev/cu.usbmodemXXXX
   ```

   Use `/dev/ttyACM0` on Linux or `COM5` on Windows as appropriate. Omitting
   `--port` selects the device only when exactly one Highboy VID/PID matches.
4. The CLI uses the local `.highboy-pc-monitor.psk` file automatically. Set `HB_PSK`
   to override it for another device. Compare the six-digit
   code printed by the CLI with the Highboy display; press **OK** to approve.
   PC Monitor keeps pairing available while this screen remains open.
5. Ctrl-C stops the CLI. BACK leaves the screen; an authenticated companion
   connection remains usable, following the existing Host Link lifecycle.

## Start with BLE (no USB data connection required)

Install the same requirements file; it includes [Bleak](https://bleak.readthedocs.io/en/latest/api/client.html).
Enable Bluetooth on the computer and grant the terminal/Python Bluetooth access
if the OS requests it. On Linux, BlueZ must be running.

1. The CLI reads the default device PSK from `.highboy-pc-monitor.psk` at the repository root.
   Save its 64 hex digits there once; this personal file is ignored by Git.
   For another device, set `HB_PSK`. To obtain its key, open **Bluetooth > Companion** on Highboy;
   its pairing screen displays the existing key. Copy it, then leave that screen.
   No USB console is needed to obtain the key.
2. Open **Developer > PC Monitor**. The screen requests the existing companion
   BLE service, advertised as `Tentacle-XXXX`. If BLE cannot start, it shows an
   error and OK retries. NimBLE is single-owner; this reserves the radio for the
   companion and cannot run alongside BLE spam, MeshCore or Meshtastic.
3. Run:

   ```sh
   python tools/host_link/pc_monitor.py --ble
   ```

   The CLI lists nearby Highboys with a number, advertised name, address/OS UUID
   and signal strength. Enter the number to select one, **R** to scan again or
   **Q** to cancel. The configured PSK is used automatically after selection.
   Only Highboys advertising the companion service appear; keep PC Monitor open.

   To skip interactive selection (including scripts), specify its exact advertised name:

   ```sh
   python tools/host_link/pc_monitor.py --ble --device Tentacle-1234
   ```

   `--device` also accepts the BLE address on Linux/Windows or the OS UUID on
   macOS. Only advertisements containing the Host Link service UUID are eligible.
4. Keep **PC Monitor** open; the CLI uses its default PSK or `HB_PSK` override;
   no navigation or extra Enter prompt is required. The screen refreshes its
   waiting window while visible, so scanning and typing the key do not exhaust
   it. The six-digit code appears automatically after the device accepts HELLO;
   compare it with the CLI and press OK on Highboy.

The screen refresh does not reset an active handshake or authenticated session.
Pending codes still expire after 30 seconds and still require physical approval.
A new code scrolls the screen to the top so it cannot be hidden below metrics.
Leaving the screen closes the window and cancels pending approval. This behavior
requires the updated P4 firmware; the C5 BLE implementation is unchanged.

The telemetry contract and physical pairing are identical across transports.
Each scan lasts ten seconds. After selection and PSK validation, the CLI subscribes
to TX notifications, reassembles the
ordered byte stream, and uses acknowledged GATT writes split to the negotiated
MTU (including the minimum MTU of 23). Notifications have a bounded receive
buffer; overflow or disconnect terminates the session instead of dropping bytes
silently. The existing MAC/counter checks are retained. No reconnect reuses keys.

BACK cancels pending pairing. If this screen started BLE and no authenticated
session exists, it also stops advertising. An authenticated connection survives
leaving the screen, following Companion behavior. Ctrl-C closes the PC BLE
connection. Restart and pair again after disconnecting.

The screen changes require the updated P4 application to be installed; the BLE
GATT service already exists in the C5 firmware. Physical BLE acceptance is still
pending, including the C5-to-P4 relay under actual radio/OS conditions.

Optional arguments:

- `--disk /Volumes/Data` or `--disk D:\` selects a filesystem by an existing path.
  The default is the OS root volume. This is **space usage**, not I/O throughput
  or the sum of every physical disk. The full path is used for sampling; only
  its display label is shortened to 31 ASCII characters.
- `--interface en0` chooses the interface for the local IP. By default the CLI
  selects the outgoing IPv4 route, which may be a VPN. A selected interface
  supports IPv4 or IPv6. A missing address appears as unavailable.
- `--interval 1` sets sampling between 0.5 and 2 seconds.
- `--no-public-ip` disables the external request. Otherwise the CLI queries
  [ipify](https://www.ipify.org/) over HTTPS at startup and every five minutes,
  with a three-second timeout, in a separate thread. Failures clear the address.
  The displayed public IPv4 is the Internet egress address, possibly a VPN/NAT.

CPU is overall utilization since the previous sample; the first meaningless
psutil reading is discarded. RAM usage is total minus available. Disk percentage
is psutil's filesystem usage (reserved blocks can affect the percentage).
See [psutil API](https://psutil.readthedocs.io/en/latest/).

The firmware clears displayed metrics after five seconds without a valid sample,
or when the authenticated session changes. Disconnected values are never shown
as live. IP lookup does not block sampling. The CLI exits on transport errors;
restart it and pair again after reconnecting. UP/DOWN scroll long IP/path labels.

## Wire contract

Authenticated Host Link SYSTEM `0x4E` (`SPI_ID_SYSTEM_PC_MONITOR`), P4-local.
Both SPI headers reserve the identifier; no SPI transport version change or C5
forwarding is involved. Response is status only. The normal Host Link physical
confirmation, MAC and replay protection apply; no unauthenticated telemetry is accepted.

Payload is exactly **180 bytes**, little-endian, Python format
`<BBHHHIIII32s32s46s46s`:

| Offset | Field |
| --- | --- |
| 0 | version, u8 = 1 |
| 1 | reserved, u8 = 0 |
| 2, 4, 6 | CPU, RAM, disk percentages x100, u16 (0..10000) |
| 8, 12 | RAM used/total, u32 MiB |
| 16, 20 | filesystem used/total, u32 MiB |
| 24, 56 | hostname and volume label, each 32 bytes |
| 88, 134 | local/public IP, each 46 bytes |

Strings are NUL-terminated printable ASCII, zero-padded by the CLI. Empty IP
means unavailable; nonempty IPs must parse as IPv4/IPv6. Hostname and volume must
be nonempty. Totals must be nonzero and used cannot exceed total. Invalid payloads
return INVALID_ARG without replacing the last valid snapshot. Receiver ownership
is bounded, static, and protected by a short critical section; it never calls LVGL.

## Validation

`python3 tools/host_link/test_pc_monitor.py` tests the production receiver under
ASan/UBSan, malformed packets, sample freshness and session changes, and Python
serialization, response matching, network failure and port selection. It is
included in `tools/highboy/test.command` without requiring psutil, pyserial or
Bleak. BLE tests use fake GATT clients to cover MTU fragmentation, notification
reassembly, wrong/ambiguous device selection, interactive selection/retry/cancel, noninteractive
usage, buffer overflow, disconnection,
write failures and context cleanup.

Hardware checklist: pair over USB and BLE, compare values with the PC system monitor,
select another disk/interface, stop/restart CLI, unplug/reconnect USB, disable/re-enable Bluetooth, disconnect
Internet and verify unavailable IP, reject pairing, and repeatedly enter/leave
the screen while updates arrive. No hardware flashing is performed by the CLI.

## No HELLO_ACK over BLE

Finding an advertisement and accepting GATT writes do not prove that the P4
accepted the handshake. An expired physical pairing window, wrong device PSK,
occupied Host Link session, or BLE/SPI forwarding failure can all prevent ACK.
The CLI reports accepted GATT write bytes and received notification bytes on
this timeout; these counts contain no PSK or payload contents. Zero received
bytes does not identify which part of the forwarding/security path failed.

If no six-digit code appears on Highboy, keep PC Monitor open, verify the
selected device PSK and capture the diagnostic counters before retrying. If a code appears on Highboy but not in the CLI,
the return path or ACK validation needs investigation. Preserve the CLI error
and, when accessible, P4 `HOST_LINK`/`HOST_BLE` and C5 `HOST_TRANSPORT`/GATT logs.
Do not disable physical confirmation or MAC checks to bypass a timeout.

## Telemetry acknowledgement timeouts

After pairing there is no fixed session duration. The CLI waits up to three
seconds for a telemetry response. One missing response keeps the connection
open and prints a warning; the next iteration samples current metrics and sends
a new authenticated command with a new counter. It never replays the old frame.
A successful telemetry response resets the failure count. Three consecutive
missing responses close the session with an explicit error. BLE write failures,
disconnection, invalid MAC/replay and device error responses still fail immediately.

Telemetry responses do not carry a sample identifier, so a delayed response can
indicate that communication resumed without proving delivery of the latest sample.
This tolerance is specific to replacing PC Monitor snapshots, not a retry policy
for arbitrary Host Link commands. It does not fix underlying SPI packet loss.
The UI still marks data stale after five seconds without a fresh sample.
