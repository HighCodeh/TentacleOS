# Bluetooth command reference (host link)

Byte-level request/response contract for **every Bluetooth (BLE) command** the
companion app can issue over the host link (USB CDC or BLE). Companion to
[`wifi-commands.md`](./wifi-commands.md); same conventions. Read
[`app-guide.md`](./app-guide.md) first (envelope, handshake, sessions/streams,
data pipe).

All multi-byte integers are **little-endian**; wire structs are
`__attribute__((packed))` unless noted. Every command is a `CMD` frame with
`category = 0x02` (`SPI_CAT_BT`) and the `op` below. Every response is a `RESP`
whose payload is `[status u8][data...]` (`0` OK, `1` BUSY, `2` ERROR,
`3` UNSUPPORTED, `4` INVALID_ARG). "resp: none" = just the status byte.

Source of truth: `spi_protocol.h` and the C5
`Service/spi_bridge/bt_dispatcher.c`.

> BLE radio note: the C5 shares one NimBLE host across this attack suite, the
> host-link BLE transport, Meshtastic and MeshCore - they are mutually exclusive.
> The dispatcher tears down the mesh/host GATT servers when a BT attack command
> needs the radio (`bt_ensure_service_ready`).

---

## 1. Result delivery patterns

Same three shapes as WiFi (see [`wifi-commands.md`](./wifi-commands.md) §1):
**immediate**, **list via data pipe** (`CMD category=0x00 op=0x05`,
`[index u16]`; `0xFFFF` -> `[count u16]`, `0..count-1` -> one record), and
**session + stream** (`RESP` data `[session_id u32]`, then pushed `STREAM`
frames; heartbeat with `SESSION_HEARTBEAT`, end with `SESSION_STOP`).

The BLE **scan** (`0x50`) is asynchronous: it returns `OK`/`BUSY` immediately;
poll **`BT_SCAN_STATUS` (`0x7F`)** - `RESP` data `[busy u8]` - until `0`, then
read the results from the data pipe. The scan shares the C5 async runner with the
WiFi scans, so only one scan runs at a time.

---

## 2. Service & radio control

| op | name | request | response |
|----|------|---------|----------|
| `0x54` | `BT_INIT` | none | none |
| `0x55` | `BT_DEINIT` | none | none |
| `0x56` | `BT_START` | none | none |
| `0x57` | `BT_STOP` | none | none (deinitializes the stack) |
| `0x53` | `BT_GET_INFO` | none | `spi_bt_info_t` (§7) |
| `0x51` | `BT_CONNECT` | `addr[6]` + `[addr_type u8]` | none |
| `0x52` | `BT_DISCONNECT` | none | none (drops all connections) |
| `0x58` | `BT_SET_RANDOM_MAC` | none | none |
| `0x59` | `BT_START_ADV` | none | none |
| `0x5A` | `BT_STOP_ADV` | none | none |
| `0x5B` | `BT_SET_MAX_POWER` | none | none |
| `0x5E` | `BT_GET_ADDR_TYPE` | none | `[addr_type u8]` (own address type) |
| `0x5F` | `BT_SAVE_ANNOUNCE_CFG` | `spi_bt_announce_config_t` (§7) | none |

`addr_type` is the NimBLE own/peer address type (`0` public, `1` random, ...).

---

## 3. Scan & device tracker

| op | name | request | response | pattern |
|----|------|---------|----------|---------|
| `0x50` | `BT_SCAN` | optional `[duration_ms u32]` (default 5000) | none (async) | poll `0x7F`, data pipe -> `bluetooth_service_scan_result_t` (§7) |
| `0x7F` | `BT_SCAN_STATUS` | none | `[busy u8]` | immediate |
| `0x5C` | `BT_TRACKER_START` | `addr[6]` | none | starts RSSI follow of one device |
| `0x5D` | `BT_TRACKER_STOP` | none | none | |

`BT_TRACKER_START` (`0x5C`) follows the RSSI of a **specific** address. It is
distinct from `BT_APP_TRACKER` (`0x65`), which is the AirTag/tracker-**detector**.

---

## 4. Attacks, detectors, GATT

