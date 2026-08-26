# Documentation hub

Aggregated, canonical copies of the project documentation, one directory per
component. Each component's in-tree README points back to its copy here.
Components present in both firmwares keep `# P4` and `# C5` sections in one
README.md, separated by `---`.

## Featured

- [host_link/](host_link/README.md) - companion app link: overview, [app implementation guide](host_link/app-guide.md), [protocol spec](host_link/protocol.md), per-firmware sections
- [spi_bridge/](spi_bridge/README.md) - P4<->C5 SPI bridge: architecture + per-firmware sections
- [app_sdk/](app_sdk/README.md) - **write an app for the device**: build, sign, install, run (loads signed `.hb` bundles from SD; plan in [`../APP_PLATFORM_PLAN.md`](../APP_PLATFORM_PLAN.md))

## All components

| Component | Docs |
|-----------|------|
| `audio_i2s` | [README.md](audio_i2s/README.md) - I2S audio output driver |
| `bad_usb` | [README.md](bad_usb/README.md)  |
| `bluetooth` | [README.md](bluetooth/README.md)  |
| `boot_report` | [README.md](boot_report/README.md)  |
| `bq25896` | [README.md](bq25896/README.md) - TI BQ25896 battery charger / PMIC driver |
| `bridge_manager` | [README.md](bridge_manager/README.md) - P4<->C5 bridge lifecycle and coordination service |
| `buttons_gpio` | [README.md](buttons_gpio/README.md)  |
| `c5_flasher` | [README.md](c5_flasher/README.md)  |
| `cc1101` | [README.md](cc1101/README.md)  |
| `console` | [README.md](console/README.md)  |
| `dns_server` | [README.md](dns_server/README.md)  |
| `drv2605l` | [README.md](drv2605l/README.md) - TI DRV2605L haptic motor driver |
| `esp_now` | [README.md](esp_now/README.md)  |
| `espnow_chat` | [README.md](espnow_chat/README.md)  |
| `gameboy` | [README.md](gameboy/README.md) - Game Boy emulator application |
| `host_link` | [app-guide.md](host_link/app-guide.md) [protocol.md](host_link/protocol.md) [README.md](host_link/README.md)  |
| `http_server` | [README.md](http_server/README.md)  |
| `i2c_init` | [README.md](i2c_init/README.md) - shared I2C bus initialization driver |
| `input_manager` | [README.md](input_manager/README.md)  |
| `ir` | [README.md](ir/README.md) - infrared transmit/receive service |
| `led` | [README.md](led/README.md) - status/RGB LED driver |
| `LoRa` | [README.md](LoRa/README.md) - LoRa messaging application |
| `lvgl` | [README.md](lvgl/README.md)  |
| `nfc` | [README.md](nfc/README.md) - NFC read/write application |
| `ota` | [README.md](ota/README.md)  |
| `pins` | [README.md](pins/README.md) - board pin assignment definitions |
| `power_manager` | [README.md](power_manager/README.md) - battery and power-state management service |
| `recovery` | [README.md](recovery/README.md)  |
| `sd_card` | [README.md](sd_card/README.md)  |
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
