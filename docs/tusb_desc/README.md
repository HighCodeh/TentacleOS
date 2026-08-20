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
| Product ID | `0x4001` |
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
| 1 | Manufacturer: "HighCode" |
| 2 | Product: "BadUSB Device" |
| 3 | Serial: "123456" |
| 4 | CDC interface: "TentacleOS Companion" |
| 5 | MSC interface: "TentacleOS SD" (only present when MSC is compiled in) |

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

### `usb_mux_init`
```c
void usb_mux_init(void);
```
Configures the mux select pin as an output and defaults it to the UART bridge.
Called once at boot from `kernel_init`.

### `usb_mux_set_native`
```c
esp_err_t usb_mux_set_native(bool native);
```
Switches the USB-C data lines at runtime (no reset). `true` = P4 native USB
(brings the TinyUSB composite up first, then routes the connector); `false` =
CP2105 UART bridge.

### `usb_mux_is_native`
```c
bool usb_mux_is_native(void);
```
Current mux state.

## USB-C data mux (TS3USB221)

The HighBoy V2 has a **single Type-C connector** whose `D+/D-` are muxed by a
**TS3USB221 (U13)** between two destinations, selected by
`GPIO_USB_MUX_SEL_PIN` (GPIO19):

| Select (GPIO19) | Route | Use |
|-----------------|-------|-----|
| LOW (default, 10k pulldown) | CP2105 USB-UART bridge | serial console + flashing |
| HIGH | ESP32-P4 native USB PHY | TinyUSB composite (BadUSB HID + companion CDC) |

Only one path is live at a time - they share the connector. Because the pin has
a hardware pulldown, the board powers up on the **UART bridge**, so the serial
console and flashing work by default. Flashing is unaffected either way: the ROM
download mode runs before the app drives the pin.

**Runtime toggle:** Settings -> Connection -> **USB NATIVE**. Turning it on calls
`usb_mux_set_native(true)`; the switch is immediate and needs no reset, but the
USB-serial console over that connector goes away until it is turned off (or the
device resets, which returns to the UART default). BadUSB and the companion CDC
only enumerate while the mux is on native.

> Earlier the firmware defined `GPIO_USB_MUX_SEL_PIN` but never drove it, so the
> mux stayed on the UART bridge and native USB never enumerated. That was the
> root cause of "only USB-UART works".

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

The CDC-ACM and MSC class callbacks are not implemented here: they live in the
components that own those interfaces (the companion host link for CDC, and the
SD/mass-storage feature for MSC). This component only defines the shared
descriptors and the descriptor-serving callbacks above.
