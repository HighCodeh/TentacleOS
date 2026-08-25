# WiFi command reference (host link)

Byte-level request/response contract for **every WiFi command** the companion
app can issue over the host link (USB CDC or BLE). Pair this with:

- [`app-guide.md`](./app-guide.md) - transports, the frame envelope, the security
  handshake, sessions/streams, the generic data pipe. Read it first.
- [`protocol.md`](./protocol.md) - the formal wire contract.
- `firmware_*/components/Service/spi_bridge/include/spi_protocol.h` - the source
  of truth for ids and payload structs.

All multi-byte integers are **little-endian**; structs are `__attribute__((packed))`.
Every command is a `CMD` frame with `category = 0x01` (`SPI_CAT_WIFI`) and the
`op` listed below. Every response is a `RESP` whose payload is
`[status u8][data...]`; `status` is `0` OK, `1` BUSY, `2` ERROR, `3` UNSUPPORTED,
`4` INVALID_ARG. "resp: none" means the data section is empty (just the status).

The device relays these to the C5 radio coprocessor exactly as its own firmware
does, so the ids and payloads here are identical to the internal SPI contract.

---

## 1. Result delivery patterns

Three shapes recur; each command below says which one it uses.

- **Immediate** - the `RESP` data carries the whole answer (e.g. a MAC, a flag).
- **List via data pipe** - the command starts/returns work, then you pull the
  rows with the generic data pipe: `CMD category=0x00 op=0x05`
  (`SPI_ID_SYSTEM_DATA`), payload `[index u16]`.
  - `index = 0xFFFF` -> `RESP` data `[count u16]`.
  - `index = 0 .. count-1` -> `RESP` data = one fixed-size record (layouts in §5).
- **Session + stream** - the `RESP` data is `[session_id u32]`; the device then
  pushes `STREAM` frames until you stop it. Heartbeat every ~2 s with
  `SESSION_HEARTBEAT` and end with `SESSION_STOP` (see [`app-guide.md`](./app-guide.md) §7).

### Scans are asynchronous (non-blocking)

`SCAN` (`0x10`), `APP_SCAN_AP` (`0x20`), `APP_SCAN_CLIENT` (`0x21`) and all
`PORT_SCAN_*` (`0x49`-`0x4C`) run on the C5's async scan runner so the link is
never blocked while they run. They return `OK` (or `BUSY` if a scan is already
running) **immediately**. Poll **`SCAN_STATUS` (`0x50`)** - `RESP` data
`[busy u8]`, `1` while any scan is in flight - and once it reads `0`, fetch the
rows from the data pipe. Only one scan (AP, client, or port) runs at a time.

---

## 2. STA / AP / radio control

| op | name | request payload | response | pattern |
|----|------|-----------------|----------|---------|
| `0x10` | `WIFI_SCAN` | none | none (async) | poll `0x50`, then data pipe -> `wifi_ap_record_t` (raw IDF struct; prefer `0x20`) |
| `0x50` | `WIFI_SCAN_STATUS` | none | `[busy u8]` | immediate |
| `0x11` | `WIFI_CONNECT` | `ssid[32]` then optional `password[..64]` (pad SSID to 32; append the password bytes after) | none | immediate |
| `0x12` | `WIFI_DISCONNECT` | none | none | immediate |
| `0x13` | `WIFI_GET_STA_INFO` | none | `ssid[32]` (connected SSID; `ERROR` if not connected) | immediate |
| `0x14` | `WIFI_SET_AP` | `ssid` (1..32 bytes, not padded) | none | immediate |
| `0x15` | `WIFI_START` | none | none | immediate |
| `0x16` | `WIFI_STOP` | none | none | immediate |
| `0x17` | `WIFI_SAVE_AP_CONFIG` | `spi_wifi_ap_config_t` (§5) | none | immediate |
| `0x18` | `WIFI_SET_ENABLED` | `[enabled u8]` | none | immediate |
| `0x19` | `WIFI_SET_AP_PASSWORD` | `password` (0..64 bytes) | none | immediate |
| `0x1A` | `WIFI_SET_AP_MAX_CONN` | `[max_conn u8]` | none | immediate |
| `0x1B` | `WIFI_SET_AP_IP` | `ip` string (0..15 bytes, e.g. `"192.168.4.1"`) | none | immediate |
| `0x1C` | `WIFI_PROMISC_START` | none | none | immediate |
| `0x1D` | `WIFI_PROMISC_STOP` | none | none | immediate |
| `0x1E` | `WIFI_CH_HOP_START` | none | none | immediate |
| `0x1F` | `WIFI_CH_HOP_STOP` | none | none | immediate |
| `0x4E` | `WIFI_GET_MAC` | optional `[iface u8]` (`0`=STA default, `1`=AP) | `mac[6]` | immediate |
| `0x4F` | `WIFI_GET_IP_INFO` | optional `[iface u8]` (`0`=STA default, `1`=AP) | `spi_wifi_ip_info_t` (§5) | immediate |

