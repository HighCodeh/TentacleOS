# System command reference (host link)

Byte-level contract for the **System category** (`category = 0x00`,
`SPI_CAT_SYSTEM`): housekeeping, device state, settings, file transfer, the data
pipe, and C5 firmware update. Companion to [`wifi-commands.md`](./wifi-commands.md)
and [`bluetooth-commands.md`](./bluetooth-commands.md); read
[`app-guide.md`](./app-guide.md) first.

All multi-byte integers are **little-endian**; wire structs are packed unless
noted. Every command is a `CMD` frame with `category = 0x00` and the `op` below;
every response is a `RESP` whose payload is `[status u8][data...]`.

System ops split three ways by where they run:
- **C5-relayed** - forwarded to the C5 coprocessor (ping, status, version, reboot,
  info, OTA, the data pipe).
- **P4-local** - handled on the P4, never relayed (file ops, device state,
  settings, console exec). These are documented in [`app-guide.md`](./app-guide.md)
  §9-§10; summarized here so the whole `0x00` id space is in one place.
- **Internal (not for the app)** - P4<->C5 management the companion app should not
  send (`LOG`, `STREAM`, `POWER_STATE`, `ENTER_DOWNLOAD`, `PROTO_VERSION`).

---

## 1. Housekeeping (C5-relayed)

| op | name | request | response |
|----|------|---------|----------|
| `0x01` | `SYSTEM_PING` | none | none (status OK = C5 alive) |
| `0x02` | `SYSTEM_STATUS` | none | `spi_system_status_t` (§4) |
| `0x03` | `SYSTEM_REBOOT` | none | none (reboots the **C5** coprocessor, not the P4) |
| `0x04` | `SYSTEM_VERSION` | none | C5 firmware version string (UTF-8, not NUL-padded) |
| `0x0C` | `SYSTEM_INFO` | none | `spi_sys_info_t` (§4) - C5 chip model/rev/MAC/free heap |
| `0x05` | `SYSTEM_DATA` | `[index u16]` | list row (see §2) |

`SYSTEM_VERSION` returns the C5 version; the P4 version comes in the device-state
frame (`0x46`). For a device-wide reboot, drive it via `CONSOLE_EXEC` (there is no
P4 reboot op in this category).

## 2. Data pipe (`SYSTEM_DATA`, `0x05`)

The generic list-result channel used by scans and other list-producing commands
across every category. Request `[index u16]`:

