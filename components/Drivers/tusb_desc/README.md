# TinyUSB Descriptors (HID)

This component defines the USB descriptors required to enumerate the ESP32 as a USB HID Keyboard (BadUSB). It uses the TinyUSB stack provided by Espressif.

## Overview

- **Location:** `components/Drivers/tusb_desc/`
- **Header:** `include/tusb_desc.h`
- **Dependencies:** `tinyusb`, `esp_tinyusb`

## Descriptors Defined

### Device Descriptor
- **USB Version:** 2.0
- **Vendor ID:** `0xCAFE` (Example/Test ID)
- **Product ID:** `0x4001`
- **Class:** Defined at Interface level

### Configuration Descriptor
- **Interfaces:** 1 (HID)
- **Power:** 100mA
- **Attributes:** Remote Wakeup enabled

### HID Report Descriptor
- **Type:** Keyboard
- **Report ID:** `1` (Must match the ID used in the application logic/parser).

### String Descriptors
0.  Language ID (English)
1.  Manufacturer: "HighCode"
2.  Product: "BadUSB Device"
3.  Serial: "123456"

## API Reference

### `busb_init`
```c
void busb_init(void);
```
Initializes the TinyUSB driver with the defined descriptors.
- Installs the driver using `tinyusb_driver_install`.
- **Note:** This function must be called before attempting to send any keystrokes.

## Callbacks (TinyUSB Hooks)
The component implements standard TinyUSB callbacks to serve descriptors to the host:
- `tud_descriptor_device_cb`
- `tud_descriptor_configuration_cb`
- `tud_descriptor_string_cb`
- `tud_hid_descriptor_report_cb`
