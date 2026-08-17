# Host Link Protocol - Companion App ↔ TentacleOS

**Status: CONFIRMED v1 (firmware-owned).**
The **firmware is the source of truth** for the wire protocol; the desktop/web
companion app only follows it. This document is the agreed contract - the
`[FW]` decisions from the original proposal are resolved below. A few hardware
identifiers (USB VID/PID, BLE UUIDs) are marked **TBD** and assigned during
implementation; they don't affect the protocol shape.

Related docs:
- [`README.md`](./README.md) - unified host-link overview
- [`../spi_bridge/README.md`](../spi_bridge/README.md) - P4 ↔ C5 architecture overview
- `firmware_*/components/Service/spi_bridge/README.md` - command reference, session lifecycle, stream transport
- `firmware_*/components/Service/spi_bridge/spi_protocol.h` - shared command table (`spi_id_t`)

---

## 1. Goal

Let the companion app drive the device over **USB and BLE** using the **same
command set the firmware speaks internally** (`spi_id_t` = `Category`+`Op`). We
do not invent a parallel command protocol - the app reuses the existing
commands, stream format, and session lifecycle. The host link only adds what an
external, untrusted connection needs that the internal SPI trace does not:
framing, authentication, push delivery, file transfer, and log/console access.

---

## 2. Architecture - Model A (P4 is the single hub) - CONFIRMED

```
                USB  ┌─────────────┐  SPI (existing bridge)  ┌─────────────┐
 Companion app ─────►│  ESP32-P4   │◄───────────────────────►│  ESP32-C5   │
 (desktop / web)     │  brain/OS   │                         │   radios    │
                BLE  │  SD · USB   │                         │ WiFi·BT·LoRa│
        ┌────────────│  SPI master │                         │  SPI slave  │
        │   (relay)  └─────────────┘                         └─────────────┘
        │                   ▲
        └───────────────────┘  BLE terminates on the C5 (it owns the radio);
                               the C5 RELAYS framed host bytes to the P4.
```

- **USB** terminates on the **P4** (P4 owns USB).
- **BLE** terminates on the **C5** (C5 owns the BLE radio); the C5 is a
  **transparent byte relay** that ferries companion frames to/from the P4 over
  the existing SPI bridge.
- **The P4 is the one brain:** it terminates the security envelope, dispatches
  commands (locally or to the C5 over SPI, exactly as today), owns SD storage
  and device state. Identical behavior on both transports; one place for crypto.

**C5 ⇄ P4 relay mechanism:** reuses the **proven Meshtastic/MeshCore phone-bridge
pattern** (BLE-on-C5 → SPI → P4 already ships today). Two SPI ops carry opaque
host bytes: `SPI_ID_HOST_RX` (C5→P4, inbound from app; C5 buffers, raises IRQ,
P4 pulls via the stream path) and `SPI_ID_HOST_TX` (P4→C5, outbound to app; C5
notifies over BLE). Host frames larger than one SPI frame are chunked by the
firmware. The C5 never parses companion payloads - it only moves bytes; **all
crypto/auth is on the P4.** The app never sees this internal hop.

---

## 3. Transports

### 3.1 USB - CDC-ACM (dedicated)
- A dedicated **CDC-ACM** interface in the P4's TinyUSB composite (alongside the
  existing BadUSB HID). The raw developer console (`idf.py monitor`) stays on the
  **USB-Serial-JTAG**, so dev logs and the companion link don't collide.
- Bidirectional, framed: app→device = `CMD`; device→app = `RESP`/`STREAM`/`LOG`.
- **TBD:** VID/PID for auto-detect.

### 3.2 BLE - GATT companion service (on the C5)
- A GATT service with a **write** characteristic (app→device) and a **notify**
  characteristic (device→app). Frames larger than the MTU span multiple
  notifications and are reassembled by `LEN`.
- **TBD:** service/characteristic UUIDs, advertised name / scan-match, target
  MTU, bonding requirement (LE Secure Connections recommended on top of the PSK).

### 3.3 Single companion session
Only **one** companion connection is active at a time. While one app is
connected (and authenticated), the device **rejects** a second connection on
either transport.

---

## 4. What we REUSE from the SPI bridge (unchanged)

- **Command identity:** `Category` + `Op` → `spi_id_t` (`SPI_CMD(cat, op)`). Same
  IDs as `spi_protocol.h`.
- **Message types:** `CMD 0x01`, `RESP 0x02`, `STREAM 0x03` (host link adds `LOG`).
- **Response status:** `RESP` payload byte 0 = `spi_status_t` (`OK 0`, `BUSY 1`,
  `ERROR 2`, `UNSUPPORTED 3`, `INVALID_ARG 4`).
- **Stream record layout:** `[u16 batch_len]` then records `[u16 op][u8 len][payload]`,
  payload carrying `spi_stream_meta_t { session_id, seq }` + op data.