| op | name | request | response | pattern |
|----|------|---------|----------|---------|
| `0x60` | `BT_APP_SCANNER` | none | none | starts the on-device continuous scanner; results are saved to **C5 flash** (`/storage/ble/scanned_devices.json`), **not** exposed to the app - use `BT_SCAN` (`0x50`) for app-retrievable results |
| `0x61` | `BT_APP_SNIFFER` | none | `[session_id u32]` | session + stream -> `spi_ble_sniffer_frame_t` (§7) |
| `0x62` | `BT_APP_SPAM` | `[attack_index u8]` (§6) | `[session_id u32]` | session |
| `0x63` | `BT_APP_FLOOD` | `addr[6]` + `[addr_type u8]` | `[session_id u32]` | session (connect flood) |
| `0x64` | `BT_APP_SKIMMER` | none | `[session_id u32]` | session (skimmer detector) |
| `0x65` | `BT_APP_TRACKER` | none | `[session_id u32]` | session (AirTag/tracker detector) |
| `0x66` | `BT_APP_GATT_EXP` | `addr[6]` + `[addr_type u8]` | none | connects and discovers services/characteristics; progress appears as C5 `LOG` frames and the full tree is written to **C5 flash** (`/storage/ble/gatt_results.json`) |

All of these bring the base BLE service up first (tearing down mesh/host GATT if
needed), so a single `RESP` `BUSY` means another owner holds the radio.