`WIFI_CONNECT`: the firmware requires at least 32 bytes (the SSID field). For an
open network send the 32-byte SSID alone; for a secured one append the password
bytes right after (total `32 + passlen`, passlen <= 64).

---

## 3. App scans, attacks, monitors

| op | name | request payload | response | pattern |
|----|------|-----------------|----------|---------|
| `0x20` | `WIFI_APP_SCAN_AP` | none | none (async) | poll `0x50`, data pipe -> `spi_wifi_scan_record_t` (§5) |
| `0x21` | `WIFI_APP_SCAN_CLIENT` | none | none (async) | poll `0x50`, data pipe -> `client_scanner_record_t` (§5) |
| `0x22` | `WIFI_APP_BEACON_SPAM` | optional SSID-list file `path` string (empty = random SSIDs) | `[session_id u32]` | session |
| `0x23` | `WIFI_APP_DEAUTHER` | `bssid[6]` + `client[6]` + `[type u8]` + optional `[channel u8]` (13 bytes min, 14 with channel) | `[session_id u32]` | session |
| `0x24` | `WIFI_APP_FLOOD` | `[type u8]`(`0`=auth,`1`=assoc,`2`=probe) + `bssid[6]` + optional `[channel u8]` (7 min, 8 with channel) | `[session_id u32]` | session |
| `0x25` | `WIFI_APP_SNIFFER` | optional `[type u8][channel u8][monitor u8]` (0 args = RAW, hop all channels) | `[session_id u32]` | session + stream (§4) |
| `0x26` | `WIFI_APP_EVIL_TWIN` | `ssid` (1..32 bytes) | `[session_id u32]` | session |
| `0x27` | `WIFI_APP_DEAUTH_DET` | none | `[session_id u32]` | session + stream |
| `0x28` | `WIFI_APP_PROBE_MON` | none | `[session_id u32]` | session; rows via data pipe -> `probe_monitor_record_t` (§5) |
| `0x29` | `WIFI_APP_SIGNAL_MON` | `bssid[6]` + `[target_channel u8]` (7 bytes) | `[session_id u32]` | session + stream |

`type` for deauth/sniffer frame kinds is the firmware's
`wifi_deauther_frame_type_t` / `wifi_sniffer_type_t`; `0` is the safe default.

---

## 4. Sniffer control, deauth frames, targets

| op | name | request payload | response |
|----|------|-----------------|----------|
| `0x2B` | `WIFI_SNIFFER_SET_SNAPLEN` | `[snaplen u16]` | none |
| `0x2C` | `WIFI_SNIFFER_SET_VERBOSE` | `[verbose u8]` | none |
| `0x2D` | `WIFI_SNIFFER_SAVE_FLASH` | `filename` (<= 95 bytes) | none |
| `0x2E` | `WIFI_SNIFFER_SAVE_SD` | `filename` (<= 95 bytes) | none |
| `0x2F` | `WIFI_SNIFFER_FREE_BUFFER` | none | none |
| `0x30` | `WIFI_SNIFFER_STREAM_SD` | `[type u8][channel u8]` + `filename` | none |
| `0x31` | `WIFI_SNIFFER_CLEAR_PMKID` | none | none |
| `0x32` | `WIFI_SNIFFER_GET_PMKID_BSSID` | none | `bssid[6]` |
| `0x33` | `WIFI_SNIFFER_CLEAR_HANDSHAKE` | none | none |
| `0x34` | `WIFI_SNIFFER_GET_HANDSHAKE_BSSID` | none | `bssid[6]` |
| `0x35` | `WIFI_DEAUTH_STATUS` | none | `[running u8]` |
| `0x36` | `WIFI_DEAUTH_SEND_RAW` | raw 802.11 frame bytes | none |
| `0x37` | `WIFI_ASSOC_REQUEST` | `bssid[6]` + `[channel u8]` + `[ssid_len u8]` + `ssid[ssid_len]` | none |
| `0x38` | `WIFI_DEAUTH_SEND_FRAME` | `bssid[6]` + `[type u8]` + `[channel u8]` | none |
| `0x39` | `WIFI_DEAUTH_SEND_BROADCAST` | `bssid[6]` + `[type u8]` + `[channel u8]` | none |
| `0x3A` | `WIFI_TARGET_SCAN_START` | `bssid[6]` + `[channel u8]` | none; rows via data pipe -> `target_scanner_record_t` (§5) |
| `0x3B` | `WIFI_TARGET_SCAN_STATUS` | none | `[scanning u8][count u16]` |
| `0x3C` | `WIFI_TARGET_SAVE_FLASH` | none | none |
| `0x3D` | `WIFI_TARGET_SAVE_SD` | none | none |
| `0x3E` | `WIFI_TARGET_FREE` | none | none |
| `0x3F` | `WIFI_PROBE_SAVE_FLASH` | none | none |
| `0x40` | `WIFI_PROBE_SAVE_SD` | none | none |
| `0x45` | `WIFI_CLIENT_SAVE_FLASH` | none | none |
| `0x46` | `WIFI_CLIENT_SAVE_SD` | none | none |
| `0x47` | `WIFI_AP_SAVE_FLASH` | none | none |
| `0x48` | `WIFI_AP_SAVE_SD` | none | none |

