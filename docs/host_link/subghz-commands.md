# Sub-GHz command reference (host link)

Byte-level contract for the **Sub-GHz category** (`category = 0x09`,
`SPI_CAT_SUBGHZ`). P4-native (CC1101), handled on the P4, never relayed to the C5.
Read [`app-guide.md`](./app-guide.md) first. Little-endian; responses are
`[status u8][data...]`.

Because Sub-GHz is P4-local, there is **no data pipe** here: list results come
back inline in the `RESP`, and captures/spectrum are pushed as `STREAM` frames.

---

## 1. Commands

| op | name | request | response |
|----|------|---------|----------|
| `0x01` | `SUBGHZ_RX_START` | `[mode u8][preset u8][freq u32]` | none (starts streaming) |
| `0x03` | `SUBGHZ_RX_STOP` | none | none |
| `0x04` | `SUBGHZ_TX_RAW` | `[count u16]` + `count` × `int32 timing` (us, sign = level) | none |
| `0x05` | `SUBGHZ_REPLAY` | `name` (saved capture, no `.sub`) | none |
| `0x06` | `SUBGHZ_SPECTRUM_START` | `[center_freq u32][span_hz u32]` | none (starts streaming) |
| `0x08` | `SUBGHZ_SPECTRUM_STOP` | none | none |
| `0x09` | `SUBGHZ_LIST` | none | `[count u8]` + rows (§3) |
| `0x0A` | `SUBGHZ_DELETE` | `name` | none |

`0x02` (`SUBGHZ_RX_FRAME`) and `0x07` (`SUBGHZ_SPECTRUM_LINE`) are the op tags on
the pushed `STREAM` frames, not commands.

`mode`: `0` SCAN (decode known protocols), `1` RAW (capture raw). `preset` is
`cc1101_preset_t`: `0` IDLE, `1` OOK_270K, `2` OOK_650K, `3` 2FSK_2K, `4` 2FSK_47K,
`5` 2FSK_95K, `6` OOK_800K. `freq` in Hz (e.g. 433920000). `TX_RAW` is capped at
512 timings; RX and spectrum are mutually exclusive (a second start returns
`BUSY`) and auto-stop if the companion session drops.

---

## 2. Streams

**RX capture** (`STREAM` cat `0x09` op `0x02`):
```
[seq u32][decoded u8][freq u32][bit_count u8][btn u8][serial u32][raw_value u32][name_len u8][protocol_name]
```
`decoded = 0` means a raw capture (no protocol match).

**Spectrum line** (`STREAM` cat `0x09` op `0x07`):
```
[center_freq u32][span_hz u32][start_freq u32][step_hz u32][count u8][int8 dBm × count]
```
`count` is 80 bins; bin `i` is at `start_freq + i*step_hz`, value in dBm (signed).

---

## 3. `SUBGHZ_LIST` rows

`RESP` data = `[count u8]` then `count` rows, each fixed 68 bytes:
```
[name char[40]][protocol char[24]][frequency u32]
```
Names and protocol labels are NUL-padded. Capped to what fits one response
(~15 rows).

---

## 4. Console + tester

Console: `subghz rx <freq> [seconds] [raw]` / `subghz spectrum <center> <span> [lines]`
/ `subghz list` / `subghz replay <name>` / `subghz delete <name>`.
Tester: `cli.py subghz rx 433920000` (Ctrl-C stops), `cli.py subghz list`.

Source of truth: `spi_protocol.h`, P4 `Service/host_link/host_link_subghz.c`
(driving `Applications/SubGhz`).
</content>
