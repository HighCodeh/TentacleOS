# TentacleOS - P4 ↔ C5 SPI Bridge

How the two microcontrollers in TentacleOS talk to each other.

This is the **architecture overview** that ties both sides together. For
side-specific detail and migration recipes see the component READMEs:
- `firmware_p4/components/Service/spi_bridge/README.md` (master, protocol spec,
  command reference, session lifecycle, stream transport)
- `firmware_c5/components/Service/spi_bridge/README.md` (slave)

---

## 1. Roles

TentacleOS runs on two chips with a clean split of responsibilities:

| Chip | Role | Owns |
|------|------|------|
| **ESP32-P4** | Main OS / "brain" | UI (LVGL display), apps, storage (micro-SD via SDMMC), USB, the SPI **master** |
| **ESP32-C5** | Radio co-processor | WiFi, Bluetooth (NimBLE), LoRa, the SPI **slave** |

The P4 has no native WiFi/BT radio, so every radio action (scan, connect,
sniff, attack, mesh, …) is a **command sent to the C5** over SPI. The C5
executes it on the radio and returns results / streams data back. Anything that
needs the micro-SD is routed from the C5 to the P4 over this same bridge - the
C5 stores only on its internal flash (LittleFS).

```
        ┌────────────────────┐   SPI (10 MHz, mode 0, DMA)   ┌────────────────────┐
        │      ESP32-P4      │  ── SCLK / MOSI / MISO / CS ──►│      ESP32-C5      │
        │   (master / OS)    │  ◄──────── IRQ ───────────────│  (slave / radio)   │
        │                    │  ── UART1 + RESET/BOOT ───────►│  (firmware flash)  │
        └────────────────────┘                                └────────────────────┘
```

---

## 2. Physical layer

Two independent links connect the chips:

### 2.1 SPI bridge (runtime data path)

Standard **4-wire SPI, 1-bit, full-duplex, mode 0, 10 MHz, DMA-driven**
(`SPI_DMA_CH_AUTO` on both sides). The P4 is master, the C5 is slave. A separate
GPIO line (**IRQ**) lets the slave signal "response ready" to the master.

| Signal | P4 GPIO | C5 GPIO |
|--------|---------|---------|
| SCLK   | 20      | 6       |
| MOSI   | 21      | 7       |
| MISO   | 22      | 2       |
| CS     | 23      | 10      |
| IRQ    | 2       | 3       |

- **DMA** is mandatory: frames are 264 B (and stream frames 2 KB), far above the
  SPI hardware FIFO (~64 B). DMA also frees the CPU during transfers.
- Because of DMA, **every transfer length must be a multiple of 4 bytes** - see
  the frame sizing notes below.

### 2.2 UART + control (firmware flashing only)

The P4 flashes the C5's firmware over a separate UART link using the official
`esp-serial-flasher` component. Not used at runtime.

| Signal | P4 GPIO | C5 |
|--------|---------|----|
| UART TX (P4→C5) | 46 | GPIO12 (U0RXD) |
| UART RX (C5→P4) | 47 | GPIO11 (U0TXD) |
| RESET  | 48 | EN |
| BOOT   | 33 | IO0 (GPIO0 strapping) |

---

## 3. Frame format

Every packet on the SPI bus starts with a fixed **5-byte header**:

```c
typedef struct {
  uint8_t sync;     // 0xAA
  uint8_t type;     // 0x01 CMD, 0x02 RESP, 0x03 STREAM
  uint8_t category; // spi_cat_t - subsystem
  uint8_t op;       // operation within the category
  uint8_t length;   // payload bytes that follow (0-255)
} spi_header_t;
```

`SPI_FRAME_SIZE` = header + 256 B payload, **rounded up to a multiple of 4** for
DMA = **264 B**. The command/response path always transfers `SPI_FRAME_SIZE`.

### Command identifier = `category` + `op`

A command is identified by two header bytes that pack into a single 16-bit value
in code via `SPI_CMD(cat, op)`. The C5 routes to a dispatcher by `category`
alone; `op` selects the operation within it.

| Category | Value | Routed to |
|----------|-------|-----------|
| `SPI_CAT_SYSTEM`  | `0x00` | inline system handlers |
| `SPI_CAT_WIFI`    | `0x01` | `wifi_dispatcher` |
| `SPI_CAT_BT`      | `0x02` | `bt_dispatcher` |
| `SPI_CAT_LORA`    | `0x03` | (lora) |
| `SPI_CAT_MESH`    | `0x04` | meshtastic (split BLE/WiFi transport) |
| `SPI_CAT_MCORE`   | `0x05` | meshcore → `bt_dispatcher` |
| `SPI_CAT_HOST`    | `0x06` | companion host-link BLE relay → `bt_dispatcher` |
| `SPI_CAT_SESSION` | `0xFF` | inline session handlers |

In C, the `SPI_ID_*` constants stay single named values (e.g.
`SPI_ID_WIFI_SCAN = SPI_CMD(SPI_CAT_WIFI, 0x10) = 0x0110`), so call sites and
dispatcher `case` labels are unchanged - only the wire carries the two bytes.
The full command table lives in the P4 component README.

### Response status

A `RESP` frame's **payload byte 0 is the status** (`spi_status_t`): `OK (0)`,
`BUSY (1)`, `ERROR (2)`, `UNSUPPORTED (3)`, `INVALID_ARG (4)`; the rest of the
payload is the response data.

---

## 4. Command / response flow

The bridge is a master-driven request/response protocol with an IRQ handshake:

```
P4 (master)                                  C5 (slave)
   │  clock CMD frame (264 B)  ───────────►   receive into armed RX buffer
   │                                          route by category → dispatcher
   │                                          build RESP, arm TX buffer
   │  ◄────────── IRQ rising edge ──────────  pulse IRQ (~10 µs)
   │  clock again to read RESP (264 B) ───►   transmit RESP
   │  parse status + payload
```

- The P4 catches the IRQ via a **GPIO rising-edge interrupt** (ISR → semaphore),
  so the C5 only needs a short (~10 µs) pulse - no held level, no millisecond
  delay.
