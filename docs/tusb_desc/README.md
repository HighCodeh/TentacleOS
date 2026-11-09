<!--
Copyright (c) 2025 HIGH CODE LLC

TentacleOS is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

TentacleOS is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
-->

# TinyUSB Descriptors (HID + CDC + optional MSC Composite)

This component defines the USB descriptors required to enumerate the ESP32-P4 as a USB composite device and provides the initialization routine for the TinyUSB driver. The composite is HID (Keyboard + Mouse, for BadUSB) plus a CDC-ACM interface (the companion host link), with an optional mass-storage (MSC) interface exposed only while the SD card is handed to the host.

## Overview

- **Location:** `components/Drivers/tusb_desc/`
- **Header:** `include/tusb_desc.h`
- **Dependencies:** `tinyusb`, `esp_tinyusb`, `driver/gpio`
- **USB Port:** High Speed (ESP32-P4)

## USB Descriptors

### Device Descriptor

| Field | Value |
|-------|-------|
| USB Version | 2.0 (`bcdUSB` `0x0200`) |
| Vendor ID | `0xCAFE` |
| Product ID | `0x4001` (base), `0x4003` (Storage) |
| Device Class | Miscellaneous / Common / IAD (`TUSB_CLASS_MISC`) - required so the host groups the CDC interfaces |
| Configurations | 1 |

### Configuration Descriptor

The config is runtime-selected. `tud_descriptor_configuration_cb` picks a variant
by both link speed and whether MSC is currently exposed, all built from a single
`HID_CDC_BLOCK` macro (optionally followed by an `MSC_INTERFACE`).

| Field | Value |
|-------|-------|
| Interfaces | 3 (HID + CDC comm + CDC data), or 4 with MSC (`TUSB_DESC_ITF_NUM_TOTAL`) |
| Max Power | 100 mA |
| Attributes | Remote Wakeup |

The CDC bulk (data) endpoint size is speed-dependent: USB requires exactly 512
bytes at High Speed and 64 at Full Speed. The P4 USB is High Speed, but two
descriptor variants are built per mode (HS/FS) and the matching one is served so
`tu_edpt_validate` accepts the config. A wrong size fails `SET_CONFIGURATION`,
which would also take the HID keyboard down.

### Interfaces and Endpoints

| Interface | Number | Endpoint(s) |
|-----------|--------|-------------|
| HID (keyboard + mouse) | 0 | IN `0x81` |
| CDC-ACM comm | 1 | notification IN `0x82` |
| CDC-ACM data | 2 | OUT `0x03`, IN `0x83` |
| MSC (optional) | 3 | OUT `0x04`, IN `0x84` |

### HID Report Descriptor

Single HID interface with two reports using Report IDs:

| Report ID | Type | Usage |
|-----------|------|-------|
| 1 | Keyboard | Generic Desktop Keyboard |
| 2 | Mouse | Generic Desktop Mouse (buttons + XY + wheel) |

### String Descriptors

| Index | Value |
|-------|-------|
| 0 | Language ID (English US) |
| 1 | Manufacturer: "Highboy" |
| 2 | Product: "Highboy" |
| 3 | Serial: factory MAC, twelve hexadecimal digits |
| 4 | CDC interface: "Highboy Companion" |
| 5 | MSC interface: "Highboy Storage" (only present when MSC is compiled in) |

## API Reference

### `busb_init`
```c
esp_err_t busb_init(void);
```
Initializes the TinyUSB driver with the defined descriptors.
1. Installs the GPIO ISR service (required for ESP32-P4 High Speed USB).
2. Configures device, configuration, and HID report descriptors.
3. Installs the TinyUSB driver on the High Speed port.

Must be called before any HID report transmission. It does **not** touch the
USB-C data mux (see below) - installing the driver and routing the connector are
separate steps. The HID (BadUSB) and CDC (companion) sides share one TinyUSB
install: whichever calls first brings the composite up, later calls are no-ops.

### `busb_set_msc_exposed`
```c
void busb_set_msc_exposed(bool exposed);
```
Advertises (or hides) the mass-storage interface on the composite at runtime.

MSC is off by default, so plain native-USB bring-ups enumerate as HID + CDC only.
`tud_descriptor_configuration_cb` reads `s_msc_exposed` at request time and serves
the 4-interface (MSC) config variant only while the flag is set. Behavior:

- If TinyUSB is not up yet, the call just latches the flag so the first
  enumeration advertises the right layout.
- If it is already up, the device detaches (`tud_disconnect`), waits
  `BUSB_REENUM_DELAY_MS` (100 ms) so the host notices, swaps the advertised
  config, and re-attaches (`tud_connect`) so the host re-reads the descriptor.

**Rationale:** an MSC LUN that is advertised but not backed by an initialized
storage handle crashes the TinyUSB task on the host's first SCSI command. MSC is
therefore exposed only while the SD is actually handed to the host (mass-storage
mode) and hidden again on exit. Only compiled in when `CFG_TUD_MSC` is set; a
no-op otherwise.

## V2.03 USB routing

Descriptors and the native High Speed PHY remain unchanged. GPIO24/25 belong
to the separate J15 debug connector; they are not the muxed native data pair.
J12 routes through U13 (TPG3USB221YP): P4 GPIO19 low selects CP2105, high selects
native P4. C5 GPIO4 owns active-low `/OE`. Boot preloads GPIO19 low and C5 GPIO4
low, preserving CP2105 access without an operational SPI link. P4 also preloads
charger OTG GPIO33 low.

