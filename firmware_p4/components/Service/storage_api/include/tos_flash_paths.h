// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef TOS_FLASH_PATHS_H
#define TOS_FLASH_PATHS_H

// LittleFS assets partition mount point
#define FLASH_MOUNT              "/assets"

// Config fallback paths (read-only)
#define FLASH_CONFIG_SCREEN      FLASH_MOUNT "/config/screen/screen_config.conf"
#define FLASH_CONFIG_THEMES      FLASH_MOUNT "/config/screen/screen_themes.conf"
#define FLASH_CONFIG_INTERFACE   FLASH_MOUNT "/config/screen/interface_config.conf"
#define FLASH_CONFIG_BRIGHTNESS  FLASH_MOUNT "/config/screen/brightness.conf"
#define FLASH_CONFIG_WIFI_AP     FLASH_MOUNT "/config/wifi/wifi_ap.conf"
#define FLASH_CONFIG_BLE         FLASH_MOUNT "/config/bluetooth/ble_announce.conf"
#define FLASH_CONFIG_LORA        FLASH_MOUNT "/config/lora/lora.conf"
#define FLASH_CONFIG_SYSTEM      FLASH_MOUNT "/config/system/system.conf"
#define FLASH_CONFIG_BUZZER      FLASH_MOUNT "/config/buzzer/buzzer.conf"
#define FLASH_CONFIG_BUZZER_DIR  FLASH_MOUNT "/config/buzzer/sounds"

// Storage paths (writable on flash)
#define FLASH_STORAGE_WIFI       FLASH_MOUNT "/storage/wifi"
#define FLASH_STORAGE_WIFI_BEACONS FLASH_MOUNT "/storage/wifi/beacon_list.json"
#define FLASH_STORAGE_BLE        FLASH_MOUNT "/storage/ble"
#define FLASH_STORAGE_BADUSB     FLASH_MOUNT "/storage/bad_usb_scripts"

// Captive portal HTML templates (flash fallback)
#define FLASH_CAPTIVE_TEMPLATES  FLASH_MOUNT "/html/captive_portal"

#endif // TOS_FLASH_PATHS_H
