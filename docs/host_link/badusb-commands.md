# BadUSB command reference (host link)

Byte-level contract for the **BadUSB category** (`category = 0x0C`,
`SPI_CAT_BADUSB`). P4-native (USB-HID in the TinyUSB composite), handled on the
P4, never relayed to the C5. Read [`app-guide.md`](./app-guide.md) first.
Responses are `[status u8]`.

| op | name | request | response |
|----|------|---------|----------|
| `0x10` | `BADUSB_RUN` | `[target u8]? path` (optional transport byte, then the Ducky script path) | none (`BUSY` if a run is already in progress) |
| `0x11` | `BADUSB_ABORT` | none | none |
| `0x12` | `BADUSB_STATUS` | none | `[running u8]` (`1` running, `0` idle) |

`BADUSB_RUN` spawns the run on its own task (core RADIO): it waits for the target
host to connect, then executes the Ducky script (US layout). A `path` starting
with `/assets/` runs a bundled script from flash; anything else is read from the
SD card. Only one run at a time (`BUSY` otherwise); `BADUSB_ABORT` requests a
stop, `BADUSB_STATUS` polls whether a run is still active.

**Transport (optional leading byte):** the request may begin with a single
target byte — `0x00` = USB HID (TinyUSB composite), `0x01` = Bluetooth HID (HOGP
on the C5) — followed by the path. The byte is optional and backward-compatible:
real paths always start with `/` (`0x2F`), so a payload that begins with the raw
path (no target byte) is run over USB, exactly as before. Over Bluetooth the run
waits for a host to pair before typing.

Source of truth: `spi_protocol.h`, P4 `Service/host_link/host_link_badusb.c`
(driving `bad_usb` + `ducky_parser`).