The CP2105 ECI channel carries P4 UART0 (TX38/RX37); SCI carries C5 UART.
Identify channels from USB interface metadata or boot logs, not a fixed OS port
name or discovery order. This does not reinstate a direct P4-to-C5 UART flasher.

`usb_mux_bind_controls()` injects the board interface once before
`usb_mux_init()`. The mux worker and blocking worker API serialize the full
sequence: prepare TinyUSB, confirm `/OE` disabled, wait 100 ms for detach, check
session continuity, select GPIO19, then confirm `/OE` enabled in the same epoch.
Only then does `usb_mux_is_native()` report true; enumeration still requires a
host/cable. That getter also checks current remote validity and epoch.

- `usb_mux_request_native(bool)` is the UI-safe asynchronous API. ESP_OK means
  accepted. `usb_mux_result()` reports NOT_FINISHED, completion or error.
- `usb_mux_set_native(bool)` is the blocking worker API, used by MSC and USB
  voice/session workers. It returns an error if another transition owns the mux.
- Connection settings and mouse screens enqueue changes. The toggle follows
  confirmation, and HID reports require a confirmed native route plus mount.
- Failed/ambiguous switching selects CP2105 and schedules `/OE` restoration.
  The worker retries recovery, including after a late disconnect ACK. A UI
  request for CP2105 is retained even when a transition is busy.
- On C5 restart/loss, the worker returns GPIO19 to CP2105. The C5 boot/lease safe
  state also enables `/OE`; reconnection does not restore native automatically.

Confirmation is bounded to 1200 ms per remote level assignment. The worker
checks recovery every 100 ms when not busy. Failed physical GPIO writes cannot
be guaranteed recovered by software; they remain errors and are retried.
Recovery boot keeps the boot-selected CP2105 route and does not start native USB.

## Type-C role and power

U2 HUSB320-BA000 has ADDR strapped low through R26 (910 kΩ), selecting `0x42`;
PORT is floating (DRP default), EN_N is grounded, and INT/ID are not routed to
firmware inputs. The new `Drivers/husb320` boot initializer checks DEVICE_TYPE,
sets PORTROLE to sink-only with Try/accessory modes disabled, enables CONTROL1,
and reads both settings back. It runs after I²C initialization even in recovery.
No automatic source/OTG or VBUS discharge command is issued. Charger current
policy remains with BQ25896; this driver does not infer a current budget from CC.

Register definitions and enable behavior follow the manufacturer's
[HUSB320 datasheet, tables 21 and 23](https://en.hynetek.com/uploadfiles/site/219/news/cd136f46-0b6d-43db-81da-3750dd522663.pdf).
Initialization failures are recorded in the boot report; USB attachment is not
claimed working in that case. A role change may cause a brief detach at startup.
Before firmware runs, the hardware strap still determines the role: guaranteed
sink-only operation in ROM/power-off requires a hardware PORT strap change, not
an application patch. Validate both cable orientations, cold boot, recovery,
P4/C5 resets, J15, UART/native enumeration and shutdown with/without VBUS on V2.03.

`tools/board_control/test_consumers.py` exercises the production mux and HUSB320
code with fault injection, including GPIO failure, lost/late acknowledgement,
reboot during detach, readback mismatch and every I²C transaction failure.

## TinyUSB Callbacks

The component implements the required TinyUSB callbacks to serve descriptors to the USB host:

| Callback | Purpose |
|----------|---------|
| `tud_descriptor_device_cb` | Returns the composite device descriptor |
| `tud_descriptor_configuration_cb` | Returns the configuration descriptor, selecting the variant by link speed (HS/FS) and whether MSC is currently exposed |
| `tud_descriptor_string_cb` | Returns string descriptors (manufacturer, product, serial, CDC, MSC) |
| `tud_hid_descriptor_report_cb` | Returns the HID report descriptor |
| `tud_hid_get_report_cb` | Handles GET_REPORT requests (stub) |
| `tud_hid_set_report_cb` | Handles SET_REPORT requests (stub) |

CDC-ACM callbacks are registered by `host_link_cdc.c`. MSC sector callbacks
are implemented directly in `storage_vfs/usb_msc.c`; the inquiry response here
uses Highboy branding. The esp_tinyusb storage wrapper is not instantiated.

## Highboy USB identity

Native P4 USB uses Highboy as manufacturer and product in HID/CDC, storage and
optional voice modes. MSC SCSI INQUIRY reports vendor `Highboy`, product
`Highboy Storage` and revision `1.0`, with fixed-width space padding. The native TinyUSB
`inquiry2` callback supplies this identity before the library's legacy inquiry
fallback; no managed sources or USB VID/PID/serial values are changed.

The mounted volume label belongs to the card filesystem and is preserved. The
CP2105 UART bridge and ROM download interfaces have independent descriptors;
the P4 application cannot rename those through its native TinyUSB descriptors.
Reconnect native USB after updating so the host can enumerate the new strings.

## USB Storage descriptor review

USB dependencies now use compatible ranges (`esp_tinyusb ^2.4.0`,
`tinyusb ^0.21.0`); the Component Manager resolved 2.4.0 and 0.21.0~2 during
validation. `idf.py update-dependencies` refreshes an existing local lockfile.

Storage has PID `0x4003`, separate from HID/CDC `0x4001` and optional voice
`0x4002`, so host descriptor caches do not mix the layouts. Manufacturer/product
remain Highboy. The serial is the factory MAC rendered as twelve hexadecimal
digits. A device qualifier is supplied explicitly, and Other Speed Configuration
uses the active interface layout with opposite-speed bulk packet sizes and
descriptor type 7. Full-speed bulk endpoints use 64 bytes; high-speed uses 512.
