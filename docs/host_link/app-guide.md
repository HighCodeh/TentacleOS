# Companion app implementation guide

How a desktop/mobile companion app talks to a TentacleOS device. This is the
practical, app-side recipe: transports, the byte-level frame, the security
handshake, and how to issue commands / read streams / logs / files.

The firmware owns the protocol; the app follows it. Pair this guide with:

- [`protocol.md`](./protocol.md) - the formal wire contract.
- [`../spi_bridge/README.md`](../spi_bridge/README.md) - the full `category`/`op` command table.
- [`README.md`](./README.md) - cross-firmware overview.

All multi-byte integers are **little-endian**.

---

## 1. Transports

The app speaks the **same framed byte protocol** over either transport. Pick one
connection; the device allows **only one companion session at a time**.

### 1.1 USB (CDC-ACM)

- The device enumerates as a composite USB device, **VID `0xCAFE` / PID `0x4001`**,
  with a CDC-ACM interface labelled **"TentacleOS Companion"**.
- On Linux it shows up as `/dev/ttyACM*`; on macOS `/dev/cu.usbmodem*`; on Windows
  a COM port. Open it **raw** (no line discipline, no echo, no newline translation):
  it is a transparent binary pipe, not a text console.
- Baud rate is irrelevant (USB CDC ignores it). Write whole frames; read a byte
  stream and reassemble (see §3).
- Note: this is the device's **native USB** port, separate from the USB-Serial-JTAG
  used for `idf.py monitor`.

### 1.2 BLE (GATT)

The C5 advertises as **`Tentacle-XXXX`** (last 4 hex of its MAC). GATT service
(NUS-style), UUIDs (current values; treat as the contract for now):

| Role | UUID | Properties |
|------|------|------------|
| Service | `6e540001-b5a3-f393-e0a9-e50e24dcca9e` | primary |
| RX (app → device) | `6e540002-b5a3-f393-e0a9-e50e24dcca9e` | write / write-no-response |
| TX (device → app) | `6e540003-b5a3-f393-e0a9-e50e24dcca9e` | notify (subscribe via CCCD) |

- Negotiate the largest MTU you can (the device prefers 512).
- **App → device:** write frames to RX. A single write must not exceed
  `min(MTU-3, 512)` bytes; split larger frames across consecutive writes (order is
  preserved, the device reassembles the byte stream).
- **Device → app:** subscribe to TX notifications. A frame larger than `MTU-3` is
  split across multiple notifications; concatenate notification payloads and
  reassemble by frame length (see §3).
- Bonding is "just works" (LE Secure Connections, no passkey). BLE encryption is
  defense-in-depth; the real auth is the host-link PSK envelope below.

---

## 2. Frame envelope

Every frame on either transport:

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0 | 2 | `MAGIC` | `0x48 0x42` ("HB") |
| 2 | 1 | `VER` | `1` |
| 3 | 1 | `FLAGS` | bit0 = authenticated; other bits 0 |
| 4 | 4 | `COUNTER` | u32 LE, per-direction monotonic |
| 8 | 2 | `LEN` | u16 LE, length of `BODY` |
| 10 | `LEN` | `BODY` | `[type u8][category u8][op u8][payload...]` |
| 10+LEN | 16 | `MAC` | present only if `FLAGS.bit0 == 1` |

`MAC = HMAC-SHA256(K_dir, frame[2 .. 10+LEN])[:16]` - i.e. over `VER`, `FLAGS`,
`COUNTER`, `LEN`, and the whole `BODY` (everything except the 2 MAGIC bytes and
the MAC itself), truncated to the first 16 bytes.

`BODY` types:

| type | name | direction | payload |
|------|------|-----------|---------|
| `0x01` | `CMD` | app → device | command args |
| `0x02` | `RESP` | device → app | `[status u8][data...]` |
| `0x03` | `STREAM` | device → app | live data (see §6) |
| `0x04` | `LOG` | device → app | `[source u8][level u8][utf-8 text]` |
| `0x10` | `HELLO` | app → device | handshake (unauthenticated) |
| `0x11` | `HELLO_ACK` | device → app | handshake (unauthenticated) |

`category`/`op` are the same ids the firmware uses internally
(`spi_id_t = (category << 8) | op`). Full table: [`../spi_bridge/README.md`](../spi_bridge/README.md).

---

## 3. Reassembly (RX byte stream)

Both transports deliver bytes that may split or coalesce frames. Buffer and parse:

```
loop:
  resync: drop bytes until buffer starts with 48 42
  if buffered < 10: wait for more
  LEN  = u16le(buf[8:10])
  auth = buf[3] & 1
  total = 10 + LEN + (auth ? 16 : 0)
  if buffered < total: wait for more
  handle(buf[0:total]); remove those bytes
```

