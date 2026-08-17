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

#include "vfs_config.h"

// Flash storage lives on the active VFS backend (LittleFS, 'storage' partition).
#define FLASH_MOUNT VFS_MOUNT_POINT

// Config paths (read-only defaults)
#define FLASH_CONFIG_WIFI_AP      FLASH_MOUNT "/config/wifi/wifi_ap.conf"
#define FLASH_CONFIG_WIFI_BEACONS FLASH_MOUNT "/config/wifi/beacon_list.json"
#define FLASH_CONFIG_BLE_ANNOUNCE FLASH_MOUNT "/config/bluetooth/ble_announce.conf"
#define FLASH_CONFIG_BLE_SPAM     FLASH_MOUNT "/config/bluetooth/beacon_list.conf"
#define FLASH_CONFIG_CHAT         FLASH_MOUNT "/config/chat/chat.conf"
#define FLASH_CONFIG_CHAT_ADDRS   FLASH_MOUNT "/config/chat/addresses.conf"
#define FLASH_CONFIG_BUZZER       FLASH_MOUNT "/config/buzzer/buzzer.conf"
#define FLASH_CONFIG_BUZZER_DIR   FLASH_MOUNT "/config/buzzer/sounds"

// Storage paths (writable on flash)
#define FLASH_STORAGE_WIFI          FLASH_MOUNT "/storage/wifi"
#define FLASH_STORAGE_WIFI_PCAP     FLASH_MOUNT "/storage/wifi/pcap"
#define FLASH_STORAGE_WIFI_APS      FLASH_MOUNT "/storage/wifi/scanned_aps.json"
#define FLASH_STORAGE_WIFI_CLIENTS  FLASH_MOUNT "/storage/wifi/scanned_clients.json"
#define FLASH_STORAGE_WIFI_PROBES   FLASH_MOUNT "/storage/wifi/probe_monitor.json"
#define FLASH_STORAGE_WIFI_TARGETS  FLASH_MOUNT "/storage/wifi/target_scan.json"
#define FLASH_STORAGE_WIFI_NETWORKS FLASH_MOUNT "/storage/wifi/know_networks.json"
#define FLASH_STORAGE_BLE           FLASH_MOUNT "/storage/ble"
#define FLASH_STORAGE_BLE_DEVICES   FLASH_MOUNT "/storage/ble/scanned_devices.json"
#define FLASH_STORAGE_BLE_GATT      FLASH_MOUNT "/storage/ble/gatt_results.json"
#define FLASH_STORAGE_CAPTIVE       FLASH_MOUNT "/storage/captive_portal"
#define FLASH_STORAGE_CAPTIVE_PASS  FLASH_MOUNT "/storage/captive_portal/passwords.json"

// Captive portal HTML templates
#define FLASH_CAPTIVE_HTML_INDEX  "html/captive_portal/evil_twin_index.html"
#define FLASH_CAPTIVE_HTML_THANKS "html/captive_portal/evil_twin_thank_you.html"

#ifdef __cplusplus
}
#endif

#endif // TOS_FLASH_PATHS_H
