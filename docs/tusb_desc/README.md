# TinyUSB Descriptors (HID Composite)

This component defines the USB descriptors required to enumerate the ESP32-P4 as a USB HID Composite Device (Keyboard + Mouse) and provides the initialization routine for the TinyUSB driver.

## Overview

- **Location:** `components/Drivers/tusb_desc/`
- **Header:** `include/tusb_desc.h`
- **Dependencies:** `tinyusb`, `esp_tinyusb`, `driver/gpio`
- **USB Port:** High Speed (ESP32-P4)

## USB Descriptors

### Device Descriptor

| Field | Value |
|-------|-------|
| USB Version | 2.0 |
| Vendor ID | `0xCAFE` |
| Product ID | `0x4001` |
| Device Class | Defined at interface level |
| Configurations | 1 |

### Configuration Descriptor

| Field | Value |
|-------|-------|
| Interfaces | 1 (HID) |
| Max Power | 100 mA |
| Attributes | Remote Wakeup |

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
separate steps.

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
| `tud_descriptor_device_cb` | Returns the device descriptor |
| `tud_descriptor_configuration_cb` | Returns the configuration descriptor |
| `tud_descriptor_string_cb` | Returns string descriptors (manufacturer, product, serial) |
| `tud_hid_descriptor_report_cb` | Returns the HID report descriptor |
| `tud_hid_get_report_cb` | Handles GET_REPORT requests (stub) |
| `tud_hid_set_report_cb` | Handles SET_REPORT requests (stub) |
