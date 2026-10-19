# Companion ephemeral pairing (handshake v2)

Open **Companion** on the device, obtain its existing PSK and start the client.
Both sides show a six-digit code. Compare them and press the device OK button to
allow the connection; BACK rejects it. Every new USB or BLE connection requires
this device-side approval. The pairing window lasts 60 seconds and each pending
exchange expires within 30 seconds, bounded by that window. Reopen Companion to
start a new window. Closing an unlinked pairing screen also invalidates the host
session generation, including a concurrent FINISH; a completed linked session
can remain active after leaving the screen. After confirmed USB pairing, the
UI requests BLE relay shutdown, leaving the C5 available for central GATT and
other Bluetooth commands; a BLE-owned session keeps its relay running.

The persisted device PSK remains the out-of-band identity. A fresh X25519 secret
and nonce are generated for every accepted HELLO; ephemeral secrets are never
stored in NVS. Session MAC keys combine ECDH, PSK and the complete transcript.
Private/shared scalar material is cleared before HELLO handling returns; reset,
rejection and expiry invalidate provisional keys. Low-order/zero shared-secret
inputs are rejected by libsodium. The implementation follows the X25519
primitive in [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) and the
[libsodium public-key binding guidance](https://doc.libsodium.org/advanced/scalar_multiplication).
This is the firmware's application handshake, not a claim of TLS/Noise compliance.

## Wire contract

The existing `HB` envelope retains **version 1** and its little-endian counters.
Its HELLO payload now requires **handshake version 2**; old PSK-only payloads are
rejected. Both initial messages have flags=0 and category/op=0. Integer and byte
lengths below are fixed.

- HELLO type `0x10`: `[2][client_nonce:16][client_X25519_public:32][client_proof:16]`
  (65 bytes). Define `C` as its first 49 bytes.
- HELLO_ACK type `0x11`: `[2][server_nonce:16][server_X25519_public:32][device_id:6][server_proof:16]`
  (71 bytes). Define `S` as its first 55 bytes and transcript `T=C||S` (104 bytes).
- `client_proof = HMAC-SHA256(PSK, "tos-host-v2-client" || C)[:16]`.
- `server_proof = HMAC-SHA256(PSK, "tos-host-v2-server" || T)[:16]`.

The client proof is checked before performing X25519. Given shared secret `Q`,
derive each 32-byte value with HKDF-SHA256, `salt=PSK`, `IKM=Q`, and
`info=label||T`. Labels are `tos-host-v2-a2d`, `tos-host-v2-d2a`, and
`tos-host-v2-code`. Labels contain no trailing NUL. Both public keys, both nonces,
handshake version and device identity are bound by the proofs and derivation.
The displayed code is the little-endian first u32 of the code value modulo
1,000,000, padded to six digits. The app-to-device and device-to-app MAC keys
have distinct labels.

HELLO_ACK establishes provisional keys only. The device UI holds a monotonically
changing token for the displayed exchange. OK approves exactly that token;
replacement, rejection, expiry or cancellation makes an old token unusable.
Neither commands nor application/log output are authorized by provisional keys.

After comparing codes, the client sends **HELLO_FINISH `0x12`**, flags=AUTH,
category/op=0, empty payload and counter=1, with the existing truncated envelope
MAC under `K_a2d`. The device requires physical approval, an unexpired exchange
and a valid FINISH MAC, then activates authentication and sends **HELLO_READY
`0x13`**, flags=AUTH, category/op=0 and empty payload, under `K_d2a`.
READY is serialized before other authenticated output. The first READY normally
uses counter=1 (HELLO_ACK consumed outbound counter=0). Subsequent client
commands start at counter=2 and all directional counters increase strictly.

A repeated, correctly MACed FINISH at counter=1 is idempotent: the device may
resend READY with a fresh outbound counter without resetting the command replay
baseline. This handles a lost READY. No command is dispatched by FINISH.
Other repeated inbound counters are rejected. No downgrade path accepts the
old PSK-only handshake.

Frame payloads retain the existing HMAC integrity envelope and are visible on
the underlying transport. This change adds ephemeral integrity keys and device
approval; payload confidentiality depends on the transport and is not provided
by this envelope. The PSK authorizes the same companion command set as before.

## Reference clients and dependencies

`tools/host_link/cli.py` and `hb_push.py` use `hb_pairing.py` for the exchange:

```sh
python3 -m pip install -r tools/host_link/requirements.txt
python3 tools/host_link/cli.py system ping
```

The client uses `cryptography` for X25519 and stdlib HMAC/HKDF. Both clients
validate authenticated replies, including their MAC, frame bounds and monotonic
counter, before returning them to command handlers. They print the comparison
code and wait for READY; commands are not sent before device confirmation.
They use monotonic timeout clocks. Python does not guarantee explicit erasure of
all immutable private-key copies; OS process/key handling remains relevant.

## Validation

`python3 tools/host_link/test_pairing.py` compiles the production security code
with ASan/UBSan, real libsodium X25519 and real OpenSSL HMAC/HKDF adapters.
Only NVS, RTOS, hardware RNG/MAC identity and serial/physical interaction are
fixtures. It checks the RFC 7748 public-key vector, proof/key agreement, low-order
input rejection, token replacement, physical approval, denial, expiry, reset,
reflection/replay rejection and failed PSK persistence. A shared-library test
interoperates with the actual Python cryptography client. Both actual USB tools
complete the exchange against that C backend and reject forged/replayed replies.
The real UI OK/BACK handler is also compiled and exercised.

Host test dependencies are a C compiler, OpenSSL/libsodium development libraries,
`pkg-config` and the Python requirements above. Optional
`HOST_LINK_TEST_CFLAGS` / `HOST_LINK_TEST_LDFLAGS` select an existing host crypto
build instead of pkg-config. CI installs those host dependencies; firmware still
uses the unchanged vendored libsodium and ESP-IDF mbedTLS.

These tests do not prove physical button operation, USB/BLE interoperability,
RNG entropy quality or resistance to hardware fault injection. Paired-device
acceptance remains in the cross-interface matrix (#156). Muse pairing remains
its separate protocol and is unchanged.