- **Generic data pipe** for list results: `SPI_ID_SYSTEM_DATA` (`0xFFFF` count,
  `0..N-1` item, `0xEEEE` stats, `0xDDDD` deauth counter).
- **Session lifecycle:** random 32-bit `session_id`, heartbeat (2 s) + watchdog
  (5 s) → `SPI_ID_SESSION_LOST`, backpressure window (64), `SPI_ID_SESSION_STOP`.
- **Version check:** `SPI_ID_SYSTEM_VERSION`.

**NOT reused:** SPI physical artifacts - fixed 264 B / 2048 B frames, 4-byte DMA
alignment, master-poll. The host link uses variable length-prefixed frames and
**push** delivery.

---

## 5. Host frame format (the envelope)

Every byte on the USB/BLE link is one host frame. **Little-endian.**

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0 | 2 | `MAGIC` | `0x48 0x42` ("HB") - frame sync / resync anchor |
| 2 | 1 | `VER` | host-link protocol version (separate from firmware version) |
| 3 | 1 | `FLAGS` | bit0 = authenticated; rest reserved |
| 4 | 4 | `COUNTER` | u32, per-direction monotonic - replay protection |
| 8 | 2 | `LEN` | u16, length of `BODY` |
| 10 | `LEN` | `BODY` | see below |
| 10+LEN | 16 | `MAC` | HMAC-SHA256(`K_dir`, bytes `[2 .. 10+LEN)`) truncated to 128 bits |

`MAC` is **fixed 16 B**. The P4 **verifies the MAC and checks the counter before
parsing `BODY`**; on failure it drops the frame and logs a security event. The
P4 has hardware SHA acceleration, so per-frame HMAC is cheap even on pcap streams.

```
BODY = | type (1B) | category (1B) | op (1B) | payload (...) |
  CMD    (0x01)  payload = command args
  RESP   (0x02)  payload = [status u8][data...]
  STREAM (0x03)  payload = [u16 batch_len][record]...  (record = [u16 op][u8 len][meta+data])
  LOG    (0x04)  payload = [source u8][level u8][utf-8 text]   (see §7)
```

Pre-auth handshake frames (`HELLO` / `HELLO_ACK`, §6) travel with
`FLAGS.authenticated = 0` and are the only frames accepted before keys exist.

**Reassembly:** sync on `MAGIC`, read the 10-byte header for `LEN`, accumulate
`LEN + 16` more bytes.

---

## 6. Security

The internal SPI trace is trusted; **USB and especially BLE are not.** The device
drives real RF/USB attack hardware, so command authenticity + replay protection
are mandatory.

### 6.1 Handshake (on every connect)
Dedicated pre-auth frames (unauthenticated):
1. App → device: `HELLO { host_ver, client_nonce[16] }`.
2. Device → app: `HELLO_ACK { host_ver, server_nonce[16], device_id, mac_psk }`
   where `mac_psk = HMAC(PSK, client_nonce || server_nonce)` (proves the device
   holds the PSK - mutual auth).
3. Both derive per-direction session keys and reset counters:
   - `K_a2d = HKDF(PSK, client_nonce || server_nonce, "a2d")`
   - `K_d2a = HKDF(PSK, client_nonce || server_nonce, "d2a")`
4. All later frames carry `FLAGS.authenticated = 1`, the sender's direction key,
   and a monotonic counter.

Per-direction keys prevent reflection; fresh nonces prevent cross-session replay.

### 6.2 PSK provisioning - QR/code on the P4 display
First-time pairing: the user authorizes a new app; the **P4 shows a QR/code on
its display**, the app reads it (or the user types it), and both derive the PSK.
Works identically for USB and BLE. App-side, the PSK is stored in the OS keystore
(Keychain / Credential Manager / libsecret) - never plaintext, never logged.

### 6.3 Alignment
OWASP 2021: A02 (HMAC-SHA256/HKDF, BLE LE Secure Connections), A07 (session keys,
re-auth on reconnect), A08 (per-frame integrity), A01 (only the paired app
commands the device).

---

## 7. Logs & console (two separate consoles)

Both chips emit their own `ESP_LOGx`. Each tees its output via
`esp_log_set_vprintf` (without losing the local dev console). The P4 forwards
both streams to the app as `LOG` frames tagged with a **`source`** byte so the
app can render **two consoles** (P4 / C5):

```
LOG payload = [source u8: 0=P4, 1=C5][level u8: E=0,W=1,I=2,D=3,V=4][utf-8 text]
```

- **P4 logs:** teed locally on the P4.
- **C5 logs:** teed on the C5 → pushed to the P4 over SPI via `SPI_ID_SYSTEM_LOG`
  (same mechanism as the existing `SPI_ID_MESH_LOG_PUSH`) → relayed out as `LOG`
  with `source=C5`.
