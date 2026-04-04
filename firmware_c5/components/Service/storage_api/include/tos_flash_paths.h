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

#define FLASH_MOUNT                  "/assets"

// Config paths (read-only defaults)
#define FLASH_CONFIG_WIFI_AP         FLASH_MOUNT "/config/wifi/wifi_ap.conf"
#define FLASH_CONFIG_WIFI_BEACONS    FLASH_MOUNT "/config/wifi/beacon_list.json"
#define FLASH_CONFIG_BLE_ANNOUNCE    FLASH_MOUNT "/config/bluetooth/ble_announce.conf"
#define FLASH_CONFIG_BLE_SPAM        FLASH_MOUNT "/config/bluetooth/beacon_list.conf"
#define FLASH_CONFIG_CHAT            FLASH_MOUNT "/config/chat/chat.conf"
#define FLASH_CONFIG_CHAT_ADDRS      FLASH_MOUNT "/config/chat/addresses.conf"
#define FLASH_CONFIG_BUZZER          FLASH_MOUNT "/config/buzzer/buzzer.conf"
#define FLASH_CONFIG_BUZZER_DIR      FLASH_MOUNT "/config/buzzer/sounds"

// Storage paths (writable on flash)
#define FLASH_STORAGE_WIFI           FLASH_MOUNT "/storage/wifi"
#define FLASH_STORAGE_WIFI_PCAP      FLASH_MOUNT "/storage/wifi/pcap"
#define FLASH_STORAGE_WIFI_APS       FLASH_MOUNT "/storage/wifi/scanned_aps.json"
#define FLASH_STORAGE_WIFI_CLIENTS   FLASH_MOUNT "/storage/wifi/scanned_clients.json"
#define FLASH_STORAGE_WIFI_PROBES    FLASH_MOUNT "/storage/wifi/probe_monitor.json"
#define FLASH_STORAGE_WIFI_TARGETS   FLASH_MOUNT "/storage/wifi/target_scan.json"
#define FLASH_STORAGE_WIFI_NETWORKS  FLASH_MOUNT "/storage/wifi/know_networks.json"
#define FLASH_STORAGE_BLE            FLASH_MOUNT "/storage/ble"
#define FLASH_STORAGE_BLE_DEVICES    FLASH_MOUNT "/storage/ble/scanned_devices.json"
#define FLASH_STORAGE_BLE_GATT       FLASH_MOUNT "/storage/ble/gatt_results.json"
#define FLASH_STORAGE_CAPTIVE        FLASH_MOUNT "/storage/captive_portal"
#define FLASH_STORAGE_CAPTIVE_PASS   FLASH_MOUNT "/storage/captive_portal/passwords.json"

// Captive portal HTML templates
#define FLASH_CAPTIVE_HTML_INDEX     "html/captive_portal/evil_twin_index.html"
#define FLASH_CAPTIVE_HTML_THANKS    "html/captive_portal/evil_twin_thank_you.html"

#endif // TOS_FLASH_PATHS_H
