# NFC and native LF RFID companion commands

Authenticated USB CDC and BLE sessions can identify NFC-A/B/F/V cards and read
EM4100/EM4102 LF tags through the P4's native drivers. These commands do not
modify cards, start emulation or synthesize results. Haptics remains deferred.

The integrated Muse RPC already occupies category `0x11`. NFC uses `0x12` and
LF RFID uses `0x13`; both are reserved in the identical P4/C5 SPI v8 headers.
The P4 intercepts them after authentication and session-generation checks. They
are never forwarded to the C5, including unknown operations in these categories.
The host envelope remains version 1. Payloads below exclude the usual RESP
status byte (`OK=0`, `BUSY=1`, `ERROR=2`, `UNSUPPORTED=3`, `INVALID_ARG=4`).

| Category | Operation | Request | Successful response |
| --- | --- | --- | --- |
| NFC `0x12` | READ `0x01` | `[technology u8]`: A=0, B=1, F=2, V=3 | `[found u8]`, followed by the NFC record below when found |
| RFID `0x13` | READ `0x01` | `[timeout_ms u16 LE]`, 40–500 inclusive | `[found u8]`, followed by the LF record below when found |
| Either | RECOVER `0x02` | empty | empty; retry cleanup retained by this companion operation |
| Either | STATUS `0x03` | empty | `[cleanup_pending u8][active_stage u8]`: idle=0, LF=1, NFC=2 |

Requests require the exact payload length; trailing or missing bytes are rejected
before driving hardware. Unknown operations return UNSUPPORTED. No discovery
result is represented by the single byte `00`, not a fabricated card. Successful
found records are fixed-size and zero-padded. Setup, driver and cleanup failures
return ERROR; unavailable ownership returns BUSY. Discovery timeout/no-card is
normal absence; a startup timeout remains an error.

## NFC found record (17 bytes total)

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 1 | `found=1` |
| 1 | 1 | Requested technology: A=0, B=1, F=2, V=3 |
| 2 | 1 | Native `highboy_nfc_protocol_t`; activation evidence, not a product guess |
| 3 | 1 | Identifier length, 1–10 |
| 4 | 10 | Identifier in native byte order, padded with zeroes |
| 14 | 2 | Type-A ATQA; zero for other technologies |
| 16 | 1 | Type-A SAK; zero for other technologies |

The identifier is the Type-A UID (normally 4/7/10 bytes), Type-B PUPI (4),
FeliCa IDm (8), or ISO15693 UID (8). Type-B PUPI can change between activations;
it is not a permanent card identifier. Protocol values are defined in
`Drivers/st25r3916/include/highboy_nfc_types.h` and keep the native distinction
between an ISO-DEP activation and a verified card product. The command performs
one native probe; repeat it or change technology to scan again.

## LF found record (9 bytes total)

| Offset | Bytes | Meaning |
| ---: | ---: | --- |
| 0 | 1 | `found=1` |
| 1 | 5 | Raw, parity-checked EM4100 identifier |
| 6 | 1 | Inverted signal flag, 0/1 |
| 7 | 2 | Samples used, unsigned little-endian |

The native 125 kHz field runs only for the bounded capture and cleanup. The
command has no write/clone operation.

## Antenna ownership and cleanup

The host-link worker reserves the shared RF frontend for its task before touching
hardware and holds a no-sleep reference while operating. This excludes a second
reader in the same mode as well as the other RF stage. LF public control methods,
NFC field lifecycle and NFC SPI transfers reject access from a different task
while reserved. The NFC transfer path rechecks permission after taking the
shared SPI lock. Existing native UI activity that already holds the field causes
BUSY; the companion does not forcibly stop that field.

Every accepted READ attempts field shutdown and reservation release before
responding. A failed NFC OFF keeps the antenna reservation and no-sleep lock;
results are withheld until cleanup succeeds. STATUS exposes retained cleanup and
RECOVER retries it. A retained no-sleep release failure is also recoverable.
RECOVER for the other stage returns BUSY and never turns off a UI-owned field.

Disconnect, reset and a new HELLO trigger another cleanup attempt through the
core worker. Reads execute synchronously on that worker: invalidation can happen
immediately, while hardware cleanup waits for the in-progress native call to
return. RFID's sampling window is at most 500 ms; NFC probes use native protocol
timeouts. There is no asynchronous result producer or new task. Existing
session-generation checks suppress a response to a replaced/disconnected session.

## CLI and validation

```sh
python3 tools/host_link/cli.py nfc read a
python3 tools/host_link/cli.py nfc read v
python3 tools/host_link/cli.py rfid read 500
python3 tools/host_link/cli.py nfc status
python3 tools/host_link/cli.py nfc recover
```

The reference CLI validates record sizes and rejects malformed responses.
`python3 tools/host_link/test_rf.py` exercises production handlers, RF arbitration
and authentication/generation routing with explicit hardware/crypto fakes.
`python3 tools/host_link/test_rf_cli.py` verifies the CLI without a serial port.
`python3 tools/highboy/test_rf_ownership.py` covers concurrent reservations and
retained failed-OFF behavior. Hardware card capture and live USB/BLE transport
remain physical acceptance work; these tests do not certify electrical shutdown.