- ANSI color codes are stripped; the app colorizes/filters by `level` and `source`.
- Each log channel has a small **ring + drop-oldest** buffer; only the boot burst
  is heavy (runtime logging is low-rate), so drops are rare and counted.

**Console command execution:** the app may send a raw console line; the P4 runs it
through `esp_console` and the output flows back through the `LOG` channel.

### Toggles (device settings)
| Setting | Default | Effect when off |
|---------|---------|-----------------|
| **Console exec** (app→device) | on, BLE + USB | app cannot run raw console lines (structured `CMD`s still work) |
| **Log over BLE** (global) | on | **no** logs (P4 or C5) are sent over BLE; USB always carries logs |

Structured commands (scan, capture, file ops, …) and log *reading* over USB are
always available; the toggles only gate raw console exec and BLE log delivery.

---

## 8. Command / response flow

```
app → CMD  (category, op, args)        authenticated, counter++
P4  → RESP (status, data)              authenticated, counter++
```
- Reliable + ordered (USB CDC / BLE ACL) → no app-level retransmit; the counter
  detects gaps/replays.
- The P4 dispatches by `category`/`op` exactly as it dispatches its own commands
  today (local handler or relay to C5 over SPI).
- List results pulled via the generic data pipe (`SPI_ID_SYSTEM_DATA`).
- **Full command set on both transports** (no USB-only restriction), gated only
  by the console-exec toggle above.

---

## 9. Streaming & sessions (push-based)

Long-running ops (sniffers, monitors) reuse the firmware session model; the
device **pushes** `STREAM` frames instead of the host polling:

```
app → CMD    SPI_ID_WIFI_APP_SNIFFER { params }
P4  → RESP   status OK + spi_session_resp_t { session_id }
P4  → STREAM batched records { session_id, seq, payload } …   (pushed)
app → CMD    SPI_ID_SESSION_HEARTBEAT { session_id, last_acked_seq }   (every 2 s)
app → CMD    SPI_ID_SESSION_STOP { session_id }
```
- **Liveness is two-level:** the app heartbeats the P4 over the host link; the P4
  keeps heartbeating the C5 over SPI (existing). If the app disappears, the P4
  tears down and stops heartbeating the C5 → the C5 watchdog kills the session.
- **Backpressure / anti-zombie** unchanged (window 64; 5 s watchdog →
  `SPI_ID_SESSION_LOST`). Matters more on BLE/USB (links drop/unplug).
- The app builds a real pcap/pcapng from the raw 802.11 bytes in the stream
  records → full Wireshark-level dissection in real time.

---

## 10. File transfer (download + edit)

The app has a file viewer/editor, so it can **download and write** files over
both transports. The P4 exposes **two separate filesystems, both physically on
the P4** - the app browses/edits each independently:

- **Internal flash** - the `assets` / `littlefs` partitions (config, defaults,
  captures saved to flash, …).
- **micro-SD** - via SDMMC (`/sdcard`), the larger removable storage.

The **path root selects the filesystem** (e.g. `/assets/…`, `/littlefs/…`,
`/sdcard/…`); ops are sandboxed to the mounted roots (no escaping them). Large
files (pcap, MBs) are transferred in **chunks with offsets**; big reads reuse the
batched stream transport.

Proposed `SYSTEM`-category ops (final ids assigned in `spi_protocol.h`):
- `FILE_LIST { path }` → directory entries (name, size, is_dir) via the data pipe.
- `FILE_STAT { path }` → size, flags.
- `FILE_READ { path, offset, len }` → chunk (streamed for large files).
- `FILE_WRITE { path, offset, data }` → write/edit a chunk (create/truncate flags).
- `FILE_DELETE { path }`, `FILE_MKDIR { path }`.

Writes are bounded/validated by the P4 (path sandbox to the storage mount; no
escaping it). All file ops require an authenticated session.

---

## 11. Device state (pushed)

The device pushes a status frame on connect, on change, and periodically:
```
DeviceStatus = { battery_pct u8, charging u8, app_connected u8,
                 fw_version_p4[..], fw_version_c5[..] }
```
Carried as a `SYSTEM` op (`SPI_ID_SYSTEM_STATUS` extended, or a dedicated
`SPI_ID_SYSTEM_DEVICE_STATE`). Battery comes from the BQ25896 gauge; versions
reuse the existing version contract.

---

## 12. Versioning & compatibility

- Host-link `VER` is exchanged in the handshake; mismatch → app refuses to
  proceed with a clear message.
- The app also reads `SPI_ID_SYSTEM_VERSION` on connect and checks the firmware
  version. **Min firmware version:** the first build that ships host-link support
  (TBD once it lands; bump from the current `1.3.0`).

---

## 13. Firmware components to build