- `index = 0xFFFF` (`SPI_DATA_INDEX_COUNT`) -> `RESP` data `[count u16]`.
- `index = 0 .. count-1` -> `RESP` data = one fixed-size record; the record layout
  is defined by whichever command populated the pipe (see that command's doc).
- `index = 0xEEEE` (`SPI_DATA_INDEX_STATS`) -> command-specific stats blob.
- `index = 0xDDDD` (`SPI_DATA_INDEX_DEAUTH_COUNT`) -> deauth counter (u32).

The pipe holds the most recent list; read it before starting another
list-producing command.

---

## 3. Device state, settings, files, console (P4-local)

Fully specified in [`app-guide.md`](./app-guide.md) §9 (files) and §10 (device
state, settings, console exec). IDs, for the shared id space:

| op | name | summary |
|----|------|---------|
| `0x46` | `SYSTEM_DEVICE_STATE` | `[battery_pct u8][charging u8][app_connected u8][p4_len u8][p4_ver][c5_len u8][c5_ver]` |
| `0x47` | `SYSTEM_CONSOLE_EXEC` | run a raw console line; stdout returns as `LOG` frames (gated by the console-exec toggle) |
| `0x48` | `SYSTEM_GET_SETTINGS` | `[console_exec u8][log_over_ble u8]` |
| `0x49` | `SYSTEM_SET_SETTINGS` | write `[console_exec u8][log_over_ble u8]` |
| `0x40`-`0x45` | `FILE_LIST/STAT/READ/WRITE/DELETE/MKDIR` | file transfer over `/assets`, `/littlefs`, `/sdcard` |
| `0x4B` | `SYSTEM_CONFIG_GET` | read a settings section: req `[section u8]` -> the section struct (below) |
| `0x4C` | `SYSTEM_SET_THEME` | switch the active UI theme by name: req `name` (<= 31 bytes), applied live |
| `0x4D` | `SYSTEM_CONFIG_SET` | write a settings section: req `[section u8][section struct]`, applied live |

These are handled on the P4 and never relayed to the C5.

### Settings sections (`CONFIG_GET` / `CONFIG_SET`)

`section` (payload byte 0, `spi_config_section_t`) selects one config block, each
mapping to a `tos_config` global and a packed struct. `CONFIG_GET` request is
`[section u8]` and returns the struct; `CONFIG_SET` request is `[section u8]`
then the struct, applied live and persisted.

| section | value | struct | fields |
|---------|-------|--------|--------|
| `DISPLAY` | `0` | `spi_cfg_display_t` | `[brightness u8][rotation u8][auto_lock_seconds u16][auto_dim u8]` |
| `SOUND` | `1` | `spi_cfg_sound_t` | `[volume u8][vibration u8]` |
| `CONNECTIVITY` | `2` | `spi_cfg_connectivity_t` | `[wifi_enabled u8][ble_enabled u8]` |
| `LED` | `3` | `spi_cfg_led_t` | `[brightness u8]` |

`brightness`/`volume` are 0-100; `rotation` is `1` portrait / `2` landscape;
`auto_lock_seconds` `0` = never; the booleans are `0`/`1`. `SYSTEM_SET_THEME`
takes a theme name (a built-in like `default`/`cyber_blue`, or an SD theme under
`/sdcard/themes/<name>/`) and restyles the live screen.

---

## 4. C5 firmware update (OTA, C5-relayed, advanced)

Push a new C5 `.bin` from the app. Flow: `OTA_BEGIN` (C5 erases), stream the image
with repeated `OTA_DATA` chunks, poll `OTA_STATUS`; the C5 validates and reboots
into the new image when `bytes_written` reaches `size`.

| op | name | request | response |
|----|------|---------|----------|
| `0x09` | `SYSTEM_OTA_BEGIN` | `spi_ota_begin_t` `[size u32][transport u8]` (transport `0` = SPI/OTA_DATA) | none |
| `0x0B` | `SYSTEM_OTA_DATA` | one firmware chunk (raw bytes, <= one frame) | none (written sequentially) |
| `0x0A` | `SYSTEM_OTA_STATUS` | none | `spi_ota_status_t` `[state u8][bytes_written u32]` |

`state` (`spi_ota_state_t`): `0` IDLE, `1` ERASING, `2` READY, `3` RECEIVING,
`4` DONE, `5` ERROR. Only `transport = 0` (SPI) is usable over the companion link;
`transport = 1` (UART) is a wired-recovery path.

---

## 5. Struct layouts

```c
// SYSTEM_STATUS response
typedef struct {
  uint8_t wifi_active;
  uint8_t wifi_connected;
  uint8_t bt_running;
  uint8_t bt_initialized;
} spi_system_status_t;                   // packed, 4 bytes

// SYSTEM_INFO response (C5 identity)
typedef struct {
  uint8_t chip_model;    // esp_chip_model_t
  uint16_t chip_revision;// major*100 + minor
  uint8_t mac[6];        // base MAC
  uint32_t free_heap;    // bytes
} spi_sys_info_t;                        // packed, 13 bytes

// SYSTEM_OTA_BEGIN request
typedef struct {
  uint32_t size;         // image size in bytes
  uint8_t transport;     // 0 = SPI (OTA_DATA chunks), 1 = UART
} spi_ota_begin_t;                       // packed, 5 bytes

// SYSTEM_OTA_STATUS response
typedef struct {
  uint8_t state;         // spi_ota_state_t
  uint32_t bytes_written;
} spi_ota_status_t;                      // packed, 5 bytes
```

---

## 6. Internal ops - do not send from the app

| op | name | why |
|----|------|-----|
| `0x06` | `SYSTEM_STREAM` | internal stream-batch envelope, not a command |
| `0x07` | `SYSTEM_LOG` | C5->P4 log stream; you receive these as `LOG` frames, you don't request them |
| `0x08` | `SYSTEM_ENTER_DOWNLOAD` | reboots the C5 into ROM serial-flash mode (wired recovery) |
| `0x0D` | `SYSTEM_PROTO_VERSION` | P4<->C5 SPI protocol version check (`SPI_PROTOCOL_VERSION`, currently `6`); the app uses the host-link `VER`, not this |
| `0x4A` | `SYSTEM_POWER_STATE` | P4->C5 power hint so the C5 can drop its radio when the P4 sleeps |
| `0x50`-`0x53` | `SYSTEM_CFG_HASH`/`BEGIN`/`CHUNK`/`COMMIT` | P4->C5 config cache: the P4 pushes its master config files to the C5, hash-gated (CRC32) so unchanged files are never re-sent |

Sending these from the companion app is unsupported and can reboot the
coprocessor or disrupt the link.

---

## 7. Implementation status

All ops here are handled today (C5 `spi_bridge.c` for the relayed ones; P4
host-link modules for the P4-local ones). Verify against `spi_protocol.h` and the
C5 `Service/spi_bridge/spi_bridge.c` if in doubt.
</content>
