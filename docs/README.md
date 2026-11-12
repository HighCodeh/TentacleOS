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

# Documentation

Reference documentation for Highboy hardware, firmware services, applications
and development workflows. Each component has one canonical document linked
below; in-tree READMEs point to it.

## Start here

- [Build and development](highboy/README.md)
- [Application updates](highboy/APP_UPDATE.md)
- [Release packaging](highboy/RELEASE.md)
- [Board pin mapping](pins/README.md)
- [UI architecture and screen lifecycle](ui/README.md)
- [Application SDK](app_sdk/README.md)
- [Companion app integration](host_link/app-guide.md)

## Components

| Component | Documentation |
| --- | --- |
| `audio_i2s` | [README.md](audio_i2s/README.md) - I2S audio output driver |
| `aw8623` | [README.md](aw8623/README.md) - haptic playback and diagnostics |
| `bad_usb` | [README.md](bad_usb/README.md)  |
| `bluetooth` | [README.md](bluetooth/README.md)  |
| `board_control` | [README.md](board_control/README.md) - V2.03 remote amp, haptic reset and USB enable |
| `boot_report` | [README.md](boot_report/README.md)  |
| `bq25896` | [README.md](bq25896/README.md) - TI BQ25896 battery charger / PMIC driver |
| `bridge_manager` | [README.md](bridge_manager/README.md) - P4<->C5 bridge lifecycle and coordination service |
| `buttons_gpio` | [README.md](buttons_gpio/README.md)  |
| `c5_flasher` | [README.md](c5_flasher/README.md)  |
| `cc1101` | [README.md](cc1101/README.md)  |
| `console` | [README.md](console/README.md)  |
| `dns_server` | [README.md](dns_server/README.md)  |
| `doom` | [README.md](doom/README.md) - DOOM input and internal-DMA strip rendering |
| `drv2605l` | [README.md](drv2605l/README.md) - TI DRV2605L haptic motor driver |
| `esp_now` | [README.md](esp_now/README.md)  |
| `espnow_chat` | [README.md](espnow_chat/README.md)  |
| `fatfs` | [README.md](fatfs/README.md) - exFAT integration and filesystem validation |
| `firmware_crypto` | [README.md](crypto/README.md) - shared public PSA cryptography |
| `gameboy` | [README.md](gameboy/README.md) - Game Boy emulator application |
| `gatt` | [README.md](gatt/README.md) - remote BLE service discovery and characteristic reads |
| `gps` | [README.md](gps/README.md) - external UART GPS wiring and console diagnostics |
| `hle` | [README.md](hle/README.md) - host simulator compatibility and SPI CRC regression tests |
| `host_link` | [app-guide.md](host_link/app-guide.md) [protocol.md](host_link/protocol.md) [README.md](host_link/README.md)  |
| `http_server` | [README.md](http_server/README.md)  |
| `husb320` | [Type-C policy](tusb_desc/README.md#type-c-role-and-power) - V2.03 sink-only initialization |
| `i2c_init` | [README.md](i2c_init/README.md) - shared I2C bus initialization driver |
| `imu` | [README.md](imu/README.md) - motion acquisition, calibration and snapshots |
| `input_manager` | [README.md](input_manager/README.md)  |
| `ir` | [README.md](ir/README.md) - infrared transmit/receive service |
| `led` | [README.md](led/README.md) - status/RGB LED driver |
| `LoRa` | [README.md](LoRa/README.md) - LoRa messaging application |
| `lvgl` | [README.md](lvgl/README.md)  |
| `mqtt` | [README.md](mqtt/README.md) - C5 MQTT transport and Meshtastic UI integration |
| `muse` | [README.md](muse/README.md) - P4 PTT and C5 pairing/runtime integration |
| `nfc` | [README.md](nfc/README.md) - NFC read/write application |
| `ota` | [README.md](ota/README.md)  |
| `pc_monitor` | [README.md](pc_monitor/README.md) - computer telemetry |
| `pins` | [README.md](pins/README.md) - board pin assignment definitions |
| `power_manager` | [README.md](power_manager/README.md) - battery and power-state management service |
| `qmi8658a` | [README.md](qmi8658a/README.md) - synchronized six-axis SPI driver |
| `recovery` | [README.md](recovery/README.md)  |
| `rf_frontend` | [README.md](rf_frontend/README.md) - shared NFC/LF antenna and native RFID ownership |
| `sd_card` | [README.md](sd_card/README.md)  |
| `security` | [README.md](security/README.md) - isolated paired security configuration and provisioning boundary |
| `spi` | [README.md](spi/README.md)  |
| `spi_bridge` | [README.md](spi_bridge/README.md)  |
| `st7789` | [README.md](st7789/README.md)  |
| `storage_api` | [README.md](storage_api/README.md)  |
| `storage_assets` | [README.md](storage_assets/README.md)  |
| `storage_vfs` | [README.md](storage_vfs/README.md)  |
| `SubGhz` | [README.md](SubGhz/README.md)  |
| `sx1262` | [README.md](sx1262/README.md) - Semtech SX1262 LoRa radio transceiver driver |
| `sys_monitor` | [README.md](sys_monitor/README.md)  |
| `sys_prio` | [README.md](sys_prio/README.md)  |
| `sys_time` | [README.md](sys_time/README.md) - system clock / RTC time service |
| `tusb_desc` | [README.md](tusb_desc/README.md)  |
| `ui` | [README.md](ui/README.md) [input-migration.md](ui/input-migration.md)  |
| `wifi` | [README.md](wifi/README.md)  |
| `ys_rfid2` | [README.md](ys_rfid2/README.md) - YS-RFID2 125 kHz RFID reader driver |