### Sniffer stream records (`0x25`)

Each `STREAM` frame's payload is `spi_stream_meta_t` `[session_id u32][seq u32]`
followed by a `spi_wifi_sniffer_frame_t` fragment:

```
[rssi i8][channel u8][total_len u16][frag_off u16][frag_len u8][flags u8][data frag_len]
```

A frame larger than one transfer is split into ordered fragments (same
`total_len`, advancing `frag_off`); `flags & 0x01` (`FRAG_MORE`) is set on every
fragment except the last. Reassemble by `frag_off` up to `total_len`, then build
your pcap/pcapng from the 802.11 `data` (use `rssi`/`channel` for the radiotap
header).

## Evil-twin portal template upload

| op | name | request payload | response |
|----|------|-----------------|----------|
| `0x41` | `WIFI_EVIL_TWIN_TEMPLATE` | `[ssid_len u8]` + `ssid[ssid_len]` + `[template_len u8]` + `template_path[template_len]` | none |
| `0x42` | `WIFI_EVIL_TWIN_HAS_PASSWORD` | none | `[has u8]` |
| `0x43` | `WIFI_EVIL_TWIN_GET_PASSWORD` | none | captured password, NUL-terminated |
| `0x44` | `WIFI_EVIL_TWIN_RESET_CAPTURE` | none | none |
| `0xA0` | `WIFI_EVIL_TWIN_TMPL_BEGIN` | `[total_size u16]` | none |
| `0xA1` | `WIFI_EVIL_TWIN_TMPL_CHUNK` | raw HTML chunk bytes | none |

To push a custom portal from the app: `TMPL_BEGIN` with the total size, then a
sequence of `TMPL_CHUNK` frames whose bytes concatenate to that size, then start
the attack with `WIFI_APP_EVIL_TWIN` (`0x26`).

---

## 5. Port scanner (`0x49`-`0x4D`)

The scan runs on the async runner (non-blocking): the command returns `OK`
immediately (or `BUSY`), you poll `SCAN_STATUS` (`0x50`) until `0`, then pull the
results from the data pipe as `spi_port_scan_result_t` (§6). Result count is
capped at **32**; port lists at **64** entries. Requested `max_results` of `0` or
`> 32` is clamped to 32.

| op | name | request payload |
|----|------|-----------------|
| `0x49` | `WIFI_PORT_SCAN_TARGET_RANGE` | `spi_port_scan_range_req_t` (§6) |
| `0x4A` | `WIFI_PORT_SCAN_TARGET_LIST` | `ip[16]` + `[max_results u16][count u16]` + `ports[u16 * count]` |
| `0x4B` | `WIFI_PORT_SCAN_NETWORK` | **range:** `spi_port_scan_network_req_t` (§6). **list:** `start_ip[16]` + `end_ip[16]` + `[max_results u16][count u16]` + `ports[u16 * count]` |
| `0x4C` | `WIFI_PORT_SCAN_CIDR` | **range:** `spi_port_scan_cidr_req_t` (§6). **list:** `base_ip[16]` + `[cidr u8]` + `[max_results u16][count u16]` + `ports[u16 * count]` |
| `0x4D` | `WIFI_PORT_SCAN_STOP` | none (best-effort abort at the next port boundary) |

For `0x4B`/`0x4C` the range vs list variant is chosen by **payload length**: a
payload exactly the size of the range struct (`spi_port_scan_network_req_t` = 40
bytes, `spi_port_scan_cidr_req_t` = 24 bytes) is the range form; any other length
is the packed list form. Avoid sending a network **list** with exactly 2 ports
(that lands on 40 bytes and is read as a range); pad it or use the range form.

Port scanning needs the device joined to a network (STA connected) so it has a
route to the targets. IP range span is capped at 256 hosts per network scan.

---

## 6. Struct layouts

