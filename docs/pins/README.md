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

# Pins — High Boy V2.03

Only V2.03 is supported. Each target owns its `Drivers/pins/include/pin_def.h`.
GPIO numbers belong in these headers. There is no legacy board selection.
See the [migration map](../highboy/V2_03_MIGRATION_PLAN.md#2-mapa-confirmado-e-mudanças)
for schematic page references and the complete migration sequence.

## P4

V2.03 uses ESP32-P4 silicon revision **v3.2**. The pinned IDF v6.1
configuration selects minimum v3.1 (its latest selectable minimum) and maximum
v3.99, with `ESP32P4_SELECTS_REV_LESS_V3` disabled. Rebuild both bootloader and
application from regenerated configuration when migrating from the old v1.x
profile; do not bypass the image revision check with `--force`. Existing
`sdkconfig` files take precedence over `sdkconfig.defaults`.

| Function | GPIO |
| --- | --- |
| Shared SPI MOSI / SCLK / MISO | 22 / 21 / 23 |
| I2C SDA / SCL | 31 / 30 |
| CC1101 CS / GDO0 / GDO2 | 20 / 8 / 9 |
| RF switch V1 / V2 | 17 / 18 |
| Display CS / DC / reset / backlight | 51 / 50 / 52 / 14 |
| IMU CS | 36 |
| NFC CS / IRQ | 49 / 10 |
| Buttons LEFT / RIGHT / UP / DOWN / OK / BACK | 35 / 13 / 3 / 7 / 32 / 6 |
| LF RFID carrier / data / modulation / coil | 15 / 16 / 11 / 2 |
| LoRa CS / BUSY / DIO1 | 26 / 4 / 5 |
| SDMMC CLK / CMD / D0 / D1 / D2 / D3 / detect | 43 / 44 / 39 / 40 / 41 / 42 / 34 |
| Bridge SCLK / MOSI / MISO / CS | 45 / 46 / 47 / 48 |
| C5 reset / boot (inverting transistor controls) | 0 / 1 |
| I2S data / BCLK / LRCLK | 27 / 28 / 29 |
| PDM mic CLK / data | 54 / 53 |
| Charger OTG | 33 |
| USB mux select | 19 |
| J15 debug USB D− / D+ | 24 / 25 |
| Console UART RX / TX (CP2105 ECI) | 37 / 38 |

B2 is confirmed as OK (GPIO32). TPS3421 monitors B2/GPIO32 and B4/GPIO7
for physical reset; its timing must be validated on the populated board.
BACK also connects to charger QON; LEFT/GPIO35 is the P4 boot strap.
LoRa reset/TXEN/RXEN are not wired (`-1`, guarded by their driver).
The LED and AW8623 use P4 I2C; there is no haptic trigger on GPIO32.

## V2.03 external module header

The external expansion header is drawn as **J15 (pins 1-10)** and
**J14 (pins 11-18)**. GPIO numbers below belong to the **ESP32-P4**, not the C5.
This is schematic pin numbering, not a left-to-right view of the enclosure:
confirm the physical pin-1 marking before connecting a module.

Source: `High Boy_V2_03 Schematic 2026-10-19.PDF`, page 7, sheet title
“HighBoy_V2 connectors USB GPIO MC card” (`SD_CARD_IO_Ports.SchDoc`).
The reference PDF is supplied locally at the repository root and is not a
tracked documentation dependency. Processor connections were cross-checked on
page 2; current firmware assignments are in
[P4 pin_def.h](../../firmware_p4/components/Drivers/pins/include/pin_def.h).

| Header | Pin | Schematic signal | Current connection / use |
| --- | --- | --- | --- |
| J15 | 1 | GND | Ground |
| J15 | 2 | GPIO19 | Shared with USB data-mux selection |
| J15 | 3 | GPIO31 | Shared I2C SDA |
| J15 | 4 | GPIO30 | Shared I2C SCL |
| J15 | 5 | GPIO38 | UART TX from Highboy; shared with the console/CP2105 |
| J15 | 6 | GPIO37 | UART RX into Highboy; shared with the console/CP2105 |
| J15 | 7 | GPIO24 | Debug USB D- |
| J15 | 8 | GND | Ground |
| J15 | 9 | GPIO25 | Debug USB D+ |
| J15 | 10 | 3.3V | 3.3 V supply rail |
| J14 | 11 | GND | Ground |
| J14 | 12 | GPIO11 | Through R79 (1 kOhm); shared with LF RFID modulation |
| J14 | 13 | GPIO12 | Through R80 (1 kOhm); GPS UART1 RX during the console diagnostic; otherwise available |
| J14 | 14 | GPIO21 CLK | Shared SPI clock |
| J14 | 15 | GPIO33 | Shared with charger OTG enable |
| J14 | 16 | GPIO23 SO | Shared SPI MISO (input to P4) |
| J14 | 17 | GPIO22 SI | Shared SPI MOSI (output from P4) |
| J14 | 18 | VSYS | System supply rail; not a fixed 5 V output |

### Connecting external modules

SPI and I2C on this header share the internal peripheral buses. External SPI
devices need their own chip select and compatible bus arbitration; external I2C
devices need non-conflicting addresses. GPIO12 is used by the [GPS console diagnostic](../gps/README.md) while a query
is active; otherwise it is a candidate for an external
chip select, not a reserved or validated module interface; account for its
series resistor when checking timing and loading.

GPIO19, GPIO33 and GPIO11 already control internal hardware. They must not be
treated as spare GPIOs while those functions are active. The UART is also
connected to the CP2105, so an external transmitter must not contend with the
bridge on the P4 RX line. Debug USB pins are a separate path from the muxed
native USB-C connection; see [USB routing](../tusb_desc/README.md#v203-usb-routing).

Use 3.3 V-compatible logic. The header does not imply 5 V-tolerant GPIOs, and
VSYS must not be treated as a logic-level reference. Available supply current,
connector orientation and operation with a particular module still require
board-level validation; this table records wiring, not a tested accessory API.

## C5

| Function | GPIO |
| --- | --- |
| Bridge SCLK / MOSI / MISO / CS | 26 / 25 / 24 / 23 |
| Amplifier CTRL | 0 |
| Haptic INTN / RSTN | 1 / 5 |
| USB mux /OE | 4 |
| IR TX / RX | 6 / 10 |

No bridge IRQ, local I2C peripheral bus, SD card, display, buttons or RGB LED
is wired to C5. Legacy master SPI/I2C/button sources are excluded from the build;
LED compatibility calls do nothing, and local SD initialization returns
`ESP_ERR_NOT_SUPPORTED`. The only interprocessor data link is SPI, with polling.

## Safe boot

`board_pins_init()` runs first in both kernels, before storage, radio or safe-mode
selection. It writes each output latch low before configuring the pad as output,
checks GPIO errors, and stops boot on failure.

P4 releases C5 reset/boot, selects CP2105 and holds OTG GPIO33 low. C5 disables amp/IR, holds AW8623
in reset and keeps USB `/OE` low (enabled). Basic USB routing does not depend
on a working SPI link. Haptic INTN and IR RX remain inputs until their drivers
are implemented. Pull-downs in the schematic provide the corresponding default
for the relevant control nets before firmware starts; verify actual levels and
transients on the board.

The P4 IR and amplifier APIs currently fail closed while awaiting remote C5
implementations. They never drive P4 GPIO0/1/49. No automatic C5 reset or ROM
boot sequence is introduced by this stage.

## V2.03 remote controls

The negotiated [board control service](../board_control/README.md) owns remote
amplifier enable, haptic reset and USB `/OE`, with explicit capability checks,
state reconciliation and a C5 safe-state lease. Audio and USB now use the injected controls; see [audio](../audio_i2s/README.md)
and [USB/Type-C](../tusb_desc/README.md). [AW8623](../aw8623/README.md) provides diagnostics, latched INTN reporting and bounded ERM playback
using the project’s fixed 3.0 V drive reference; electrical acceptance remains pending.

IR TX/RX now execute on C5 GPIO6/10 through the [remote IR backend](../ir/README.md).
P4 GPIO0/1 remain exclusively reset/boot controls.

### C5 normal-boot reset

`board_pins_reset_c5()` releases BOOT (P4 GPIO1 low), waits 10 ms, asserts reset
(GPIO0 high) for 20 ms and releases it low. T13/T14 invert these controls. It is
called only by the [exclusive bridge supervisor](../bridge_manager/README.md)
after drain/CS checks; it never selects the ROM loader. Timing requires bench acceptance.

### C5 physical download

`board_pins_download_c5()` asserts T14 (P4 GPIO1 high), waits 10 ms and
pulses T13 (GPIO0 high for 20 ms, then low). BOOT remains asserted until P4
restart. The caller must first drain and exclusively own the SPI bridge. The
console entry point and failure policy are documented in
[C5 flasher](../c5_flasher/README.md#console-download-modes).