---

## 4. Pairing & handshake (do this on every connect)

### 4.1 Get the PSK (once per device)

The device shows a **32-byte PSK** as a QR code + hex on its screen
(Settings -> PAIRING), or prints it on the dev console with `hostlink psk`. The
app reads/types it once and stores it in the OS keystore (Keychain / Credential
Manager / libsecret). The QR/hex encodes the 64-char lowercase hex of the PSK.

### 4.2 Handshake frames

1. **App -> device `HELLO`** (unauthenticated, `FLAGS=0`, no MAC). BODY:
   `[type=0x10][cat=0x00][op=0x00][host_ver=0x01][client_nonce[16]]`
   (`client_nonce` = 16 random bytes). `LEN = 20`.

2. **Device -> app `HELLO_ACK`** (unauthenticated). BODY:
   `[type=0x11][cat=0x00][op=0x00][host_ver=0x01][server_nonce[16]][device_id[6]][mac_psk[16]]`.
   - `device_id` = the device's 6-byte base MAC.
   - Verify `mac_psk == HMAC-SHA256(PSK, client_nonce || server_nonce)[:16]`. If it
     doesn't match, the device doesn't hold your PSK - abort.

3. **Both derive per-direction keys** (HKDF-SHA256, standard extract+expand):
   ```
   salt = client_nonce || server_nonce            # 32 bytes
   K_a2d = HKDF-SHA256(ikm=PSK, salt=salt, info="tos-host-a2d", L=32)   # app -> device
   K_d2a = HKDF-SHA256(ikm=PSK, salt=salt, info="tos-host-d2a", L=32)   # device -> app
   ```
   The `info` labels are exactly those 12 ASCII bytes (no NUL terminator).

4. **Reset counters.** Use a fresh monotonic counter per direction for this
   session. The app signs every app->device frame with `K_a2d`; it verifies every
   device->app frame with `K_d2a`.

After the handshake, **all** frames are authenticated (`FLAGS.bit0 = 1`, MAC
appended). The device rejects (drops + logs) any non-`HELLO` frame that fails the
MAC or counter check.

---

## 5. Authenticated frames, counters, replay

- Set `FLAGS = 0x01`, fill `COUNTER`, build `BODY`, then append
  `MAC = HMAC-SHA256(K_dir, frame[2 .. 10+LEN])[:16]`.
- **App -> device:** sign with `K_a2d`. Use a counter that **strictly increases**
  every frame. Starting at `0` (and incrementing) is fine; the device accepts the
  first authenticated frame at any value and then requires each next one to be
  greater.
- **Device -> app:** verify with `K_d2a` and check the counter strictly increases.
  The first authenticated device frame uses `COUNTER = 1` (the `HELLO_ACK` consumed
  `0`). Drop any frame whose MAC fails or whose counter is `<=` the last accepted.
- Reconnecting (or the link dropping) invalidates the session: redo the handshake.

---

## 6. Commands and responses

```
app -> CMD  : BODY = [0x01][category][op][args...]      (authenticated)
device -> RESP: BODY = [0x02][category][op][status u8][data...]
```

`status` (`spi_status_t`): `0` OK, `1` BUSY, `2` ERROR, `3` UNSUPPORTED,
`4` INVALID_ARG. The device echoes the same `category`/`op` in the `RESP`.

Example - **WiFi scan** (`category=0x01`, `op=0x10`), no args, authenticated, app
counter `5`:

```
48 42 01 01 05 00 00 00 03 00   header: MAGIC,VER,FLAGS=auth,COUNTER=5,LEN=3
01 01 10                        body:   type=CMD, cat=0x01, op=0x10
<16-byte MAC over bytes [2..13)>
```

List results (scan tables, etc.) are pulled with the generic data pipe
`SPI_ID_SYSTEM_DATA` (`category=0x00`, `op=0x05`): index `0xFFFF` returns the
count, `0..N-1` returns one item. See [`../spi_bridge/README.md`](../spi_bridge/README.md).

---

## 7. Streaming (sniffers, monitors)

Long-running ops push data instead of being polled:

```
app -> CMD     category/op of the op (e.g. WiFi sniffer 0x01/0x25), args
device -> RESP status=OK + data = [session_id u32]
device -> STREAM (pushed)  BODY = [0x03][category][op][record bytes]   # repeated
app -> CMD     SESSION_HEARTBEAT (0xFF/0xF0) every ~2 s  -> RESP [alive u8]
app -> CMD     SESSION_STOP (0xFF/0xF2) to end
```