```c
// SPI_ID_WIFI_SAVE_AP_CONFIG request
typedef struct {
  char ssid[32];
  char password[64];
  uint8_t max_conn;
  char ip_addr[16];
  uint8_t enabled;
} spi_wifi_ap_config_t;                 // packed, 114 bytes

// SPI_ID_WIFI_GET_IP_INFO response
typedef struct {
  uint8_t interface;   // 0 = STA, 1 = AP
  uint8_t mac[6];
  uint32_t ip;         // network byte order (as stored by lwIP)
  uint32_t netmask;
  uint32_t gw;
} spi_wifi_ip_info_t;                    // packed, 15 bytes

// APP_SCAN_AP data-pipe record
typedef struct {
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t authmode;    // wifi_auth_mode_t (0=open, 3=wpa2, 6=wpa3, ...)
  uint8_t ssid[33];    // NUL-terminated, sanitized ASCII ('' = hidden)
} spi_wifi_scan_record_t;                // packed, 43 bytes

// APP_SCAN_CLIENT data-pipe record
typedef struct {
  uint8_t bssid[6];
  uint8_t client_mac[6];
  int8_t rssi;
  uint8_t channel;
} client_scanner_record_t;               // 14 bytes

// APP_PROBE_MON data-pipe record
typedef struct {
  uint8_t mac[6];
  int8_t rssi;
  char ssid[33];
  uint32_t last_seen_timestamp;
} probe_monitor_record_t;

// TARGET_SCAN data-pipe record
typedef struct {
  uint8_t client_mac[6];
  int8_t rssi;
} target_scanner_record_t;               // 7 bytes

// Port scan requests
typedef struct {
  char ip[16];
  uint16_t start_port;
  uint16_t end_port;
  uint16_t max_results;
  uint8_t reserved[2];
} spi_port_scan_range_req_t;             // packed, 24 bytes

typedef struct {
  char start_ip[16];
  char end_ip[16];
  uint16_t start_port;
  uint16_t end_port;
  uint16_t max_results;
  uint8_t scan_type;   // 0 = port range
  uint8_t reserved;
} spi_port_scan_network_req_t;           // packed, 40 bytes

typedef struct {
  char base_ip[16];
  uint8_t cidr;
  uint8_t scan_type;   // 0 = port range
  uint16_t start_port;
  uint16_t end_port;
  uint16_t max_results;
} spi_port_scan_cidr_req_t;              // packed, 24 bytes

// Port scan data-pipe record
typedef struct {
  char ip_str[16];
  uint16_t port;
  uint8_t protocol;    // 0 = TCP, 1 = UDP
  uint8_t status;      // 0 = OPEN, 1 = OPEN_FILTERED
  char banner[64];
} spi_port_scan_result_t;                // packed, 84 bytes
```

---

## 7. Worked examples

Frames below show only the `BODY` (`[type][cat][op][payload]`); wrap each in the
envelope + MAC per [`app-guide.md`](./app-guide.md) §2, §5.

**AP scan, then read results**
```
CMD  01 20                        # WIFI_APP_SCAN_AP, no args -> RESP status OK
CMD  01 50                        # WIFI_SCAN_STATUS -> RESP [OK][busy]; repeat until busy==0
CMD  00 05  FF FF                 # SYSTEM_DATA index 0xFFFF -> RESP [OK][count u16]
CMD  00 05  00 00                 # SYSTEM_DATA index 0 -> RESP [OK][spi_wifi_scan_record_t]
...                               # indices 1..count-1
```

**Get station MAC / IP**
```
CMD  01 4E                        # WIFI_GET_MAC (STA) -> RESP [OK][mac 6]
CMD  01 4F  00                    # WIFI_GET_IP_INFO iface=STA -> RESP [OK][spi_wifi_ip_info_t]
```

**Port scan 192.168.1.1, ports 1..1024**
```
# spi_port_scan_range_req_t: ip="192.168.1.1"\0.. , start=1, end=1024, max=32, rsv=0
CMD  01 49  <24-byte spi_port_scan_range_req_t>   -> RESP status OK (async)
CMD  01 50                        # poll until busy==0
CMD  00 05  FF FF                 # count
CMD  00 05  <i>                   # each spi_port_scan_result_t
```

**Sniffer session**
```
CMD  01 25  00 00 00              # RAW, hop all, monitor off -> RESP [OK][session_id u32]
<- STREAM 01 25 [meta][sniffer fragment] ...        # pushed
CMD  FF F0  <session_id u32><last_acked_seq u32>    # heartbeat every ~2 s
CMD  FF F2  <session_id u32>                         # SESSION_STOP to end
```

---

## 8. Implementation status

All commands in this document are handled end-to-end on the current firmware
(P4 relay -> C5 dispatcher). The port scanner (`0x49`-`0x4D`), `WIFI_GET_MAC`
(`0x4E`) and `WIFI_GET_IP_INFO` (`0x4F`) were the last to be wired and are now
non-blocking. Verify against `spi_protocol.h` and the C5
`Service/spi_bridge/wifi_dispatcher.c` if in doubt - those are authoritative.
</content>
