# IR command reference (host link)

Byte-level contract for the **Infrared category** (`category = 0x08`,
`SPI_CAT_IR`): transmit decoded/raw IR, stream captures, and the IR-LED DC torch.
Companion to the other command docs; read [`app-guide.md`](./app-guide.md) first.

All multi-byte integers are **little-endian**. Every command is a `CMD` frame with
`category = 0x08` and the `op` below; every response is a `RESP` whose payload is
`[status u8][data...]`.

IR is a **P4-native** subsystem (the SX-class IR LED/receiver hangs off the P4's
RMT peripheral). These commands are handled **on the P4** and never relayed to the
C5 - the ids live in `spi_protocol.h` on both chips only so the id space stays
shared and collision-free.

Source of truth: `spi_protocol.h` and the P4
`Service/host_link/host_link_ir.c` (driving `Service/ir`).

---

## 1. Commands

| op | name | request | response |
|----|------|---------|----------|
| `0x01` | `IR_TX` | `[protocol u8][address u32][command u32][repeat u8]` (10 bytes) | none |
| `0x02` | `IR_TX_RAW` | `[carrier_hz u32][count u16]` + `count` × `rmt_symbol_word_t` (4 bytes each) | none |
| `0x03` | `IR_RX_START` | none | none (starts streaming capture) |
| `0x04` | `IR_RX_STOP` | none | none |
| `0x06` | `IR_TORCH_ON` | none | none (IR LED constant-on) |
| `0x07` | `IR_TORCH_OFF` | none | none |

`0x05` (`IR_RX_FRAME`) is not a command - it is the **op tag on the STREAM frames**
the device pushes while capturing (see §3).

Send is synchronous and fast: the P4 opens the RMT TX channel, sends, and releases
it per call. `IR_TX_RAW` may carry more than one SPI frame's worth of symbols
(it is P4-local, so it is not bound by the 255-byte relay cap); it is capped at
**512 symbols**.

---

## 2. Protocols (`protocol` field)

`ir_protocol_t` values for `IR_TX` and in the RX stream:

| value | protocol | value | protocol |
|------:|----------|------:|----------|
| 0 | UNKNOWN | 7 | JVC |
| 1 | NEC | 8 | DENON |
| 2 | SAMSUNG | 9 | PANASONIC |
| 3 | RC6 | 10 | RCA |
| 4 | RC5 | 11 | PIONEER |
| 5 | SONY | 12 | NEC42 |
| 6 | LG | | |

`address`/`command` are the decoded protocol fields; `repeat = 1` marks a repeat
frame. For anything outside these 12 protocols, capture and resend via `IR_TX_RAW`.

### `rmt_symbol_word_t` (for `IR_TX_RAW`)

Each symbol is 4 bytes, a little-endian `u32` packing two level/duration pairs
(ESP-IDF RMT format):

```
bits  0..14  duration0 (RMT ticks)
bit     15   level0    (0/1)
bits 16..30  duration1
bit     31   level1
```

Send the symbols exactly as captured (e.g. from a prior `IR_RX` at the same
carrier). `carrier_hz` is the modulation carrier (typically 38000).

---

## 3. Streaming capture (`IR_RX_START` / `IR_RX_STOP`)

Capture is **non-blocking**: `IR_RX_START` returns `OK` immediately and a P4
background task decodes incoming signals, pushing each as a `STREAM` frame; the
link is never stalled while listening. `IR_RX_START` returns `BUSY` if a capture
is already running.

Each `STREAM` frame: `BODY = [type=0x03][category=0x08][op=0x05][decoded signal]`
where the decoded signal is the same 10-byte layout as `IR_TX`:

```
[protocol u8][address u32][command u32][repeat u8]
```

Stop with `IR_RX_STOP`. The capture is also torn down automatically if the
companion session drops, so it can never keep streaming into a dead link. Unlike
the radio sniffers, IR capture is **session-less** - there is no `session_id` and
no heartbeat; just start, read the stream, and stop.

---

## 4. Worked examples

BODY only (`[type][cat][op][payload]`); wrap in the envelope + MAC per
[`app-guide.md`](./app-guide.md).

**Send an NEC signal (addr 0x04, cmd 0x08)**
```
CMD  08 01  01  04 00 00 00  08 00 00 00  00     # IR_TX NEC, no repeat -> RESP OK
```

**Capture remotes, then stop**
```
CMD  08 03                                       # IR_RX_START -> RESP OK
<- STREAM 08 05 [protocol][address u32][command u32][repeat]   # per button press
CMD  08 04                                       # IR_RX_STOP
```

**Torch on/off**
```
CMD  08 06                                       # IR_TORCH_ON
CMD  08 07                                       # IR_TORCH_OFF
```

Tester: `python tools/host_link/cli.py ir tx 1 0x04 0x08`, `... ir rx`,
`... ir torch_on`.

---

## 5. Implementation status

All IR commands are handled end-to-end on the P4 (`host_link_ir.c` -> `Service/ir`)
and build-validated. TX (decoded + raw), streaming RX, and the torch are wired;
RX auto-stops on disconnect. Not yet hardware-tested. Verify against
`spi_protocol.h` and `host_link_ir.c` if in doubt.
</content>