- The C5's `bridge_task` keeps a **receive transaction always armed in hardware**
  (it queues the next RX before the current response finishes), so a command is
  never missed in the gap between transfers, even under task preemption.
- A per-command **mutex** on the P4 serialises commands; long radio ops get
  longer timeouts (`SPI_TIMEOUT_WIFI_MS = 20 s`, default `1 s`).

---

## 5. Generic data pipe (pulling lists)

Operations that produce lists (scan results, etc.) don't push everything at
once. The C5 points the bridge at its result array via
`spi_bridge_provide_results(ptr, count, item_size)`, and the P4 pulls items with
`SPI_ID_SYSTEM_DATA` using special indices:

| Index | Meaning |
|-------|---------|
| `0xFFFF` | item count |
| `0..N-1` | one item |
| `0xEEEE` | live `spi_sniffer_stats_t` |
| `0xDDDD` | deauth counter |

This is also how the **Packet Monitor** works: it's a counter-only sniffer mode
that just polls the stats - it does not stream frames.

---

## 6. Streaming (live data, e.g. pcap)

Long-running ops that emit a continuous feed (WiFi/BLE sniffers, mesh phone
bridge) use a stream path. The C5 buffers records in a 64-deep ring; the P4
drains them by polling `SPI_ID_SYSTEM_STREAM`.

To keep throughput high, the transport **batches many records into one large
transfer** (`SPI_STREAM_FRAME_SIZE = 2048 B`) instead of one record per
round-trip:

```
STREAM frame payload (after the 5-byte header, type = STREAM):
  [u16 batch_len][record][record]...        record = [u16 op][u8 len][len bytes]
```

- `batch_len = 0` ⇒ no data pending ⇒ the P4 backs off and polls later.
- The P4 unpacks and dispatches **each record to its op's callback**, exactly as
  if it had arrived in its own frame - so session/`seq`/backpressure semantics
  stay **per record**.
- The command/response path is untouched (still `SPI_FRAME_SIZE`).

**Throughput:** the original one-record-per-frame + 1 ms IRQ pulse capped streams
at ~120 KB/s. Shortening the IRQ pulse (~3×) plus batching lifts the ceiling to
roughly ~1 MB/s at 10 MHz - enough for dense-AP / targeted capture. A saturated
data channel can still overrun it (physics on a 1-bit link), in which case
records are **dropped and counted** (capture is never blocked) - the right tool
there is a capture filter.

---

## 7. Session lifecycle (anti-zombie + backpressure)

Streaming/long-running ops are wrapped in a **session** so the C5 never keeps
running into the void if the P4 crashes or stops listening:

1. **Session ID** - the C5 returns a random 32-bit `session_id` on START; both
   sides track it, and stream records carry it so stale data is discarded.
2. **Heartbeat** - the P4 sends `SPI_ID_SESSION_HEARTBEAT` every **2 s** with its
   `last_acked_seq`. A C5 watchdog (1 s tick) kills any session whose last
   heartbeat is older than **5 s** and emits `SPI_ID_SESSION_LOST`.
3. **Backpressure window** - each record carries `{session_id, seq}`. The C5
   refuses to emit when `seq - last_acked_seq >= SPI_SESSION_WINDOW (64)`,
   preventing overflow when the radio produces faster than the bridge drains.

| Direction | When | Packet |
|-----------|------|--------|
| P4→C5 | START | `op_id` + params |
| C5→P4 | START reply | status + `spi_session_resp_t { session_id }` |
| P4→C5 | every 2 s | `SPI_ID_SESSION_HEARTBEAT` + `{ session_id, last_acked_seq }` |
| C5→P4 | data | batched STREAM frame (§6); each record = `op` + meta + payload |
| P4→C5 | STOP | `SPI_ID_SESSION_STOP` + `{ session_id }` |
| C5→P4 | watchdog kill | `SPI_ID_SESSION_LOST` + `{ session_id, cmd }` |

---

## 8. Firmware versioning & flashing

The C5 firmware is **embedded in the P4 firmware** at build time (bootloader +
partition table + app). On boot, `bridge_manager` queries the C5's version
(`SPI_ID_SYSTEM_VERSION`) and compares it against the P4's expected version
(`FIRMWARE_VERSION`, currently **1.3.0**). On mismatch (or no response) the P4
re-flashes the C5 over the UART link using `esp-serial-flasher`, writing the
full image:

| Image | C5 flash offset |
|-------|-----------------|
| bootloader | `0x2000` |
| partition table | `0x8000` |
| app | `0x10000` |

Any breaking change to the wire format must bump **both** versions
(`FIRMWARE_VERSION` on the P4 and `SPI_FW_VERSION_STRING` on the C5) to the same
new value, forcing a re-sync.

---

## 9. Key source files

**P4 (master)**
- `components/Service/spi_bridge/` - `spi_bridge.c` (send command, stream task),
  `spi_session.c` (session/heartbeat), `spi_protocol.h` (shared contract)
- `components/Drivers/spi_bridge_phy/` - SPI master PHY + IRQ edge ISR
- `components/Service/bridge_manager/` - version check + C5 recovery
- `components/Service/c5_flasher/` - `esp-serial-flasher` wrapper

**C5 (slave)**
- `components/Service/spi_bridge/` - `spi_bridge.c` (`bridge_task` routing +
  always-armed RX + stream batching), `wifi_dispatcher.c`, `bt_dispatcher.c`,
  `session_manager.c`, `spi_protocol.h`
- `components/Drivers/spi_slave/` - SPI slave driver (queued transactions)

