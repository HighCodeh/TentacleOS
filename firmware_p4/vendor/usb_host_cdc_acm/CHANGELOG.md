# Changelog for USB Host CDC-ACM driver

All notable changes to this component will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.4.0] - 2026-04-14

### Added

- Added remote wakeup feature
- Added option to open a CDC device with specific USB address
- Added support for ESP32-S31

## [2.3.0] - 2026-01-23

### Added

- Added missing CTS signal definition in `cdc_acm_uart_state_t` to complete RS-232 set
- Added support for storing user-defined, device-specific data within CDC-ACM-like driver implementations via `cdc_acm_host_interface.h`

## [2.2.0] - 2025-12-16

### Added

- Added global suspend/resume support
- Added support for transmitting data larger than the configured output buffer size

### Fixed

- Fixed opening of CDC devices with any VID/PID when connected through a USB hub

## [2.1.1] - 2025-09-24

### Added

- Added support for ESP32-H4
- Added support for IDF 6.0

## [2.1.0] - 2025-04-25

### Added

- Added option to implement custom CDC-ACM like devices with C API

## [2.0.6] - 2024-11-29

- Fixed device opening for devices with CDC class defined in Device descriptor https://github.com/espressif/esp-usb/pull/89
- Fixed driver installation from a task with priority >= CDC driver priority

## [2.0.5] - 2024-10-09

### Added

- Added an option to open a CDC device with any VID/PID

### Fixed

- Fixed CDC descriptor parsing logic, when some CDC devices could not be opened
- Fixed crash on ESP32-P4 if Receive buffer append function (introduced in v2.0.0) was used; this function does not work on P4 yet

## [2.0.4] - 2024-09-03

### Fixed

- Fixed Control transfer allocation size for too small EP0 Max Packet Size (https://github.com/espressif/esp-idf/issues/14345)

### Changed

- Merged `open()` and `open_vendor_specific()` functions. All types of CDC devices are now opened with `cdc_acm_host_open()`, CDC compliance is detected automatically

## [2.0.3] - 2024-06-17

### Added

- Added `cdc_acm_host_cdc_desc_get()` function that enables users to get CDC functional descriptors of the device

### Fixed

- Fixed closing of a CDC device from multiple threads
- Fixed reaction on TX transfer timeout (https://github.com/espressif/esp-protocols/issues/514)

## [2.0.2] - 2023-11-21

### Fixed

- Return an error if the selected interface does not have IN and OUT bulk endpoints

## [2.0.1] - 2023-09-07

### Added

- Added support for USB "triple null" devices, which use USB interface association descriptors, but have Device Class, Device Subclass, and Device Protocol all set to 0x00, instead of 0xEF, 0x02, and 0x01 respectively. USB Standard reference: https://www.usb.org/defined-class-codes, Base Class 00h (Device) section.

## [2.0.0] - 2023-02-22

### Added

- New function `cdc_acm_host_register_new_dev_callback`. This allows you to get New Device notifications even if you use the default driver.
- Receive buffer has configurable size. This is useful if you expect data transfers larger then Maximum Packet Size.
- Receive buffer has 'append' function. In the Data Received callback you can signal that you wait for more data and the current data were not yet processed. In this case, the CDC driver appends new data to the already received data. This is especially useful if the upper layer messages consist of multiple USB transfers and you don't want to waste more RAM and CPU copying the data around.

## [1.0.4] - 2022-11-07

### Changed

- C++ methods are now virtual to allow derived classes to override them.

## [1.0.0] - 2022-07-12

- Initial version