> **Result-retrieval gap (`0x60`, `0x66`).** `BT_APP_SCANNER` and `BT_APP_GATT_EXP`
> persist their results as JSON on the **C5's** flash. The host-link `FILE_*` ops
> are P4-local (they reach only the P4's `/assets`, `/littlefs`, `/sdcard`), so the
> companion app cannot read those C5 files today, and neither result is exposed via
> the data pipe or a stream. For app use: `BT_SCAN` (`0x50`) covers scanning with
> data-pipe results; structured GATT results over the link are future work (would
> need a data-pipe or stream exposure on the C5, or a C5->P4 file relay).

---

## 5. Spam list, HID, status

The spam **name list** is the custom set of advertised names used by the spam
attacks, persisted on the C5.

| op | name | request | response |
|----|------|---------|----------|
| `0x68` | `BT_SPAM_LIST_LOAD` | none | none; then data pipe -> each item is `char[32]` (NUL-terminated) |
| `0x69` | `BT_SPAM_LIST_BEGIN` | `[total u16]` (clamped to 64) | none |
| `0x6A` | `BT_SPAM_LIST_ITEM` | `[index u16]` + `name` (<= 31 bytes) | none |
| `0x6B` | `BT_SPAM_LIST_COMMIT` | none | none (persists the staged list) |

To save a list: `BEGIN(total)`, then one `ITEM` per entry (`index` `0..total-1`),
then `COMMIT`. To read it: `LOAD`, then pull `count` + items from the data pipe.

BLE HID keyboard (BadKB):

| op | name | request | response |
|----|------|---------|----------|
| `0x71` | `BT_HID_INIT` | none | none (starts the HID keyboard GATT service) |
| `0x72` | `BT_HID_DEINIT` | none | none |
| `0x73` | `BT_HID_IS_CONNECTED` | none | `[connected u8]` |
| `0x74` | `BT_HID_SEND_KEY` | `[modifier u8][keycode u8]` | none |

`BT_HID_SEND_KEY` sends one USB-HID usage code with a modifier bitmask (the same
codes BadUSB uses). Send a key then a zero key to release, or drive it as key
events from the app.

Status:

| op | name | request | response |
|----|------|---------|----------|
| `0x70` | `BT_L2CAP_STATUS` | none | `[running u8]` (L2CAP flood running?) |

### Defined but not wired over the host link

`BT_SCREEN_INIT` (`0x6C`), `BT_SCREEN_DEINIT` (`0x6D`), `BT_SCREEN_IS_ACTIVE`
(`0x6E`), `BT_SCREEN_SEND_PARTIAL` (`0x6F`) back a device->BLE screen-mirror
feature that has no companion-app client; they are not dispatched on the C5 and
return `ERROR`. Do not use them from the app.

---

## 6. Spam attack indices (`BT_APP_SPAM` `0x62`)

| index | attack |
|-------|--------|
| `0` | Apple Juice (Apple proximity pairing) |
| `1` | Sour Apple |
| `2` | Swift Pair (Windows) |
| `3` | Samsung |
| `4` | Android (Fast Pair) |
| `5` | Tutti Frutti (all of the above, rotating) |

---

## 7. Struct layouts

```c
// BT_GET_INFO response
typedef struct {
  uint8_t mac[6];
  uint8_t running;
  uint8_t initialized;
  uint16_t connected_count;
} spi_bt_info_t;                         // packed, 10 bytes

// BT_SAVE_ANNOUNCE_CFG request
typedef struct {
  char name[32];
  uint8_t max_conn;
} spi_bt_announce_config_t;              // packed, 33 bytes

// BT_APP_SNIFFER stream record (after the [session_id u32][seq u32] meta)
typedef struct {
  uint8_t addr[6];
  uint8_t addr_type;
  int8_t rssi;
  uint8_t len;
  uint8_t data[31];                      // raw advertisement bytes, `len` valid
} spi_ble_sniffer_frame_t;               // packed, 40 bytes

// BT_SCAN / BT_APP_SCANNER data-pipe record. NOTE: this is the raw C5 struct
// (NOT packed) served directly over the pipe, so it carries native padding:
// name[0..31], uuids[32..159], addr[160..165], addr_type[166], PAD[167],
// rssi is a 4-byte little-endian int at offset 168. Total 172 bytes.
typedef struct {
  char name[32];      // NUL-terminated device name ('' if none)
  char uuids[128];    // NUL-terminated, space/comma-listed advertised UUIDs
  uint8_t addr[6];
  uint8_t addr_type;
  int rssi;           // 4-byte int, dBm
} bluetooth_service_scan_result_t;       // 172 bytes incl. padding
```

Spam-list items on the data pipe (`0x68`) are fixed `char[32]` records,
NUL-terminated.

---

## 8. Worked examples

BODY only (`[type][cat][op][payload]`); wrap in the envelope + MAC per
[`app-guide.md`](./app-guide.md).

**Scan, then read results**
```
CMD  02 50  <u32 duration, optional>   # BT_SCAN -> RESP status OK (async)
CMD  02 7F                              # BT_SCAN_STATUS -> [OK][busy]; repeat until 0
CMD  00 05  FF FF                       # count
CMD  00 05  00 00                       # first bluetooth_service_scan_result_t
```

**Info / random MAC / advertise**
```
CMD  02 53                              # BT_GET_INFO -> [OK][spi_bt_info_t]
CMD  02 58                              # BT_SET_RANDOM_MAC
CMD  02 59                              # BT_START_ADV
```

**Spam (Apple Juice) as a session**
```
CMD  02 62  00                          # BT_APP_SPAM attack 0 -> [OK][session_id u32]
CMD  FF F0  <sid u32><0 u32>            # heartbeat every ~2 s
CMD  FF F2  <sid u32>                   # SESSION_STOP
```

**Save a spam name list of 2**
```
CMD  02 69  02 00                       # BEGIN total=2
CMD  02 6A  00 00 "iPhone"              # ITEM index 0
CMD  02 6A  01 00 "Galaxy"              # ITEM index 1
CMD  02 6B                              # COMMIT
```

**BLE HID: type 'a' then release**
```
CMD  02 71                              # HID_INIT
CMD  02 73                              # HID_IS_CONNECTED -> [OK][connected]
CMD  02 74  00 04                       # SEND_KEY modifier=0 keycode=0x04 ('a')
CMD  02 74  00 00                       # SEND_KEY release
```

---

## 9. Implementation status

All commands here are handled end-to-end (P4 relay -> C5 `bt_dispatcher.c`)
except the `BT_SCREEN_*` group (§5), which has no companion client and is not
wired. `BT_GET_INFO` was corrected to return the full `spi_bt_info_t`, and the
following were wired 2026-08-24: `SET_RANDOM_MAC`, `START_ADV`, `STOP_ADV`,
`SET_MAX_POWER`, `GET_ADDR_TYPE`, `TRACKER_START/STOP`, `SAVE_ANNOUNCE_CFG`,
`APP_GATT_EXP`, `SPAM_LIST_*`, `HID_*`, and `L2CAP_STATUS`. Verify against
`spi_protocol.h` and `bt_dispatcher.c` if in doubt.
</content>