- **P4 host-link core:** frame envelope + HMAC/HKDF + counter + handshake + PSK
  store (NVS) + dispatch (reuses SPI dispatch) + push.
- **P4 CDC-ACM** interface (TinyUSB composite) + auto-detect VID/PID.
- **C5 BLE companion GATT** service + `SPI_ID_HOST_RX`/`HOST_TX` relay (mesh
  bridge pattern).
- **Log tee** on both chips + `SPI_ID_SYSTEM_LOG` forward (C5→P4) + `LOG` frames
  with `source`.
- **Console-exec** command + the two settings toggles.
- **File ops** (`FILE_*`) over both P4 filesystems (internal flash + micro-SD),
  path-rooted and sandboxed, chunked.
- **Device-state** push (battery + versions + connection).

---

## 14. Open hardware identifiers (TBD - don't block the protocol)
- USB VID/PID.
- BLE service/characteristic UUIDs, advertised name, target MTU, bonding policy.
- Final `SPI_ID_*` op numbers for the new commands (`HOST_RX/TX`, `SYSTEM_LOG`,
  `FILE_*`, `DEVICE_STATE`).
- Minimum firmware version once host-link ships.

---

## 15. Implementation plan (phased build order)

Built in small, independently testable phases. **All 8 phases are implemented and
build-validated on both firmwares.** They have **not** been exercised on hardware
yet (the dev board's native USB pads are unsoldered and BLE is untested), so each
phase still lists its concrete on-device check for when that's possible.

Status legend: ✅ implemented (build-validated).

1. ✅ **P4 host-link core + CDC-ACM (no crypto).** Frame envelope encode/decode +
   dispatch reusing the existing SPI dispatcher, over USB CDC.
   (`host_link.c`, `host_link_cdc.c`.) *Test:* a serial tool sends
   `PING`/`VERSION`, gets `RESP`. - note: now requires a handshake first (phase 3).
2. ✅ **Log tee on P4 + `LOG` frames** (`source=P4`). vprintf hook → ANSI strip →
   drop-oldest ring → worker. (`host_link_log.c`.) *Test:* app sees P4 logs.
3. ✅ **Security envelope** - HMAC-SHA256/HKDF (mbedTLS), per-direction keys,
   monotonic counter, `HELLO`/`HELLO_ACK` handshake, PSK in NVS, QR/hex on the P4
   display. (`host_link_sec.c`; UI `companion_pairing`; `cmd_hostlink`.) *Test:*
   unauthenticated frames rejected; paired app works; replay rejected.
4. ✅ **BLE companion (C5 GATT) + relay** `SPI_ID_HOST_RX`/`HOST_TX` (mesh-bridge
   pattern, NimBLE NUS-style, "just works" SC). C5 `host_link_gatt.c` +
   `host_transport.c`; P4 `host_link_ble.c`; single-session arbitration in the
   core. *Test:* same command set over BLE; single-app enforcement.
5. ✅ **C5 log forward** `SPI_ID_SYSTEM_LOG` (C5→P4 stream) → `LOG` frames with
   `source=C5`. C5 `c5_log.c`; P4 `host_link_c5log.c`. *Test:* both consoles
   populate.
6. ✅ **File ops** `FILE_*` over both filesystems (flash + micro-SD), chunked,
   path-sandboxed; BLE notify split by MTU. (`host_link_files.c`.) *Test:*
   download a pcap, edit + write back a config file.
7. ✅ **Device state** (battery/charging/versions) + the two settings toggles
   (console-exec, log-over-BLE) + raw console exec (captures stdout → console
   LOG frames). (`host_link_state.c`.) *Test:* state read; toggles persist.
8. ✅ **Streaming push + heartbeat proxy** (reuses the existing `spi_session`).
   Sniffer records → `STREAM` frames; app heartbeat refreshes liveness; app
   silence / link loss tears the session down. (`host_link_stream.c`.) *Test:*
   live sniffer pcap streams to the app; pulling the link triggers
   `SPI_ID_SESSION_LOST`.

Each new command (`HOST_RX/TX`, `SYSTEM_LOG`, `FILE_*`, `DEVICE_STATE`,
settings, console-exec) lives in `spi_protocol.h` so it stays part of the
single-source-of-truth HAL. Component-level docs: `firmware_p4/components/
Service/host_link/README.md` and `firmware_c5/components/Service/host_link/README.md`.

## 16. What the app implements

- A transport-agnostic backend with two implementations (**USB serial** / **BLE**)
  behind one interface.
- Host-frame encode/decode + HMAC envelope + counter + handshake.
- Reuse of `spi_id_t` IDs derived from `spi_protocol.h` (single source of truth).
- Mapping device→app messages onto app state (commands, streams, the two log
  consoles, file viewer/editor, device-status indicators).

The backend is the trust boundary; the UI layer only ever sees validated state.