`spi_protocol.h` is kept in sync between the two firmwares (the P4 copy is a
superset - it has port-scan commands the C5 doesn't implement).

---

## 10. Design constraints & limits

- **1-bit SPI** - dual/quad isn't wired, so the raw ceiling is the clock
  (~1.25 MB/s at 10 MHz). Higher clocks (20/40 MHz) are possible but limited by
  the SPI slave timing and trace integrity.
- **264 B / 2 KB frames must stay 4-byte aligned** for DMA.
- The two `spi_protocol.h` copies are maintained by hand - keep them in sync.
- Command `op` values currently reuse the legacy single-byte ids (e.g. WiFi ops
  start at `0x10`); renumbering to `0x01`-based per category is a safe cosmetic
  follow-up.

---

# P4

This component manages the high-speed communication link between the **ESP32-P4 (Main OS)** and the **ESP32-C5 (Radio Co-processor)**.

## Architecture
The P4 acts as the **SPI Master**. It is responsible for:
1. Generating the SCLK and managing the CS line.
2. Initiating all command transfers.
3. Handling the **IRQ (Handshake)** signal from the C5 to know when response data is ready.
4. Managing the C5 lifecycle (Reset, Boot mode, and Firmware Updates via UART).

## Protocol Specification
Every packet follows a 5-byte fixed header:
- `Sync (0xAA)`: Packet synchronization.
- `Type`: `0x01` (Command), `0x02` (Response), `0x03` (Stream).
- `Category`: Subsystem selector (`spi_cat_t`: WiFi `0x01`, BT `0x02`, …). The C5
  routes a command to a dispatcher by this byte alone.
- `Op`: Operation within the category.
- `Length`: Size of the following payload (0-255 bytes).

`Category` + `Op` together form the packed command identifier (`spi_id_t`),
built via `SPI_CMD(cat, op)`. Use `spi_header_cmd()` / `spi_header_set_cmd()` to
read/write the pair as a single 16-bit value.

## Command Reference

Every command's `spi_id_t` packs `Category` (high byte) and `Op` (low byte) via `SPI_CMD(cat, op)`. On the wire those are the 3rd and 4th header bytes; in code use the single 16-bit `SPI_ID_*` constant.

### System (`0x00`)

| Command | Op | `spi_id_t` |
|---------|----|------------|
| `SPI_ID_SYSTEM_PING` | `0x01` | `0x0001` |
| `SPI_ID_SYSTEM_STATUS` | `0x02` | `0x0002` |
| `SPI_ID_SYSTEM_REBOOT` | `0x03` | `0x0003` |
| `SPI_ID_SYSTEM_VERSION` | `0x04` | `0x0004` |
| `SPI_ID_SYSTEM_DATA` | `0x05` | `0x0005` |
| `SPI_ID_SYSTEM_STREAM` | `0x06` | `0x0006` |
| `SPI_ID_SYSTEM_LOG` | `0x07` | `0x0007` |

`SPI_ID_SYSTEM_LOG` is a C5→P4 stream carrying log lines (`[level u8][utf-8]`) for
the companion's C5 console (see the host-link docs).

System ops `0x40`-`0x49` (`FILE_*`, `SYSTEM_DEVICE_STATE`, `SYSTEM_CONSOLE_EXEC`,
`SYSTEM_GET_SETTINGS`, `SYSTEM_SET_SETTINGS`) are **P4-local host-link commands**:
they share the `spi_id_t` space so the companion app and P4 agree, but they are
handled on the P4 and **never travel over this SPI bridge**. They are documented
in [`../host_link/protocol.md`](../host_link/protocol.md).

### WiFi (`0x01`)

| Command | Op | `spi_id_t` |
|---------|----|------------|
| `SPI_ID_WIFI_SCAN` | `0x10` | `0x0110` |
| `SPI_ID_WIFI_CONNECT` | `0x11` | `0x0111` |
| `SPI_ID_WIFI_DISCONNECT` | `0x12` | `0x0112` |
| `SPI_ID_WIFI_GET_STA_INFO` | `0x13` | `0x0113` |
| `SPI_ID_WIFI_SET_AP` | `0x14` | `0x0114` |
| `SPI_ID_WIFI_START` | `0x15` | `0x0115` |
| `SPI_ID_WIFI_STOP` | `0x16` | `0x0116` |
| `SPI_ID_WIFI_SAVE_AP_CONFIG` | `0x17` | `0x0117` |
| `SPI_ID_WIFI_SET_ENABLED` | `0x18` | `0x0118` |
| `SPI_ID_WIFI_SET_AP_PASSWORD` | `0x19` | `0x0119` |
| `SPI_ID_WIFI_SET_AP_MAX_CONN` | `0x1A` | `0x011A` |
| `SPI_ID_WIFI_SET_AP_IP` | `0x1B` | `0x011B` |
| `SPI_ID_WIFI_PROMISC_START` | `0x1C` | `0x011C` |
| `SPI_ID_WIFI_PROMISC_STOP` | `0x1D` | `0x011D` |
| `SPI_ID_WIFI_CH_HOP_START` | `0x1E` | `0x011E` |
| `SPI_ID_WIFI_CH_HOP_STOP` | `0x1F` | `0x011F` |
| `SPI_ID_WIFI_APP_SCAN_AP` | `0x20` | `0x0120` |
| `SPI_ID_WIFI_APP_SCAN_CLIENT` | `0x21` | `0x0121` |
| `SPI_ID_WIFI_APP_BEACON_SPAM` | `0x22` | `0x0122` |
| `SPI_ID_WIFI_APP_DEAUTHER` | `0x23` | `0x0123` |
| `SPI_ID_WIFI_APP_FLOOD` | `0x24` | `0x0124` |
| `SPI_ID_WIFI_APP_SNIFFER` | `0x25` | `0x0125` |
| `SPI_ID_WIFI_APP_EVIL_TWIN` | `0x26` | `0x0126` |
| `SPI_ID_WIFI_APP_DEAUTH_DET` | `0x27` | `0x0127` |
| `SPI_ID_WIFI_APP_PROBE_MON` | `0x28` | `0x0128` |
| `SPI_ID_WIFI_APP_SIGNAL_MON` | `0x29` | `0x0129` |
| `SPI_ID_WIFI_SNIFFER_SET_SNAPLEN` | `0x2B` | `0x012B` |
| `SPI_ID_WIFI_SNIFFER_SET_VERBOSE` | `0x2C` | `0x012C` |
| `SPI_ID_WIFI_SNIFFER_SAVE_FLASH` | `0x2D` | `0x012D` |
| `SPI_ID_WIFI_SNIFFER_SAVE_SD` | `0x2E` | `0x012E` |
| `SPI_ID_WIFI_SNIFFER_FREE_BUFFER` | `0x2F` | `0x012F` |
| `SPI_ID_WIFI_SNIFFER_STREAM_SD` | `0x30` | `0x0130` |
| `SPI_ID_WIFI_SNIFFER_CLEAR_PMKID` | `0x31` | `0x0131` |
| `SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID` | `0x32` | `0x0132` |
| `SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE` | `0x33` | `0x0133` |
| `SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID` | `0x34` | `0x0134` |
| `SPI_ID_WIFI_DEAUTH_STATUS` | `0x35` | `0x0135` |
| `SPI_ID_WIFI_DEAUTH_SEND_RAW` | `0x36` | `0x0136` |
| `SPI_ID_WIFI_ASSOC_REQUEST` | `0x37` | `0x0137` |
| `SPI_ID_WIFI_DEAUTH_SEND_FRAME` | `0x38` | `0x0138` |
| `SPI_ID_WIFI_DEAUTH_SEND_BROADCAST` | `0x39` | `0x0139` |
| `SPI_ID_WIFI_TARGET_SCAN_START` | `0x3A` | `0x013A` |
| `SPI_ID_WIFI_TARGET_SCAN_STATUS` | `0x3B` | `0x013B` |
| `SPI_ID_WIFI_TARGET_SAVE_FLASH` | `0x3C` | `0x013C` |
| `SPI_ID_WIFI_TARGET_SAVE_SD` | `0x3D` | `0x013D` |
| `SPI_ID_WIFI_TARGET_FREE` | `0x3E` | `0x013E` |
| `SPI_ID_WIFI_PROBE_SAVE_FLASH` | `0x3F` | `0x013F` |
| `SPI_ID_WIFI_PROBE_SAVE_SD` | `0x40` | `0x0140` |
| `SPI_ID_WIFI_EVIL_TWIN_TEMPLATE` | `0x41` | `0x0141` |
| `SPI_ID_WIFI_EVIL_TWIN_HAS_PASSWORD` | `0x42` | `0x0142` |
| `SPI_ID_WIFI_EVIL_TWIN_GET_PASSWORD` | `0x43` | `0x0143` |
| `SPI_ID_WIFI_EVIL_TWIN_RESET_CAPTURE` | `0x44` | `0x0144` |
| `SPI_ID_WIFI_CLIENT_SAVE_FLASH` | `0x45` | `0x0145` |
| `SPI_ID_WIFI_CLIENT_SAVE_SD` | `0x46` | `0x0146` |
| `SPI_ID_WIFI_AP_SAVE_FLASH` | `0x47` | `0x0147` |
| `SPI_ID_WIFI_AP_SAVE_SD` | `0x48` | `0x0148` |
| `SPI_ID_WIFI_PORT_SCAN_TARGET_RANGE` | `0x49` | `0x0149` |
| `SPI_ID_WIFI_PORT_SCAN_TARGET_LIST` | `0x4A` | `0x014A` |
| `SPI_ID_WIFI_PORT_SCAN_NETWORK` | `0x4B` | `0x014B` |
| `SPI_ID_WIFI_PORT_SCAN_CIDR` | `0x4C` | `0x014C` |
| `SPI_ID_WIFI_PORT_SCAN_STOP` | `0x4D` | `0x014D` |
| `SPI_ID_WIFI_GET_MAC` | `0x4E` | `0x014E` |
| `SPI_ID_WIFI_GET_IP_INFO` | `0x4F` | `0x014F` |
| `SPI_ID_WIFI_EVIL_TWIN_TMPL_BEGIN` | `0xA0` | `0x01A0` |
| `SPI_ID_WIFI_EVIL_TWIN_TMPL_CHUNK` | `0xA1` | `0x01A1` |

### Bluetooth (`0x02`)

| Command | Op | `spi_id_t` |
|---------|----|------------|
| `SPI_ID_BT_SCAN` | `0x50` | `0x0250` |
| `SPI_ID_BT_CONNECT` | `0x51` | `0x0251` |
| `SPI_ID_BT_DISCONNECT` | `0x52` | `0x0252` |
| `SPI_ID_BT_GET_INFO` | `0x53` | `0x0253` |
| `SPI_ID_BT_INIT` | `0x54` | `0x0254` |
| `SPI_ID_BT_DEINIT` | `0x55` | `0x0255` |
| `SPI_ID_BT_START` | `0x56` | `0x0256` |
| `SPI_ID_BT_STOP` | `0x57` | `0x0257` |
| `SPI_ID_BT_SET_RANDOM_MAC` | `0x58` | `0x0258` |
| `SPI_ID_BT_START_ADV` | `0x59` | `0x0259` |
| `SPI_ID_BT_STOP_ADV` | `0x5A` | `0x025A` |
| `SPI_ID_BT_SET_MAX_POWER` | `0x5B` | `0x025B` |
| `SPI_ID_BT_TRACKER_START` | `0x5C` | `0x025C` |
| `SPI_ID_BT_TRACKER_STOP` | `0x5D` | `0x025D` |
| `SPI_ID_BT_GET_ADDR_TYPE` | `0x5E` | `0x025E` |
| `SPI_ID_BT_SAVE_ANNOUNCE_CFG` | `0x5F` | `0x025F` |
| `SPI_ID_BT_APP_SCANNER` | `0x60` | `0x0260` |
| `SPI_ID_BT_APP_SNIFFER` | `0x61` | `0x0261` |
| `SPI_ID_BT_APP_SPAM` | `0x62` | `0x0262` |
| `SPI_ID_BT_APP_FLOOD` | `0x63` | `0x0263` |
| `SPI_ID_BT_APP_SKIMMER` | `0x64` | `0x0264` |
| `SPI_ID_BT_APP_TRACKER` | `0x65` | `0x0265` |
| `SPI_ID_BT_APP_GATT_EXP` | `0x66` | `0x0266` |
| `SPI_ID_BT_SPAM_LIST_LOAD` | `0x68` | `0x0268` |
| `SPI_ID_BT_SPAM_LIST_BEGIN` | `0x69` | `0x0269` |
| `SPI_ID_BT_SPAM_LIST_ITEM` | `0x6A` | `0x026A` |
| `SPI_ID_BT_SPAM_LIST_COMMIT` | `0x6B` | `0x026B` |
| `SPI_ID_BT_SCREEN_INIT` | `0x6C` | `0x026C` |
| `SPI_ID_BT_SCREEN_DEINIT` | `0x6D` | `0x026D` |
| `SPI_ID_BT_SCREEN_IS_ACTIVE` | `0x6E` | `0x026E` |
| `SPI_ID_BT_SCREEN_SEND_PARTIAL` | `0x6F` | `0x026F` |
| `SPI_ID_BT_L2CAP_STATUS` | `0x70` | `0x0270` |
| `SPI_ID_BT_HID_INIT` | `0x71` | `0x0271` |
| `SPI_ID_BT_HID_DEINIT` | `0x72` | `0x0272` |
| `SPI_ID_BT_HID_IS_CONNECTED` | `0x73` | `0x0273` |
| `SPI_ID_BT_HID_SEND_KEY` | `0x74` | `0x0274` |

### LoRa (`0x03`)

| Command | Op | `spi_id_t` |
|---------|----|------------|
| `SPI_ID_LORA_RX` | `0x80` | `0x0380` |
| `SPI_ID_LORA_TX` | `0x81` | `0x0381` |

### Meshtastic (`0x04`)

| Command | Op | `spi_id_t` |
|---------|----|------------|
| `SPI_ID_MESH_BLE_INIT` | `0x90` | `0x0490` |
| `SPI_ID_MESH_BLE_STOP` | `0x91` | `0x0491` |
| `SPI_ID_MESH_WIFI_INIT` | `0x92` | `0x0492` |
| `SPI_ID_MESH_WIFI_STOP` | `0x93` | `0x0493` |
| `SPI_ID_MESH_FROMRADIO_PUSH` | `0x94` | `0x0494` |
| `SPI_ID_MESH_LOG_PUSH` | `0x95` | `0x0495` |
| `SPI_ID_MESH_STATUS` | `0x96` | `0x0496` |
| `SPI_ID_MESH_TORADIO_STREAM` | `0x97` | `0x0497` |

### MeshCore (`0x05`)

| Command | Op | `spi_id_t` |
|---------|----|------------|
| `SPI_ID_MCORE_BLE_INIT` | `0x98` | `0x0598` |
| `SPI_ID_MCORE_BLE_STOP` | `0x99` | `0x0599` |
| `SPI_ID_MCORE_TX_PUSH` | `0x9A` | `0x059A` |
| `SPI_ID_MCORE_RX_STREAM` | `0x9B` | `0x059B` |
| `SPI_ID_MCORE_STATUS` | `0x9C` | `0x059C` |

### Host Link (`0x06`)

Companion BLE relay (the C5 owns the radio; the P4 owns crypto). The C5 routes
this category to `bt_dispatcher`. See [`../host_link/`](../host_link/README.md).

| Command | Op | `spi_id_t` | Direction |
|---------|----|------------|-----------|
| `SPI_ID_HOST_BLE_INIT` | `0xA0` | `0x06A0` | P4→C5 cmd: start GATT + advertise |
| `SPI_ID_HOST_BLE_STOP` | `0xA1` | `0x06A1` | P4→C5 cmd: stop GATT |
| `SPI_ID_HOST_TX` | `0xA2` | `0x06A2` | P4→C5 push: device→app (BLE notify) |
| `SPI_ID_HOST_RX` | `0xA3` | `0x06A3` | C5→P4 stream: app→device (BLE write) |
| `SPI_ID_HOST_STATUS` | `0xA4` | `0x06A4` | P4→C5 cmd: poll BLE connection state |

### Session (`0xFF`)

| Command | Op | `spi_id_t` |
|---------|----|------------|
| `SPI_ID_SESSION_HEARTBEAT` | `0xF0` | `0xFFF0` |
| `SPI_ID_SESSION_LOST` | `0xF1` | `0xFFF1` |
| `SPI_ID_SESSION_STOP` | `0xF2` | `0xFFF2` |

## Frame Example

The 5-byte header maps directly to `spi_header_t`:

```c
typedef struct {
  uint8_t sync;     // 0xAA
  uint8_t type;     // spi_type_t: CMD 0x01 / RESP 0x02 / STREAM 0x03
  uint8_t category; // spi_cat_t
  uint8_t op;       // operation within the category
  uint8_t length;   // payload bytes that follow (0-255)
} spi_header_t;
```

**Example - WiFi scan** (`SPI_ID_WIFI_SCAN` = `SPI_CMD(SPI_CAT_WIFI, 0x10)` = `0x0110`), no payload:

```
P4 -> C5  (command)
  AA 01 01 10 00
  ^  ^  ^  ^  ^
  |  |  |  |  +-- length = 0
  |  |  |  +----- op       = 0x10
  |  |  +-------- category = 0x01 (WiFi)
  |  +----------- type     = 0x01 (CMD)
  +-------------- sync     = 0xAA

C5 -> P4  (response, after raising IRQ) - payload byte 0 is the status
  AA 02 01 10 01 00
  ^  ^  ^  ^  ^  ^
  |  |  |  |  |  +-- status   = 0x00 (SPI_STATUS_OK)
  |  |  |  |  +----- length   = 1
  |  |  |  +-------- op       = 0x10
  |  |  +----------- category = 0x01
  |  +-------------- type     = 0x02 (RESP)
  +----------------- sync     = 0xAA
```

Scan results are then pulled item-by-item through the **Generic Data Pipe** (`SPI_ID_SYSTEM_DATA`) described below.

## Generic Data Pipe
To keep the bridge simple, we use a "Dumb Pipe" approach for large data sets (like Scan results):
1. **Pull Count**: Call `SPI_ID_SYSTEM_DATA` with index `0xFFFF`.
2. **Pull Item**: Call `SPI_ID_SYSTEM_DATA` with index `0 to N`.
3. **Real-time Stats**: Call `SPI_ID_SYSTEM_DATA` with index `0xEEEE` to get a `sniffer_stats_t` structure.

## Stream Transport (batched)

Long-running ops (sniffers, mesh bridge) emit a continuous stream of records.
The P4 drains them by polling `SPI_ID_SYSTEM_STREAM`. To keep throughput high,
the transport **batches many records into one transfer** instead of one record
per round-trip:

- The C5 buffers records in a ring (depth `SPI_STREAM_QUEUE_LEN = 64`). On a
  `SPI_ID_SYSTEM_STREAM` poll it packs as many as fit into a single large frame
  of `SPI_STREAM_FRAME_SIZE` (2048 B) and the P4 always clocks that fixed size.
- Stream frame layout (after the 5-byte header, `type = STREAM`):
  `[u16 batch_len]` then `batch_len` bytes of records, each
  `[u16 op][u8 len][len bytes]`. `batch_len = 0` means "no data" → the P4 backs
  off and polls again later.
- The P4 unpacks and dispatches **each record to its `op`'s stream callback**,
  exactly as if it had arrived in its own frame - so session/`seq`/backpressure
  semantics stay **per record** (see Session Lifecycle). The command/response
  path is unaffected and still uses `SPI_FRAME_SIZE`.

Two related tunables: the C5 signals readiness with a short rising-edge IRQ
pulse (~10 µs - the P4 catches it via a GPIO edge interrupt, so no held level
or millisecond delay is needed), and bursts are absorbed by the 64-deep ring;
when it overflows, records are dropped and counted (never block capture).

### Stream Example (WiFi sniffer)

**Producer - C5** (each captured 802.11 frame becomes one record; the session
layer adds the `{session_id, seq}` meta and applies backpressure):
```c
spi_wifi_sniffer_frame_t f = { .rssi = -42, .channel = 6, .len = n, /* data */ };
session_manager_try_emit(session_id, (const uint8_t *)&f, 3 + n);
```

**On the wire** - the P4 polls `SYSTEM_STREAM` and the C5 returns one 2 KB frame
batching the queued records:
```
P4 -> C5:  AA 01 00 06 00              poll: SYSTEM_STREAM (cat 0x00, op 0x06)
C5 -> P4:  AA 03 00 00 00 | <payload, padded to 2048 B>
           ^ header, type=STREAM (cat/op/length unused for the batch)
  payload:
    20 00                              batch_len = 0x0020 (32 bytes of records)
    ── record 1 ───────────────────────
    25 01                              op = 0x0125 (SPI_ID_WIFI_APP_SNIFFER)
    0D                                 rec_len = 13
    34 12 00 00  01 00 00 00           spi_stream_meta_t { session_id=0x1234, seq=1 }
    D6 06 02 AA BB                     frame: rssi=-42, ch=6, len=2, data=AA BB
    ── record 2 (same op, seq=2) ──────
    25 01 0D  34 12 00 00 02 00 00 00  D6 06 02 CC DD
    ── remaining bytes up to 2048 = padding, ignored (batch_len bounds it) ──
```

**Consumer - P4** (each record is dispatched to the op's callback; the meta is
stripped by the session layer, so the consumer sees only the frame):
```c
// registered via spi_session_start(SPI_ID_WIFI_APP_SNIFFER, …, on_stream, …)
static void on_stream(const uint8_t *payload, uint8_t len) {
    const spi_wifi_sniffer_frame_t *f = (const void *)payload; // one captured frame
    storage_stream_write(pcap, f->data, f->len);
}
```
See `wifi_sniffer.c` (both firmwares) for the full reference implementation.

## Adding a New Command
To add a new feature (e.g., "GPS Get Location"):

1. **Protocol**: Add `SPI_ID_GPS_GET` to `spi_protocol.h`.
2. **C5 Dispatcher**:
   - Open `wifi_dispatcher.c` (or a new `gps_dispatcher.c`).
   - Add the case for `SPI_ID_GPS_GET`.
   - Call the actual hardware driver.
   - If it returns a list, call `spi_bridge_provide_results(pointer, count, size)`.
3. **P4 Wrapper**:
   - Create a wrapper in `Applications` or `Service`.
   - Use `spi_bridge_send_command(SPI_ID_GPS_GET, ...)` to trigger the action.
   - Use the generic `SPI_ID_SYSTEM_DATA` to pull results if necessary.

## Session Lifecycle (Long-Running Operations)

For operations that run for an extended period (sniffers, monitors, attacks
that emit a stream of events), the basic request-response model is unsafe:
if the master dies or stops listening, the slave keeps running indefinitely
and sends data into the void. The session protocol fixes this with three
mechanisms working together:

### 1. Session ID
Every long-running operation is tagged with a 32-bit `session_id` chosen
randomly by the C5 when the operation starts. Both sides track the active
session; stream packets carry the id so stale data can be discarded after
a restart.

### 2. Heartbeat (anti-zombie)
The P4 sends `SPI_ID_SESSION_HEARTBEAT { session_id, last_acked_seq }`
every **2 seconds** while a session is active. The C5 has a watchdog task
that runs every second and kills any session whose last heartbeat is older
than **5 seconds**. When killed, the C5 emits `SPI_ID_SESSION_LOST` as a
stream so the master can react (e.g., restart, show error UI).

If the master detects 3 consecutive heartbeat failures, it assumes the
session is gone and fires its local `on_lost` callback.

### 3. Backpressure window
Stream packets carry `{ session_id, seq }`. The master accumulates
`last_acked_seq` and reports it via heartbeat. The C5 refuses to emit if
`seq - last_acked_seq >= SPI_SESSION_WINDOW (64)` - protects against
buffer overflow when the slave produces faster than the master drains.
Drops are counted and logged.

### Wire shapes

| Direction | When | Packet |
|-----------|------|--------|
| P4 → C5 | START | `op_id` + op-specific params |
| C5 → P4 | START reply | status byte + `spi_session_resp_t { session_id }` |
| P4 → C5 | every 2s | `SPI_ID_SESSION_HEARTBEAT` + `spi_heartbeat_req_t` |
| C5 → P4 | heartbeat reply | status + `spi_heartbeat_resp_t { alive }` |
| C5 → P4 | data | batched STREAM frame (see "Stream Transport"); each record = `op` + `spi_stream_meta_t { session_id, seq }` + payload |
| P4 → C5 | STOP | `SPI_ID_SESSION_STOP` + `spi_session_stop_req_t { session_id }` |
| C5 → P4 | watchdog kill | `SPI_ID_SESSION_LOST` STREAM + `spi_session_lost_t { session_id, cmd }` |

### Master API

```c
// Start a long-running operation. Spawns heartbeat task internally.
uint32_t spi_session_start(spi_id_t op_id,
                           const uint8_t *params, uint8_t params_len,
                           spi_session_stream_cb_t on_stream,  // peeled meta
                           spi_session_lost_cb_t on_lost);

// Clean teardown. Kills heartbeat, sends STOP.
esp_err_t spi_session_stop(uint32_t session_id);
```

Returns `SPI_SESSION_INVALID_ID` (0) on START failure. The `on_stream`
callback receives the **operation payload only** - the meta header is
stripped and ack tracking is invisible to the consumer.

### Slave API (C5)

```c
// Open a session for the op_id. Closes any prior session first.
uint32_t session_manager_start(spi_id_t op_id, session_kill_cb_t kill_cb);

// Emit a stream packet (prefixes meta, applies backpressure).
esp_err_t session_manager_try_emit(uint32_t session_id,
                                   const uint8_t *data, uint8_t len);
```

The op implementation stores the returned `session_id` and uses it for
every emit. The `kill_cb` is invoked by the watchdog if heartbeats stop -
the op should call its own `_stop()` from there.

### Migrating a New Operation (recipe)

There are two patterns depending on whether the op emits streams. Both
are used in the codebase - see `wifi_sniffer` (streaming) and
`wifi_deauther` (non-streaming) as references.

#### Pattern A - Non-streaming op (deauther, flood, evil_twin, …)

The op runs in background but does NOT emit packets to the master. The
master polls for results via `SPI_ID_SYSTEM_DATA` if it needs data.

**C5 side (only the dispatcher changes - op .c/.h untouched):**
```c
// In wifi_dispatcher.c (or bt_dispatcher.c):
static void killed_my_op(spi_id_t id) { (void)id; my_op_stop(); }

case SPI_ID_MY_OP:
    if (!my_op_start(...)) return SPI_STATUS_ERROR;
    return open_session(SPI_ID_MY_OP, killed_my_op,
                        out_resp_payload, out_resp_len, my_op_stop);
```

**P4 side (wrapper):**
```c
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

bool my_op_start(...) {
    s_session_id = spi_session_start(SPI_ID_MY_OP, params, len, NULL, NULL);
    return s_session_id != SPI_SESSION_INVALID_ID;
}

void my_op_stop(void) {
    if (s_session_id != SPI_SESSION_INVALID_ID) {
        spi_session_stop(s_session_id);
        s_session_id = SPI_SESSION_INVALID_ID;
    }
}
```

#### Pattern B - Streaming op (sniffer, ble_sniffer, …)

The op emits a continuous stream of packets to the master.

**C5 side:**
1. Add `static uint32_t s_session_id = SPI_SESSION_INVALID_ID;` to the
   op's `.c`.
2. Add public `_bind_session(uint32_t)` setter and
   `_session_killed(spi_id_t)` kill callback (the latter calls `_stop()`).
3. Replace `spi_bridge_stream_push(SPI_ID_OP, data, len)` with
   `session_manager_try_emit(s_session_id, data, len)`.
4. In the dispatcher, replace the START handler with: call
   `op_start(...)`, then `session_manager_start(SPI_ID_OP, op_session_killed)`,
   then `op_bind_session(sid)`, then return
   `spi_session_resp_t { sid }` as response payload.

**P4 side:**
1. Replace `spi_bridge_send_command(SPI_ID_OP, …)` +
   `spi_bridge_register_stream_cb(SPI_ID_OP, raw_cb)` with a single
   `spi_session_start(SPI_ID_OP, params, …, on_stream, on_lost)`.
2. Store the returned `session_id`.
3. Change STOP to `spi_session_stop(session_id)`.
4. The `on_stream` callback signature is
   `void(const uint8_t *payload, uint8_t len)` - the meta header is
   already stripped.

### Tunables
Defined in `session_manager.c` (slave) and `spi_session.c` (master):
- `SESSION_TIMEOUT_MS` = 5000 - slave watchdog timeout
- `WATCHDOG_PERIOD_MS` = 1000 - slave watchdog tick
- `HEARTBEAT_INTERVAL_MS` = 2000 - master ping period
- `HEARTBEAT_FAIL_LIMIT` = 3 - master fails before declaring lost
- `SPI_SESSION_WINDOW` = 64 - backpressure window (in `spi_protocol.h`)

### Migrated operations

All long-running ops now use the session lifecycle. Each one:
- Returns `spi_session_resp_t { session_id }` on START.
- Has a kill_cb registered with the session manager that calls its `_stop()`.
- Is closed by the master via `SPI_ID_SESSION_STOP { session_id }` (sent
  internally by `spi_session_stop`).
- Is auto-killed by the C5 watchdog if the master stops sending heartbeats
  for 5s (master crash, screen freeze, etc.).

| Op | C5 module | P4 wrapper | Streams? |
|----|-----------|-----------|----------|
| `WIFI_APP_SNIFFER` | wifi_sniffer.c | wifi_sniffer.c | ✓ stream |
| `BT_APP_SNIFFER` | ble_sniffer.c | bluetooth_service.c | ✓ stream |
| `WIFI_APP_DEAUTHER` | wifi_deauther.c | wifi_deauther.c | - |
| `WIFI_APP_FLOOD` | wifi_flood.c | wifi_flood.c | - |
| `WIFI_APP_EVIL_TWIN` | evil_twin.c | evil_twin.c | - |
| `WIFI_APP_BEACON_SPAM` | beacon_spam.c | beacon_spam.c | - |
| `WIFI_APP_DEAUTH_DET` | deauther_detector.c | deauther_detector.c | - |
| `WIFI_APP_PROBE_MON` | probe_monitor.c | probe_monitor.c | - |
| `WIFI_APP_SIGNAL_MON` | signal_monitor.c | signal_monitor.c | - |
| `BT_APP_FLOOD` | ble_connect_flood.c | ble_connect_flood.c | - |
| `BT_APP_SKIMMER` | skimmer_detector.c | skimmer_detector.c | - |
| `BT_APP_TRACKER` | tracker_detector.c | tracker_detector.c | - |
| `BT_APP_SPAM` | (handler pending) | canned_spam.c | - |
| `BT_APP_FLOOD` (L2CAP variant) | ble_connect_flood.c | ble_l2cap_flood.c | - |

The legacy `SPI_ID_WIFI_APP_ATTACK_STOP` and `SPI_ID_BT_APP_STOP` shotgun
commands have been removed entirely. Every op now stops via its own
session via `SPI_ID_SESSION_STOP { session_id }`.

## Hardware Hookup
| Signal | P4 Pin | C5 Pin |
|--------|--------|--------|
| SCLK   | 20     | 6      |
| MOSI   | 21     | 7      |
| MISO   | 22     | 2      |
| CS     | 23     | 10     |
| IRQ    | 2      | 3      |
| RESET  | 48     | EN     |
| BOOT   | 33     | IO0    |
| UART TX| 46     | RX     |
| UART RX| 47     | TX     |

---

# C5

This component transforms the **ESP32-C5** into a high-performance radio co-processor for the ESP32-P4.

## How it Works
The C5 runs a background task (`spi_bridge_task`) that stays in a blocked state waiting for the P4 to send SPI bytes. 

1. **Reception**: When bytes arrive, the task validates the `0xAA` sync byte.
2. **Routing**: It switches on the `Category` byte and routes the payload to the appropriate **Dispatcher** (WiFi or Bluetooth); the `Op` byte selects the operation within that dispatcher.
3. **Execution**: The Dispatcher executes the radio command (e.g., starts a scan).
4. **Notification**: Once the command is done (or results are ready), the C5 raises the **IRQ (Handshake)** pin.
5. **Response**: The P4 sees the IRQ, sends a dummy SPI clock, and the C5 "pushes" the response packet back.

## Memory Mapping (Zero-Copy Results)
The C5 uses a `current_data_source` pointer system. Instead of copying large scan lists into a bridge buffer, the Dispatcher simply points the bridge to the existing result array in memory:
```c
spi_bridge_provide_results(wifi_records, count, sizeof(wifi_ap_record_t));
```
The bridge then serves these items one by one when the P4 asks for them via the generic `SPI_ID_SYSTEM_DATA` command.

## Key Files
- `spi_bridge.c`: Main task and generic data provider logic.
- `wifi_dispatcher.c`: Logic to translate SPI IDs to WiFi driver calls.
- `bt_dispatcher.c`: Logic to translate SPI IDs to NimBLE/BT calls.
- `spi_slave_driver.c`: Low-level peripheral configuration.
- `session_manager.c`: Session lifecycle for long-running operations
  (heartbeat watchdog + backpressure). See "Session Lifecycle" below.

## Command Categories
The `Category` header byte (`spi_cat_t`) selects the subsystem; the `Op` byte
selects the operation within it. Together they pack into `spi_id_t` via
`SPI_CMD(cat, op)`.
- `0x00`: System/Bridge management (ping, status, version, data, stream, log).
- `0x01`: WiFi operations.
- `0x02`: Bluetooth operations.
- `0x03`: LoRa operations.
- `0x04`: Meshtastic phone bridge.
- `0x05`: MeshCore phone bridge.
- `0x06`: Companion host-link BLE relay (routed to `bt_dispatcher`).
- `0xFF`: Session lifecycle (heartbeat, lost, stop).

`SPI_ID_SYSTEM_LOG` (`0x0007`) is a C5→P4 stream that forwards this chip's log
lines to the companion's C5 console.

## Session Lifecycle (Long-Running Operations)

For full design and migration recipe, see the
[P4 README "Session Lifecycle" section](../../../../firmware_p4/components/Service/spi_bridge/README.md#session-lifecycle-long-running-operations).
The two sides share `spi_protocol.h` so the wire format is identical.

### Slave responsibilities (this side)

The `session_manager` runs a background watchdog that auto-kills sessions
when the master stops sending heartbeats (5s timeout). Each long-running
operation must:

1. Call `session_manager_start(op_id, kill_cb)` from its dispatcher case
   to obtain a `session_id`. The dispatcher returns this id to the master
   inside an `spi_session_resp_t` response payload.
2. Provide a `kill_cb(spi_id_t)` that calls the op's `_stop()` - invoked
   by the watchdog when the master goes quiet, and also when the master
   sends `SPI_ID_SESSION_STOP`.
3. **Streaming ops only**: store the id in the op (e.g. via a
   `_bind_session(uint32_t)` setter) and emit packets via
   `session_manager_try_emit(s_session_id, data, len)` instead of raw
   `spi_bridge_stream_push` - this prefixes meta and applies backpressure.

For non-streaming ops (deauther, flood, evil_twin, beacon_spam, etc.),
the `kill_cb` lives in the dispatcher itself - the op's `.c` file does
not need to know about sessions at all.

References:
- Streaming pattern: `wifi_sniffer.c`, `ble_sniffer.c`.
- Non-streaming pattern: see the `killed_*` static functions plus the
  `open_session()` / `bt_open_session()` helpers in the dispatchers.
