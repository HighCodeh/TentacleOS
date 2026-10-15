# USB Host CDC-ACM Class Driver

[![Component Registry](https://components.espressif.com/components/espressif/usb_host_cdc_acm/badge.svg)](https://components.espressif.com/components/espressif/usb_host_cdc_acm) ![maintenance-status](https://img.shields.io/badge/maintenance-passively--maintained-yellowgreen.svg) ![changelog](https://img.shields.io/badge/Keep_a_Changelog-blue?logo=keepachangelog&logoColor=E05735)

This component contains an implementation of a USB CDC-ACM Host Class Driver that is implemented on top of the [USB Host Library](https://components.espressif.com/components/espressif/usb).

## Supported Devices

The CDC-ACM Host driver supports the following types of CDC devices:

1. CDC-ACM devices
2. CDC-like vendor specific devices (usually found on USB to UART bridge devices or cellular modems)

### CDC-ACM Devices

The CDC-ACM Class driver supports CDC-ACM devices that meet the following requirements:

- The device class code must be set to the CDC class `0x02` or implement Interface Association Descriptor (IAD)
- The CDC-ACM must contain the following interfaces:
  - A Communication Class Interface containing a management element (EP0) and may also contain a notification element (an interrupt endpoint). The driver will check this interface for CDC Functional Descriptors.
  - A Data Class Interface with two BULK endpoints (IN and OUT). Other transfer types are not supported by the driver

### CDC-Like Vendor Specific Devices

The CDC-ACM Class driver supports CDC-like devices that meet the following requirements:

- The device class code must be set to the vendor specific class code `0xFF`
- The device needs to provide an interface containing the following endpoints:
  - (Mandatory) Two Bulk endpoints (IN and OUT) for data
  - (Optional) An interrupt endpoint (IN) for the notification element

## Usage

The following steps outline the typical API call pattern of the CDC-ACM Class Driver

1. Install the USB Host Library via `usb_host_install()`
2. Install the CDC-ACM driver via `cdc_acm_host_install()`
3. Open a CDC-ACM/CDC-like device with `cdc_acm_host_open()`. The call blocks until a matching device is connected or the connection timeout expires.
4. To transmit data, call `cdc_acm_host_data_tx_blocking()`
5. When data is received, the driver will automatically run the receive data callback
6. An opened device can be closed via `cdc_acm_host_close()`
7. The CDC-ACM driver can be uninstalled via `cdc_acm_host_uninstall()`

Use `CDC_HOST_ANY_VID`, `CDC_HOST_ANY_PID`, and `CDC_HOST_ANY_DEV_ADDR` when you do not want to filter by vendor ID, product ID, or USB device address. Wildcards are appropriate when only one matching device is expected. If several devices share the same VID and PID (for example behind a hub), fill `cdc_acm_host_open_config_t` and set `dev_addr` to the device’s USB address.

## Examples

- For an example with a CDC-ACM device, or Virtual COM Port device refer to [cdc example in esp-idf](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/host/cdc)
- For examples with [esp_modem](https://components.espressif.com/components/espressif/esp_modem), refer to [esp_modem examples](https://github.com/espressif/esp-protocols/tree/master/components/esp_modem/examples)
