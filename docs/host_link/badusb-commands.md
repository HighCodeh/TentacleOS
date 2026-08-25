# BadUSB command reference (host link)

Byte-level contract for the **BadUSB category** (`category = 0x0C`,
`SPI_CAT_BADUSB`). P4-native (USB-HID in the TinyUSB composite), handled on the
P4, never relayed to the C5. Read [`app-guide.md`](./app-guide.md) first.
Responses are `[status u8]`.

| op | name | request | response |
|----|------|---------|----------|
| `0x10` | `BADUSB_RUN` | `path` (Ducky script; `/assets/...` for a bundled flash script, else an SD path) | none (`BUSY` if a run is already in progress) |
| `0x11` | `BADUSB_ABORT` | none | none |
| `0x12` | `BADUSB_STATUS` | none | `[running u8]` (`1` running, `0` idle) |

`BADUSB_RUN` spawns the run on its own task (core RADIO): it waits for the USB
HID to enumerate on the host, then executes the Ducky script (US layout, USB
output). A `path` starting with `/assets/` runs a bundled script from flash;
anything else is read from the SD card. Only one run at a time (`BUSY`
otherwise); `BADUSB_ABORT` requests a stop, `BADUSB_STATUS` polls whether a run
is still active.

The HID shares the same native USB as the host link (both live in the TinyUSB
composite), so BadUSB is only available over the USB transport.

Source of truth: `spi_protocol.h`, P4 `Service/host_link/host_link_badusb.c`
(driving `bad_usb` + `ducky_parser`).
