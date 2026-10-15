| Supported Targets | ESP32-S2 | ESP32-S3 | ESP32-P4 |
| ----------------- | -------- | -------- | -------- |

# USB: CDC Class test application

## CDC-ACM driver

It tests basic functionality of the driver like open/close/read/write operations, advanced features like CDC control request, multi-threaded or multi-device access, as well as reaction to sudden disconnection and other error states.

### Hardware Required

This test requires two ESP32 development board with USB-OTG support. The development boards shall have interconnected USB peripherals, one acting as host running CDC-ACM host driver and another CDC-ACM device driver (tinyusb).

This test expects that TinyUSB dual CDC device with VID = 0x303A and PID = 0x4002 is connected to the USB host.

## Selecting the USB Component

To manually select which USB Component shall be used to build this test application, please refer to the following documentation page: [Manual USB component selection](../../../../../docs/host/usb_host_lib/usb_component_manual_selection.md).
