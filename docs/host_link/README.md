# Host Link - unified overview

End-to-end companion-app link, spanning **both firmwares**. This document is the
single cross-firmware view: how the pieces fit, who owns what, and where to look.
It deliberately does **not** repeat the per-file reference tables - those live in
the component READMEs, and the byte-level wire format lives in the protocol spec.

- Companion app implementation guide: [`app-guide.md`](./app-guide.md)
- Wire spec: [`protocol.md`](./protocol.md)
- SPI bridge (P4↔C5 transport this rides on): [`../spi_bridge/README.md`](../spi_bridge/README.md)
- P4 component reference: the [`# P4`](#p4) section below.
- C5 component reference: the [`# C5`](#c5) section below.

## The model

```
                          USB CDC-ACM (P4-native)
 ┌──────────────┐  ◄───────────────────────────────►  ┌─────────────┐
 │ Companion app│                                      │  ESP32-P4   │
 │   (PC/phone) │  ◄───────────────────────────────►  │  (the brain)│
 └──────────────┘     BLE      ┌─────────────┐  relay  └─────────────┘
                  ◄───────────►│  ESP32-C5   │◄────────────────┘ SPI bridge
                               │ (BLE radio) │
                               └─────────────┘
```

- **The P4 is the single brain.** It terminates the security envelope, dispatches
  every command (locally or relayed to the C5 over SPI), and owns SD/flash and
  device state. Identical behavior on both transports - one place for crypto.
- **USB** terminates on the P4 (CDC-ACM in the TinyUSB composite, alongside the
  BadUSB HID).
- **BLE** terminates on the **C5** (it owns the radio). The C5 is a transparent
  byte relay - it never parses companion payloads; all auth is on the P4.
- **One companion session at a time.** The first transport to attach owns the
  session; a second attach is rejected until it releases.

## Frame envelope (summary)

```
[MAGIC 'H''B'][VER][FLAGS][COUNTER u32][LEN u16][BODY][MAC 16 if FLAGS.auth]
BODY = [type][category][op][payload]
```

`category`/`op` reuse the `spi_protocol.h` ids (`SPI_CMD(cat, op)`) - one HAL
shared by app, P4 and C5. Types: `CMD`, `RESP`, `STREAM`, `LOG`, `HELLO`,
`HELLO_ACK`. Full field semantics: see the wire spec.

## Security (P4 only)

- PSK (32 B) in NVS, auto-generated on first boot. Provisioned out-of-band: QR +
  hex on the P4 pairing screen (Settings → PAIRING) or the `hostlink psk` console
  command.
- `HELLO`/`HELLO_ACK` handshake → per-direction HKDF keys (`a2d`/`d2a`) + counter
  reset. Per-frame HMAC-SHA256 (truncated 16 B, mbedTLS) verified before any body
  parse; monotonic counter rejects replays. Only `HELLO` is accepted unauthenticated.
- BLE bonding is "just works" (LE Secure Connections, no MITM) on top of the PSK
  envelope - the PSK is the real trust boundary.

## Module map

**P4 (`firmware_p4/components/Service/host_link/`)** - core + both transports +
all local handlers: framing/dispatch/session arbitration, USB CDC, BLE relay,
security, the P4 log tee, the C5 log relay, file ops, device state/settings/
console-exec, and the streaming/heartbeat proxy.

**C5 (`firmware_c5/components/Service/host_link/`)** - BLE GATT server (NimBLE
NUS-style), the chunking transport to/from the P4, and the C5 log tee.

New SPI ids backing all this live in `spi_protocol.h` under `SPI_CAT_HOST = 0x06`
(BLE relay) plus P4-local `SPI_CAT_SYSTEM` ops (`SYSTEM_LOG`, `FILE_*`,
`DEVICE_STATE`, settings, console-exec). Per-file detail: the component READMEs.

## Command routing (P4)

After auth, each `CMD` is routed by id: file ops → local; device-state/settings/
console-exec → local; `SPI_CAT_SESSION` (heartbeat/stop) → stream proxy (local,
**not** relayed - the P4 keeps heartbeating the C5 itself); session-start ops
(sniffer) → `spi_session`; everything else → relayed to the C5.

## Logs & two consoles

Both chips tee their `ESP_LOGx` (without losing the local dev console). P4 logs
are emitted directly; C5 logs stream to the P4 (`SPI_ID_SYSTEM_LOG`) and are
re-emitted. Each `LOG` frame carries a `source` byte (P4 / C5) so the app renders
two separate consoles. Console-exec output is delivered as console LOG frames.

## Toggles (NVS, default on)

| Setting | Off behavior |
|---------|--------------|
| `console_exec` | app can't run raw console lines (structured `CMD`s still work) |
| `log_over_ble` | no background logs over BLE; **USB always carries logs**; console-exec output always delivered |

## Boot order

**P4 (`kernel.c`):** `host_link_state_init` → `host_link_stream_init` →
`host_link_init` → `host_link_cdc_init` → `host_link_log_init` →
`host_link_c5log_init` → `host_link_ble_init` (BLE advertising starts on demand:
`hostlink ble on`).

**C5 (`kernel.c`):** `c5_log_init` right after `spi_bridge_slave_init`. The GATT
server is started on demand by the P4, not at boot.

## Phase status

All 8 phases (core+CDC, P4 log tee, security, BLE relay, C5 log forward, file ops,
device state+toggles+console-exec, streaming+heartbeat proxy) are **implemented
and build-validated on both firmwares**. See §15 of the wire spec for the
per-phase breakdown.

## Caveats (not yet hardware-tested)

- The transport layer is unexercised: the dev board's native USB pads are
  unsoldered and BLE hasn't been run. Everything is build-validated only.
- **NimBLE is single-owner**: host-link BLE, MeshCore and Meshtastic are mutually
  exclusive.
- **One `spi_session`**: the on-device UI sniffer and the companion sniffer are
  mutually exclusive (a start preempts the other).
- Device→app frames larger than the BLE MTU are split across notifications and
  reassembled by the app via `LEN`.

---

# P4

Terminates the companion-app protocol on the **ESP32-P4**. The P4 is the single
brain: it owns the security envelope, dispatches commands (locally or relayed to
the C5 over the SPI bridge), and owns SD/flash storage and device state. The same
behavior is exposed over **two transports** - USB CDC-ACM (P4-native) and BLE
(terminated on the C5, relayed here). Only **one** companion session is active at
a time.

- Unified cross-firmware overview: [`README.md`](./README.md)
- Wire format (envelope, types, ids): [`protocol.md`](./protocol.md)

This README is the **P4 component reference** - the file map and P4-side wiring.
The frame envelope, BODY types and the `SPI_CMD(cat, op)` id scheme are defined in
the wire spec; the end-to-end (app↔P4↔C5) picture is in the unified overview.

## Files

| File | Role |
|------|------|
| `host_link.c` | Core: reassembly, frame encode/decode, dispatch, single-session arbitration, `emit_frame` (RESP/LOG/STREAM). |
| `host_link_cdc.c` | USB CDC-ACM transport (TinyUSB composite). Claims the session on DTR; drops bytes when no app is attached. |
| `host_link_ble.c` | BLE transport relay: chunks frames to the C5 (`SPI_ID_HOST_TX`), reassembles inbound (`SPI_ID_HOST_RX` stream), drives the C5 GATT on/off and connection status. |
| `host_link_sec.c` | Security: PSK in NVS (auto-generated), `HELLO`/`HELLO_ACK` handshake, HKDF per-direction keys, per-frame MAC verify/sign, counter replay rejection. mbedTLS. |
| `host_link_log.c` | P4 log tee (`esp_log_set_vprintf`): ANSI strip, level, drop-oldest ring, worker → `LOG` frames `source=P4`. |
| `host_link_c5log.c` | Consumes the `SPI_ID_SYSTEM_LOG` stream from the C5 → `LOG` frames `source=C5`. |
| `host_link_files.c` | P4-local `FILE_*` ops over `/assets`, `/littlefs`, `/sdcard` (POSIX VFS), path-sandboxed, chunked. |
| `host_link_state.c` | Device state (battery/versions), the two settings toggles (NVS), and raw console exec (captured stdout → console LOG frames). |
| `host_link_stream.c` | Streaming + heartbeat proxy: starts session ops via `spi_session`, pushes records as `STREAM` frames, app-liveness watchdog, link-loss teardown. |

## Command routing (in `host_link.c`)

After authentication, `process_frame` routes each `CMD` by id:

1. `host_files_is_file_op` → local file ops (bypass the 256 B relay cap).
2. `host_state_is_local_op` → device state / settings / console exec.
3. `category == SPI_CAT_SESSION` → heartbeat/stop handled by the stream proxy
   (**not** relayed; the P4 keeps heartbeating the C5 itself).
4. `host_stream_is_session_op` → start a session-based stream (sniffer).
5. otherwise → relayed to the C5 via `spi_bridge_send_command`.

## Security model

- Only `HELLO` is accepted before keys exist. Every other inbound frame must be
  authenticated (valid MAC, fresh counter) or it is dropped + logged.
- Per-direction HKDF keys (`a2d`/`d2a`) prevent reflection; fresh nonces per
  handshake prevent cross-session replay.
- The PSK is provisioned out-of-band: shown as a QR + hex on the P4 pairing
  screen (Settings → PAIRING) and via the `hostlink psk` console command.
- BLE bonding is "just works" (LE Secure Connections, no MITM) on top of the PSK
  envelope, which is the real trust boundary.

## Toggles (NVS, default on)

| Setting | Effect when off |
|---------|-----------------|
| `console_exec` | the app cannot run raw console lines (structured `CMD`s still work) |
| `log_over_ble` | background logs are not sent over BLE; **USB always carries logs**, and console-exec output is always delivered |

## Boot wiring (`kernel.c`)

```
host_link_state_init();   // load toggles
host_link_stream_init();  // streaming proxy
host_link_init();         // core + PSK
host_link_cdc_init();     // USB transport
host_link_log_init();     // P4 log tee
host_link_c5log_init();   // C5 log relay
host_link_ble_init();     // BLE relay infra (advertising on demand: `hostlink ble on`)
```

## Status

All phases implemented and build-validated. **Not yet hardware-tested** - the
dev board's native USB pads are unsoldered and BLE is unexercised. Known runtime
caveats: NimBLE is single-owner (host-link BLE / MeshCore / Meshtastic are
mutually exclusive); the UI sniffer and the companion sniffer share one
`spi_session` (mutually exclusive); large device→app frames split across BLE
notifications and are reassembled by the app via `LEN`.

---

# C5

The companion app's **BLE transport terminates on the ESP32-C5** (it owns the BLE
radio). The C5 is a **transparent byte relay**: it ferries opaque host-link frames
to/from the P4 over the SPI bridge and forwards its own logs up. **All
crypto/auth lives on the P4** - the C5 never parses companion payloads.

Mirrors the proven Meshtastic/MeshCore phone-bridge pattern.

- Unified cross-firmware overview: [`README.md`](./README.md)
- Wire format: [`protocol.md`](./protocol.md)

This README is the **C5 component reference** (BLE relay + log tee).

## Files

| File | Role |
|------|------|
| `host_link_gatt.c` | NimBLE GATT server (NUS-style): a **write** char (app→device) and a **notify** char (device→app). "Just works" LE Secure Connections (no MITM). Splits notifications by ATT MTU; the app reassembles by frame `LEN`. |
| `host_transport.c` | Chunk/reassembly between BLE and SPI. BLE write → `SPI_ID_HOST_RX` stream (C5→P4). `SPI_ID_HOST_TX` chunks (P4→C5) → reassemble → BLE notify. Reuses `spi_mesh_chunk_hdr_t`. |
| `c5_log.c` | C5 log tee (`esp_log_set_vprintf`): keeps the local dev console, ANSI strip + level, drop-oldest ring, worker → `SPI_ID_SYSTEM_LOG` stream (C5→P4) as `[level u8][utf-8 text]`. |

## SPI ops (category `SPI_CAT_HOST = 0x06`, in `spi_protocol.h`)

| Op | Id | Direction | Purpose |
|----|----|-----------|---------|
| `SPI_ID_HOST_BLE_INIT` | `0x06A0` | P4→C5 cmd | start GATT + advertise (`spi_host_init_t { name_prefix }`) |
| `SPI_ID_HOST_BLE_STOP` | `0x06A1` | P4→C5 cmd | stop GATT |
| `SPI_ID_HOST_TX` | `0x06A2` | P4→C5 cmd (push) | device→app bytes → BLE notify |
| `SPI_ID_HOST_RX` | `0x06A3` | C5→P4 stream | app→device bytes (BLE write) |
| `SPI_ID_HOST_STATUS` | `0x06A4` | P4→C5 cmd | poll `spi_host_status_t { ble_connected, ble_subscribed }` |

`SPI_ID_SYSTEM_LOG` (`0x0007`, C5→P4 stream) carries the forwarded log lines.

## Dispatch

`SPI_CAT_HOST` is routed to `bt_dispatcher_execute` (alongside `SPI_CAT_BT` /
`SPI_CAT_MCORE`) in `spi_bridge.c`. The handlers call into `host_transport` /
`host_link_gatt`.

## Boot wiring (`kernel.c`)

`c5_log_init()` runs right after `spi_bridge_slave_init()` (it pushes to the SPI
stream). The GATT server is started on demand by the P4 (`SPI_ID_HOST_BLE_INIT`),
not at boot, so it doesn't hog NimBLE from the BLE attack features.

## Caveats

- **NimBLE is single-owner**: host-link BLE, MeshCore, and Meshtastic each refuse
  to init while another holds NimBLE.
- The C5 log stream is always enabled on this side; the P4 drops the resulting
  `LOG` frames when no companion session is active, and the **log-over-BLE**
  toggle (P4) gates BLE delivery. Build-validated; **not yet hardware-tested**.
