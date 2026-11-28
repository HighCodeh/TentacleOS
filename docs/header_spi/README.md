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

# External header SPI

ABI 1.24 app capability `TOS_CAP_HEADER` (0x100) exposes bounded full-duplex
SPI transfers and one control GPIO through `api->header`. The NRF24 driver lives
in the app, not in the firmware.

The SPI3 bus remains owned by the firmware. Each transaction uses its shared
lock (100 ms timeout); an internal worker handles app calls from PSRAM stacks.
CS is driven by GPIO rather than consuming a hardware CS slot (already shared
by the display, CC1101 and NFC). Each transfer acquires the SDK bus before
asserting CS, completes synchronously, then deasserts CS even on transfer error
before releasing the bus. The SDK can report exhausted CS slots as
`ESP_ERR_NO_MEM`; this is not necessarily a heap allocation failure.
Two 64-byte internal DMA staging arrays bound the internal-memory cost. Only
one app session may open the header. Stale or foreign sessions are rejected.
Back, normal exit and forced app cleanup close the session, lower the control
pin and release the sleep hold. No automatic RF transmission occurs on open.

## V2.03 wiring

Use the documented schematic header numbering in [Pins](../pins/README.md).
This is not the legacy GPIO-header table.

| nRF24L01+ | Highboy header | GPIO / supply |
| --- | --- | --- |
| GND (1) | 8 | GND |
| VCC (2) | 10 | 3.3 V |
| CE (3) | 7 | GPIO24 |
| CSN (4) | 9 | GPIO25 |
| SCK (5) | 14 | GPIO21 |
| MOSI (6) | 17 | GPIO22 |
| MISO (7) | 16 | GPIO23 |
| IRQ (8) | unused | polling |

Pins 7/9 are debug USB pads: GPIO use disables that debug PHY until reset.
The main USB port is separate. Do not attach debug USB while using these pins.
Only pins 7/9 may serve as CS/control; clocks are restricted to 100 kHz–4 MHz,
SPI modes 0–3, transfers 1–64 bytes. CS cannot be changed through GPIO write.

## NRF24 example

Build with `tools/app_sdk/nrf24/build.sh` after sourcing ESP-IDF 6.1.
Install `NRF24.hb` in `/sdcard/apps/NRF24.hb`, then open Apps or run
`apprun NRF24 grant`. Requires updated P4 firmware; C5 is unchanged.

The app reads back configuration and address before enabling Send. A register
mismatch shows the register, received byte and expected byte on screen, with the
SPI return code in the terminal. `ESP_ERR_NOT_FOUND` is a readback mismatch, not
a bus-open failure; a successful SPI transaction alone does not prove a radio is
connected. Defaults:
channel 76 (2476 MHz), 1 Mbps, -6 dBm, 2-byte CRC, five address bytes `HBOY1`,
fixed 16-byte payload. OK sends one packet, UP retries initialization,
LEFT/RIGHT select channel 0–83, DOWN toggles ACK, BACK powers down and exits.
ACK off confirms local TX completion only; ACK on requires a matching receiver
and reports timeout if no acknowledgement arrives. It does not discover peers.

Protocol reference: [Nordic nRF24L01+ specification](https://docs-be.nordicsemi.com/bundle/nRF24L01P_PS_v1.0/raw/resource/enus/nRF24L01P_PS_v1.0.pdf).

## Validation

Host tests simulate register readback, a disconnected module, TX completion, ACK
failure, timeout and ACK labels. Run:

```sh
cc -std=c11 -Wall -Wextra -Werror -I firmware_p4/components/Service/app_runtime/include tools/app_sdk/nrf24/test.c -o /tmp/nrf24-test
/tmp/nrf24-test
```

`python3 tools/highboy/tests/test_header_spi.py` compiles the production header
driver with host mocks and sanitizers. It covers occupied hardware CS slots,
100 open/transfer/close cycles, bus-acquisition and transfer failures, and retry
of failed removal. After installing the software-CS change, the user reported
that the NRF24 worked after correcting a loose connection. This is a user
observation, not a measured over-the-air or long-duration validation.

These tests do not prove over-the-air reception; a second configured radio is
required to verify delivery.
