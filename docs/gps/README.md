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

# External UART GPS test

The console test targets the GY-GPS6MV2 breakout's NEO-6 NMEA output. Default
UART settings are 9600 baud, 8N1, no flow control, matching the
[u-blox NEO-6 default configuration](https://content.u-blox.com/sites/default/files/products/documents/NEO-6_DataSheet_%28GPS.G6-HW-09005%29.pdf).
Previously configured modules may require a different `--baud`.

## Wiring

Use [schematic header numbering](../pins/README.md#v203-external-module-header),
not the legacy GPIO app's position table. That app still contains older board
assignments. Identify GPIO12 physically before connecting.

| GPS pin | Highboy connection |
| --- | --- |
| TX | GPIO12, J14 pin 13, through R80 (1 kOhm) |
| GND | Header GND (for example J14 pin 11) |
| RX | Leave disconnected; no module configuration is transmitted |
| VCC | Supply appropriate to the particular breakout; header pin 10 is 3.3 V, pin 18 is variable VSYS, not regulated 5 V |

The NEO-6 chip specification does not establish the input voltage/dropout of
all GY-GPS6MV2 breakout regulators. If your board requires 5 V at VCC, use a
regulated external 5 V supply with common GND; do not connect that supply to a
Highboy power rail. GPS TX logic must be 3.3 V-compatible. Do not connect GPS
TX to the header UART RX (GPIO37): CP2105 already drives it.

Keep the GPIO control app closed and do not drive GPIO12 from another app
while this diagnostic is running. UART1 must be unused; an installed driver
causes the command to fail without removing it. The diagnostic does not provide
system-wide pin arbitration for arbitrary external apps.

## Commands

```text
gps --geo
gps --info
gps --raw --timeout 10
gps --geo --baud 9600 --timeout 120
gps --help
```

`gps` alone is equivalent to `gps --geo`. Options select 1200-115200 baud and
1-120 seconds. The console command blocks until a result or timeout; it does
not create a background task. Raw mode runs until timeout. A separate stop
command cannot interrupt the blocked REPL. After completion/failure, the UART
is deleted and GPIO12 reset to its input/default state. The module's power is
not switched by software.

`--geo` accepts a newly received, checksum-valid GGA sentence with usable fix
quality and valid coordinates. It never returns a cached position. It converts
NMEA degrees/minutes to decimal degrees, including negative south/west values.
Estimated/manual/simulated positions (quality 6-8) are not reported as a GPS fix.
`--info` can succeed without a position, reporting quality 0 and satellite count.
UTC is the GGA time of day, not a date; altitude is the GGA mean-sea-level value
in meters. Other NMEA messages remain visible in raw mode but are not parsed.

A fresh cold start can take longer than the default timeout. Test outdoors with
an unobstructed antenna, and repeat the command or use `--timeout 120`.
No bytes suggests power/wiring/baud trouble. Valid GGA without a fix confirms
serial communication but not satellite reception. Raw lines are diagnostic
bytes and are not themselves a verified position.

## Validation

`python3 tools/highboy/tests/test_gps_nmea.py` compiles the production parser with
ASan/UBSan and checks checksum rejection, coordinate conversion, hemisphere,
no-fix and malformed/estimated fixes. Command tests also inject UART initialization/read errors, an occupied UART
and timeout to check cleanup and routing. Physical GPS acquisition, breakout supply
compatibility and antenna reception require testing on the device.

## Installation layout

The GPS candidate uses the [expanded P4 layout](../highboy/README.md#flash-and-storage-boundary):
3 MiB in each OTA slot, and 1,856 KiB for assets. Existing 0x2F0000-slot devices
need a backed-up serial migration of the table, OTA metadata, application and
rebuilt assets. The guarded app-only updater intentionally rejects the old map;
this command addition does not authorize writing a mismatched image. C5 is unchanged.
