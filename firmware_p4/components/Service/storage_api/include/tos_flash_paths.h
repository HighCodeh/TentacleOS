// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef TOS_FLASH_PATHS_H
#define TOS_FLASH_PATHS_H

#ifdef __cplusplus
extern "C" {
#endif
#define FLASH_MOUNT "/assets"

// Config fallback paths (read-only)
#define FLASH_CONFIG_SCREEN     FLASH_MOUNT "/config/screen/screen_config.conf"
#define FLASH_CONFIG_THEMES     FLASH_MOUNT "/config/screen/screen_themes.conf"
#define FLASH_CONFIG_INTERFACE  FLASH_MOUNT "/config/screen/interface_config.conf"
#define FLASH_CONFIG_BRIGHTNESS FLASH_MOUNT "/config/screen/brightness.conf"
#define FLASH_CONFIG_WIFI_AP    FLASH_MOUNT "/config/wifi/wifi_ap.conf"
#define FLASH_CONFIG_BLE        FLASH_MOUNT "/config/bluetooth/ble_announce.conf"
#define FLASH_CONFIG_LORA       FLASH_MOUNT "/config/lora/lora.conf"
#define FLASH_CONFIG_SYSTEM     FLASH_MOUNT "/config/system/system.conf"
#define FLASH_CONFIG_LED        FLASH_MOUNT "/config/led/led.conf"
#define FLASH_CONFIG_BUZZER     FLASH_MOUNT "/config/buzzer/buzzer.conf"
#define FLASH_CONFIG_BUZZER_DIR FLASH_MOUNT "/config/buzzer/sounds"

// Storage paths (writable on flash)
#define FLASH_STORAGE_WIFI         FLASH_MOUNT "/storage/wifi"
#define FLASH_STORAGE_WIFI_BEACONS FLASH_MOUNT "/storage/wifi/beacon_list.json"
#define FLASH_STORAGE_BLE          FLASH_MOUNT "/storage/ble"
#define FLASH_STORAGE_BADUSB       FLASH_MOUNT "/storage/bad_usb_scripts"

// Captive portal HTML templates (flash fallback)
#define FLASH_CAPTIVE_TEMPLATES FLASH_MOUNT "/html/captive_portal"

// NFC key dictionaries (flash fallback)
#define FLASH_NFC_DICT FLASH_MOUNT "/nfc/dict"

#ifdef __cplusplus
}
#endif

#endif // TOS_FLASH_PATHS_H
