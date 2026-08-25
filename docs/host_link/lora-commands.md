# LoRa command reference (host link)

Byte-level contract for the two **P4-native LoRa categories**, handled on the P4
and never relayed to the C5. Read [`app-guide.md`](./app-guide.md) first.
Little-endian; responses are `[status u8][data...]`.

- **LoRa radio config** - `category = 0x0D` (`SPI_CAT_LORACFG`): read/write the
  stored SX1262 radio parameters (`g_config_lora`), applied live when a MeshCore
  session is running.
- **LoRa broadcast chat** - `category = 0x0E` (`SPI_CAT_LORACHAT`): join the
  MeshCore group, send text, and receive messages as `STREAM` frames.

---

## 1. Radio config (`category = 0x0D`)

| op | name | request | response |
|----|------|---------|----------|
| `0x10` | `LORACFG_GET` | none | `[freq u32][sf u8][bw u32][tx_power i8][sync_word u8][enabled u8]` (12 B) |
| `0x11` | `LORACFG_SET` | `[freq u32][sf u8][bw u32][tx_power i8][enabled u8]` (11 B) | none |

`freq` in Hz, `sf` spreading factor (7-12), `bw` bandwidth in Hz, `tx_power` in
dBm (signed), `sync_word` the network sync byte (returned by `GET`, not set by
`SET`). `SET` persists to the LoRa config and, if a MeshCore session is active,
retunes the radio live (coding rate 4/5).

---

## 2. Broadcast chat (`category = 0x0E`)

| op | name | request | response |
|----|------|---------|----------|
| `0x10` | `LORACHAT_START` | optional node `name` (<= 23 bytes) | `[local_node_id u32]` (`BUSY` if a session is already up) |
| `0x11` | `LORACHAT_STOP` | none | none |
| `0x12` | `LORACHAT_SEND` | UTF-8 `text` (<= 191 bytes) | none |

`0x60` (`LORACHAT_RX`) is the op tag on pushed `STREAM` frames, not a command.

`LORACHAT_START` brings up a MeshCore session, sets the node name (if given), and
starts a poll task that pushes received messages. `local_node_id` is the low 4
bytes of the node's public key. `STOP` stops the companion relay only (it does
not tear down an on-device radio session).

### RX stream (`STREAM` cat `0x0E` op `0x60`)

```
[type u8][sender_id u32][rssi i16][snr i8][name_len u8][name][text]
```

`type` is `0` (message). `sender_id` is a hash of the sender name; `rssi`/`snr`
are the sender's last-known values from the node list (`0` for an unknown sender
- MeshCore group text carries no per-sender identity). `name` is `name_len`
bytes; the rest of the frame is the message `text` (no length prefix).

---

Source of truth: `spi_protocol.h`, P4 `Service/host_link/host_link_lora.c`
(driving `lora_session` + `meshcore`).
