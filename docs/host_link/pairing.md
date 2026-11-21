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

# Companion PSK authentication (handshake v2)

The device PSK is the only user credential required for a USB or BLE Host Link
connection. There is no confirmation PIN, code comparison, device OK press or
screen-controlled pairing window. Obtain the existing PSK from **Bluetooth >
Companion** or `hostlink psk`, then configure it in the client. The selected
transport must still be enabled and available; BLE requires its companion relay
running. Authentication is independent of the visible screen.

Companion and the PC Monitor waiting screen show the provisioning PSK as a QR.
The payload is exactly the 64 hexadecimal PSK characters, without a URI or prefix,
so a phone scanner can copy it directly into the client's PSK field. OK toggles
to the same key in text. PC Monitor switches to metrics after authentication;
when BLE is unavailable, OK retries the service. QR scanning does not alter the
handshake or require confirmation. See [UI layout](../ui/README.md#companion-connection-screens).
Leaving either screen releases its status timer and an unlinked BLE relay it
started. It does not invalidate an unrelated USB handshake. An authenticated
session can remain active after navigation. After USB authentication, Companion
requests BLE relay shutdown; a BLE-owned session keeps its relay running.

The persisted PSK remains the out-of-band identity. A fresh X25519 secret and
nonce are generated for every accepted HELLO; ephemeral secrets are never stored
in NVS. Session MAC keys combine ECDH, PSK and the complete transcript.
Private/shared scalar material is cleared before HELLO handling returns. Reset,
rekey and handshake expiry invalidate provisional keys. Low-order/zero shared
secrets are rejected by libsodium. The implementation uses X25519 as specified in
[RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) and follows the
[libsodium public-key binding guidance](https://doc.libsodium.org/advanced/scalar_multiplication).
This application handshake is not a claim of TLS/Noise compliance.

## Wire contract

The existing `HB` envelope retains **version 1** and little-endian counters.
HELLO requires **handshake version 2**. Removing device approval does not change
packet sizes, proof labels or directional keys, and does not restore the legacy
17-byte HELLO. Both initial messages have flags=0 and category/op=0.

- HELLO type `0x10`: `[2][client_nonce:16][client_X25519_public:32][client_proof:16]`
  (65 bytes). Define `C` as its first 49 bytes.
- HELLO_ACK type `0x11`: `[2][server_nonce:16][server_X25519_public:32][device_id:6][server_proof:16]`
  (71 bytes). Define `S` as its first 55 bytes and transcript `T=C||S` (104 bytes).
- `client_proof = HMAC-SHA256(PSK, "tos-host-v2-client" || C)[:16]`.
- `server_proof = HMAC-SHA256(PSK, "tos-host-v2-server" || T)[:16]`.

The client proof is checked before X25519. Given shared secret `Q`, derive each
32-byte direction key with HKDF-SHA256, `salt=PSK`, `IKM=Q`, and `info=label||T`.
Labels are `tos-host-v2-a2d` and `tos-host-v2-d2a`, without trailing NUL. Both
public keys, both nonces, handshake version and device identity are bound by
the proofs and derivation. The former `tos-host-v2-code` value is no longer
computed or displayed; it was never part of a wire payload.

HELLO_ACK establishes provisional keys only. Commands and application/log output
remain unauthorized until key confirmation. The client immediately sends
**HELLO_FINISH `0x12`**, flags=AUTH, category/op=0, empty payload and counter=1,
with the truncated envelope MAC under `K_a2d`. The device requires a valid MAC
and an exchange younger than 30 seconds, then activates authentication and sends
**HELLO_READY `0x13`**, flags=AUTH, category/op=0 and empty payload under `K_d2a`.
This is automatic cryptographic confirmation, with no user interaction.

READY is serialized before other authenticated output. Its first counter is
normally 1 (HELLO_ACK consumed outbound counter=0). Subsequent client commands
start at counter=2; directional counters increase strictly. A correctly MACed
FINISH repeated at counter=1 is idempotent: the device can resend READY with a
fresh outbound counter without resetting the command replay baseline. FINISH
never dispatches a command. Other repeated inbound counters are rejected.

An expired exchange cannot finish. A fresh HELLO can start immediately without
reopening a screen. A replacement HELLO uses fresh device key material, so old
FINISH messages cannot complete the replacement exchange. Disconnect, transport
ownership changes and PSK regeneration retain the existing session invalidation.
An established session does not expire with the 30-second handshake deadline.

Frames retain the HMAC integrity envelope and are visible on the transport;
payload confidentiality is not provided by this envelope. The PSK authorizes the
same companion command set. BLE bonding and the separate Muse protocol are
unchanged.

## Reference clients and dependencies

`tools/host_link/cli.py` and `hb_push.py` share `hb_pairing.py`; PC Monitor also
uses that handshake over USB and BLE:

```sh
python3 -m pip install -r tools/host_link/requirements.txt
python3 tools/host_link/cli.py system ping
```

Clients use `cryptography` X25519 and stdlib HMAC/HKDF. They validate the device
PSK proof and authenticated replies, including MAC, bounds and monotonic counter,
before returning them to command handlers. They send FINISH immediately after
ACK and wait for verified READY before commands. No comparison code is printed.
Timeout clocks are monotonic. Python does not guarantee explicit erasure of all
immutable private-key copies.

## Validation

`python3 tools/host_link/test_pairing.py` compiles production security with
ASan/UBSan, real libsodium X25519 and real OpenSSL HMAC/HKDF adapters. NVS, RTOS,
hardware RNG/MAC identity and serial transport are fixtures. It checks RFC 7748
vectors, PSK/transcript proofs, low-order rejection, automatic authentication
without a screen/window, provisional command/output rejection, rekey, expiry,
reset, reflection/replay rejection and PSK persistence/rotation.

A shared-library test interoperates with Python cryptography. Both actual USB
clients complete the C handshake without simulated button approval, recover a
lost READY and reject forged/replayed responses. The production core HELLO
handler is tested with valid v2, wrong PSK and malformed/legacy payloads. The
real Companion input/stop handlers are exercised with UI/transport fixtures for
BACK cleanup, idempotent stop and preserving authenticated sessions.

Host dependencies are a C compiler, OpenSSL/libsodium development libraries,
`pkg-config` and the Python requirements above. Optional `HOST_LINK_TEST_CFLAGS`
and `HOST_LINK_TEST_LDFLAGS` select a host crypto build. Firmware dependencies
remain managed libsodium and ESP-IDF crypto.

These host tests do not prove physical USB/BLE interoperability or RNG entropy
quality. Device acceptance must cover both transports, reconnect, wrong PSK,
and navigation during connection. Only P4 owns this authentication policy; no
C5 protocol or firmware change is required for removing the PIN.
