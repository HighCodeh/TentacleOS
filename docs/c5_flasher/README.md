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

# C5 Flasher — V2.03

The P4 updates the running C5 through SPI OTA. `c5_flasher_update()` accepts
only `SPI_OTA_TRANSPORT_SPI`; other transports return `ESP_ERR_NOT_SUPPORTED`
before opening the image or sending commands. Images may be streamed from
`/sdcard/c5/TentacleOS_C5.bin` or supplied in memory. Existing SPI OTA framing,
status, progress and inactive-slot handling remain unchanged.

`c5_flasher_init()` needs no UART setup. `c5_flasher_release_uart()` is a
compatibility no-op. The two chips connect to separate CP2105 channels;
there is no direct P4-C5 UART. `c5_flasher_rom_flash()` returns
`ESP_ERR_NOT_SUPPORTED`; `c5_passthrough_run()` logs the missing connection
and returns without stopping the console or reconfiguring UART pins.

## Console download modes

- `c5 boot` or `c5 boot spi`: request ROM download over SPI; requires a running C5 application.
- `c5 boot physical` (also `c5 download physical`): assert C5 GPIO28 through
  P4 GPIO1/T14, wait 10 ms, then pulse C5 EN through P4 GPIO0/T13 for 20 ms.
  This works with a blank C5 and sends no SPI command to it. Transistor controls
  are inverted: high on the P4 asserts the corresponding C5 signal low.

Physical download first obtains the bridge recovery exclusion, drains in-flight
transactions and callbacks, and invalidates the session. It then suspends SPI and
latches automatic recovery off. OTA/maintenance conflicts, non-idle CS and drain
failures reject the operation before GPIO changes. The existing recovery gate
requires at least ten seconds after P4 startup or maintenance completion and
rejects shutdown or an already disabled recovery state.

BOOT stays asserted during external programming. Use the C5 CP2105 port to flash,
then power-cycle the whole board to release BOOT and resume SPI. GPIO errors are
reported, with reset/boot release attempted; the bridge stays suspended even on
failure until P4 restart. ESP_OK confirms the GPIO sequence, not a ROM handshake;
the external programmer must confirm connection to the C5.

The strap/reset procedure follows Espressif's
[boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/advanced-topics/boot-mode-selection.html).
Physical timing and first-flash acceptance still require testing on V2.03 hardware.
App-only update guards remain documented in [High Boy](../highboy/README.md).

## Automatic-reset exclusion on V2.03

SPI OTA and explicit reboot hold the bridge maintenance guard. Preflight errors
release it; definitive OTA DONE/ERROR also releases it with a boot grace period.
An uncertain OTA outcome disables automatic physical recovery until P4 restarts.
ROM-download requests do the same even if the acknowledgement times out, so the
supervisor cannot reset a deliberately selected loader during CP2105 programming.
See [bridge manager](../bridge_manager/README.md) for the complete exclusion contract.