- Keep sending the heartbeat: if the app goes silent for ~6 s (or the link drops),
  the device tears the session down. If the device ends it first (error/timeout),
  it pushes a `STREAM` with `category=0xFF op=0xF1` (session lost) and an empty
  payload.
- For the WiFi sniffer the `STREAM` record payload is
  `[rssi i8][channel u8][len u8][802.11 frame bytes]` - build your pcap/pcapng
  from `frame` (use `rssi`/`channel` for the radiotap header).
- Backpressure is handled device-side; just drain notifications/reads promptly.

---

## 8. Logs and the two consoles

The device pushes `LOG` frames: `BODY = [0x04][cat=0][op=0][source u8][level u8][utf-8 text]`.

- `source`: `0` = P4, `1` = C5 -> render two separate consoles.
- `level`: `0` ERROR, `1` WARN, `2` INFO, `3` DEBUG, `4` VERBOSE (colorize/filter).
- ANSI codes are already stripped. Logs always flow over USB; over BLE they are
  gated by the `log_over_ble` toggle (§10).

**Run a console line:** `CMD category=0x00 op=0x47` (`SYSTEM_CONSOLE_EXEC`) with
the raw command line as the payload. The command's stdout comes back as `LOG`
frames (`source=P4`); the `RESP` just confirms acceptance. Gated by the
`console_exec` toggle.

---

## 9. File transfer (P4-local)

All file ops are `category=0x00`; they run on the P4 and never touch the C5.
Paths are sandboxed to `/assets`, `/littlefs`, `/sdcard` (no `..`). Chunk size cap
is 1024 bytes.

| op | id | request payload | response data |
|----|----|-----------------|---------------|
| `FILE_LIST` | `0x40` | `<path>` | `[count u16]` then entries `[is_dir u8][size u32][nlen u8][name]` |
| `FILE_STAT` | `0x41` | `<path>` | `[exists u8][is_dir u8][size u32]` |
| `FILE_READ` | `0x42` | `[offset u32][len u16]<path>` | file bytes (0 bytes = EOF) |
| `FILE_WRITE` | `0x43` | `[offset u32][flags u8][path_len u16]<path><data>` | `[written u32]` |
| `FILE_DELETE` | `0x44` | `<path>` | (empty) |
| `FILE_MKDIR` | `0x45` | `<path>` | (empty) |

`FILE_WRITE` `flags` bit0 = create/truncate (start a fresh file); otherwise the
data is written in place at `offset` (file created if absent). Download = repeated
`FILE_READ` with advancing `offset` until a short/empty read; upload = repeated
`FILE_WRITE`.

---

## 10. Device state and settings

- **Device state:** `CMD category=0x00 op=0x46` (`SYSTEM_DEVICE_STATE`) ->
  `[battery_pct u8][charging u8][app_connected u8][p4_len u8][p4_ver][c5_len u8][c5_ver]`.
- **Read settings:** `op=0x48` (`GET_SETTINGS`) -> `[console_exec u8][log_over_ble u8]`.
- **Write settings:** `op=0x49` (`SET_SETTINGS`) with `[console_exec u8][log_over_ble u8]`.
  Both default to on. `console_exec=0` disables raw console exec (structured
  commands still work); `log_over_ble=0` stops background logs over BLE (USB always
  carries logs; console-exec output is always delivered).
- Version check: `op=0x04` (`SYSTEM_VERSION`) returns the C5 version string; the
  device-state frame carries both P4 and C5 versions.

---

## 11. Connect sequence (summary)

1. Open the transport (USB serial or BLE GATT + subscribe to TX notify).
2. `HELLO` -> `HELLO_ACK`; verify `mac_psk`; derive `K_a2d`/`K_d2a`; reset counters.
3. Read `SYSTEM_DEVICE_STATE` / `SYSTEM_VERSION`; check firmware compatibility.
4. Issue authenticated `CMD`s; handle `RESP`, `STREAM`, and `LOG` frames as they
   arrive. Heartbeat any active streaming session every ~2 s.
5. On disconnect, discard the session keys; a reconnect starts a fresh handshake.

---

## 12. Crypto checklist (must match the firmware exactly)

- HMAC-SHA256, truncated to the **first 16 bytes**.
- HKDF-SHA256 (RFC 5869 extract+expand), `ikm = PSK`,
  `salt = client_nonce || server_nonce`, `info` = `"tos-host-a2d"` / `"tos-host-d2a"`,
  output length 32.
- `mac_psk` and per-frame `MAC` are both HMAC-SHA256 truncated to 16 B; the
  per-frame MAC input is `frame[2 .. 10+LEN]` (header-after-MAGIC plus BODY).
- Verify MACs in constant time. Never log or persist the PSK or session keys in
  plaintext.
