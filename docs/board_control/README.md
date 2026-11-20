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

# V2.03 board controls

The P4 `Service/board_service` worker coordinates the C5-owned board controls.
Only High Boy V2.03 is supported. SPI command protocol v2 and transport envelope
revision 1 remain unchanged; the board extension negotiates its own revision 2,
model `0x0203`, capabilities and session epoch before allowing control requests.
Transport availability alone does not imply compatible board controls.

## Controls and safe state

| Control | C5 GPIO | Logical true | Safe value |
| --- | --- | --- | --- |
| Amplifier | 0 | Enable amplifier | false |
| Haptic reset | 5 | Release active-low reset | false |
| USB output enable | 4 | Enable switch, `/OE` low | true |
| Haptic interrupt input | 1 | Raw high level | input only |

Boot configures output latches before output mode, and GPIO1 as an input with an internal pull-up and falling-edge interrupt. The ISR
only latches an atomic event flag. The P4 retains USB selection on GPIO19, low at boot
(CP2105). Haptic status here is the raw `INTN` level, not decoded AW8623 status.
There is no arbitrary GPIO command and no legacy pin fallback.

## Wire contract

Both `spi_protocol.h` copies must remain byte-identical. All multi-byte fields
are little-endian and structures are packed. Reserved fields must be zero.

| ID | Request fields, in order | Bytes | Successful reply |
| --- | --- | --- | --- |
| `0x000E` negotiate | protocol u16, revision u16, model u16, reserved u16, client u64 | 16 | capabilities, 24 bytes |
| `0x000F` state | epoch u64 | 8 | state, 12 bytes |
| `0x0010` set | epoch u64, revision u16, control u8, value u8 | 12 | state, 12 bytes |

Capabilities: protocol/revision/model/reserved (four u16), capabilities u32,
lease duration in milliseconds u32, epoch u64. Bits 0/1/2 indicate amp/haptic/USB;
bit 3 indicates haptic input. The lease duration is 5000 ms.

State: epoch u64, logical output values u8, known output mask u8, inputs u8,
reserved u8. Output bits 0/1/2 match capabilities; input bit 0 is raw INTN high and bit 1 is a pending falling edge since
the last successful reset assertion. STATE reads do not consume the flag.
A known output means its GPIO assignment succeeded; it is **not** an electrical
measurement. Unknown outputs must not be displayed as confirmed.

SET accepts control 0/1/2 and value 0/1 only. Assignment is idempotent and an
already confirmed level produces no further GPIO write or reset pulse. Exact
request/reply sizes are checked. Invalid size/value/reserved fields return
INVALID_ARG; unsupported revisions/models/controls return UNSUPPORTED;
a stale epoch or unavailable mutex returns BUSY; GPIO failures return ERROR.
The P4 maps these to explicit ESP errors and rejects malformed replies.

## Sessions, timeouts and recovery

The P4 generates a nonzero random client identity at worker initialization. A
new client makes the C5 request safe outputs and issue a fresh random epoch.
Renegotiating the same client during its live lease preserves outputs so that a
lost acknowledgement can be reconciled through STATE. A C5 restart or expired
lease invalidates old epochs. Commands from the preceding session cannot enable
outputs in the new session.

The P4 polls every 500 ms with a 250 ms RPC budget. Snapshots expire after 1500 ms.
The C5 renews its lease only after a successful board command. A 100 ms software
timer checks expiry; it requests safe outputs, retires the client and changes the
epoch after 5000 ms without renewal. This depends on scheduler execution and is
not a hardware watchdog or a hard real-time guarantee. GPIO failure during SET
also retires the session and requests safe outputs; unsuccessful safe writes
remain unknown and are retried by the timer while no lease exists.

On transport/protocol error, the P4 cancels pending intents with an error and
invalidates confirmed snapshots. It does not retry SET automatically. The next
successful negotiation/read discovers the actual applied levels. Neither a
reconnect nor a reboot restores previously requested amp/haptic enables. A caller
must explicitly issue a new request when appropriate.

## Driver interface and concurrency

`Drivers/pins/include/board_control.h` defines typed callbacks for amp, haptic
reset and USB enable plus a status snapshot. Obtain the implementation with
`board_service_controls()` and inject it into consumers; drivers need no SPI or
Service dependency. Audio/USB consumers now use this interface. `board_control_apply()` is a
worker-only confirmation helper with a 1200 ms budget and epoch checks; it
never calls SPI directly. AW8623 playback, stop and diagnostic behavior is documented in [AW8623](../aw8623/README.md).

Request callbacks enqueue work and return immediately: ESP_OK means accepted,
not applied. Each output records `has_request`, requested value, pending status,
confirmed value, known status and completion result. There is one outstanding
request per control; a second returns INVALID_STATE. Different controls can be
pending independently, but a link failure cancels all pending requests. A missing
capability returns NOT_SUPPORTED. Before negotiation or after stale state,
requests fail closed. These APIs are task-only, not ISR-safe.

Only the P4 worker performs SPI, pinned using `sys_prio.h`. Snapshot spinlocks
are never held across SPI calls or task notifications. The C5 dispatcher and
lease timer serialize GPIO/state access with a mutex; the timer skips a busy
mutex and checks again on its next tick. No ISR performs transport or GPIO
coordination. The C5 board service initializes before the SPI slave dispatcher.

## Validation

`python3 tools/board_control/test_board_control.py` compiles both production
services into one host executable with ASan/UBSan and simulated transport/time.
It covers malformed frames, absent capabilities, unsupported revisions, pending
request isolation, idempotence, lost ACK, delivery failure, reboot between read
and write, lease expiry, a new P4 client, stale snapshots and GPIO failure.
`python3 tools/highboy/test_board_pins.py` checks production GPIO boot ordering,
error propagation and the three control polarities. Both run in the offline suite.
Hardware validation on V2.03 remains required, including levels during startup,
reset, disconnected SPI and control transitions.

### Physical recovery reconciliation

`board_service_reconcile()` is called by the exclusive bridge recovery owner.
It serializes against the normal board worker, cancels pending output intents,
negotiates capabilities and reads the new epoch/state without restoring old
outputs. Failure keeps the link unavailable; see [the supervisor](../bridge_manager/README.md).
