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

#ifndef TOS_STORAGE_PATHS_H
#define TOS_STORAGE_PATHS_H

#include "vfs_config.h"

// Protocol root paths
#define TOS_PATH_NFC              VFS_MOUNT_POINT "/nfc"
#define TOS_PATH_RFID             VFS_MOUNT_POINT "/rfid"
#define TOS_PATH_SUBGHZ           VFS_MOUNT_POINT "/subghz"
#define TOS_PATH_IR               VFS_MOUNT_POINT "/ir"
#define TOS_PATH_WIFI             VFS_MOUNT_POINT "/wifi"
#define TOS_PATH_BLE              VFS_MOUNT_POINT "/ble"
#define TOS_PATH_LORA             VFS_MOUNT_POINT "/lora"
#define TOS_PATH_BADUSB           VFS_MOUNT_POINT "/badusb"

// Protocol sub-paths (only what each protocol actually uses per doc)
#define TOS_PATH_NFC_ASSETS       TOS_PATH_NFC    "/assets"

#define TOS_PATH_RFID_ASSETS      TOS_PATH_RFID   "/assets"

#define TOS_PATH_SUBGHZ_ASSETS    TOS_PATH_SUBGHZ "/assets"

#define TOS_PATH_IR_ASSETS        TOS_PATH_IR     "/assets"

#define TOS_PATH_WIFI_ASSETS      TOS_PATH_WIFI   "/assets"
#define TOS_PATH_WIFI_LOOT        TOS_PATH_WIFI   "/loot"
#define TOS_PATH_WIFI_LOOT_HS     TOS_PATH_WIFI   "/loot/handshakes"
#define TOS_PATH_WIFI_LOOT_PCAPS  TOS_PATH_WIFI   "/loot/pcaps"
#define TOS_PATH_WIFI_LOOT_DEAUTH TOS_PATH_WIFI   "/loot/deauth_logs"

#define TOS_PATH_BLE_ASSETS       TOS_PATH_BLE    "/assets"
#define TOS_PATH_BLE_LOOT         TOS_PATH_BLE    "/loot"

#define TOS_PATH_LORA_ASSETS      TOS_PATH_LORA   "/assets"
#define TOS_PATH_LORA_LOOT        TOS_PATH_LORA   "/loot"
#define TOS_PATH_LORA_MSGS        TOS_PATH_LORA   "/messages"

#define TOS_PATH_BADUSB_ASSETS    TOS_PATH_BADUSB "/assets"

// Captive portal (lives inside wifi/)
#define TOS_PATH_CAPTIVE          TOS_PATH_WIFI    "/captive_portal"
#define TOS_PATH_CAPTIVE_TMPL     TOS_PATH_CAPTIVE "/templates"

// Configuration
#define TOS_PATH_CONFIG_DIR       VFS_MOUNT_POINT  "/config"
#define TOS_PATH_CONFIG_SCREEN    TOS_PATH_CONFIG_DIR "/screen.conf"
#define TOS_PATH_CONFIG_WIFI      TOS_PATH_CONFIG_DIR "/wifi.conf"
#define TOS_PATH_CONFIG_BLE       TOS_PATH_CONFIG_DIR "/ble.conf"
#define TOS_PATH_CONFIG_LORA      TOS_PATH_CONFIG_DIR "/lora.conf"
#define TOS_PATH_CONFIG_SYSTEM    TOS_PATH_CONFIG_DIR "/system.conf"

// System paths
#define TOS_PATH_FAVORITES        VFS_MOUNT_POINT  "/favorites.json"
#define TOS_PATH_THEMES           VFS_MOUNT_POINT  "/themes"
#define TOS_PATH_RINGTONES        VFS_MOUNT_POINT  "/ringtones"
#define TOS_PATH_APPS             VFS_MOUNT_POINT  "/apps"
#define TOS_PATH_APPS_DATA        VFS_MOUNT_POINT  "/apps_data"
#define TOS_PATH_SCRIPTS          VFS_MOUNT_POINT  "/scripts"
#define TOS_PATH_LOGS             VFS_MOUNT_POINT  "/logs"
#define TOS_PATH_BACKUP           VFS_MOUNT_POINT  "/backup"
#define TOS_PATH_CACHE            VFS_MOUNT_POINT  "/cache"
#define TOS_PATH_UPDATE           VFS_MOUNT_POINT  "/update"

// First boot marker
#define TOS_PATH_SETUP_MARKER     VFS_MOUNT_POINT  "/.setup_done"

#endif // TOS_STORAGE_PATHS_H
