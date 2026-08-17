# C5 Flasher Service - P4 Master

This service allows the ESP32-P4 to update the firmware of the ESP32-C5 using an embedded binary image.

## Features
- **Embedded Binary**: The C5 firmware is embedded directly into the P4 executable during the build process.
- **Bootloader Control**: Automatically puts the C5 into serial bootloader mode using the BOOT and RESET pins.
- **Serial Protocol**: Implements the Espressif Serial Protocol (SLIP framing) to write blocks to the C5 flash.

## Usage
1. **Initial Sync**: On boot, the `bridge_manager` checks the C5 version.
2. **Auto-Update**: If the C5 is unresponsive or outdated, `c5_flasher_update(NULL, 0)` is called.
3. **Execution**: The P4 stops the SPI bridge, initializes the Flasher UART, pulses the Reset pin with Boot LOW, and starts streaming the binary.

## Symbols
The embedded binary is accessed via:
- `_binary_firmware_c5_bin_start`
- `_binary_firmware_c5_bin_end`

## Build Automation
Use the `./tools/build_and_flash.sh` script to ensure the C5 binary is updated and embedded correctly before flashing the P4.
