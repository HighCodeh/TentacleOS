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

// The stable ABI between TentacleOS and a loaded app. An app links against
// nothing in the firmware: it receives one const tos_api_t* at entry and reaches
// every device function through it. This header is the whole contract and the
// one thing code review must guard forever.
//
// Compatibility rules:
//   - MAJOR breaks. An app requiring a different major is refused at load.
//   - MINOR is additive only. New calls are appended and bump the minor; an app
//     may require firmware minor >= the one it was built against.
//   - Never reorder or remove a field within a major version.

#ifndef TOS_API_H
#define TOS_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
// Firmware build (inside ESP-IDF): use the real types.
#include "esp_err.h"
#include "host_link_dispatch.h"
#else
// Standalone SDK build (app compiled on a PC, no ESP-IDF): self-contained.
typedef int esp_err_t;
typedef struct host_link_handler host_link_handler_t; // opaque to apps
#endif

#define TOS_ABI_VERSION_MAJOR 1
#define TOS_ABI_VERSION_MINOR 4

/** Log levels; values match esp_log_level_t so they pass straight through. */
typedef enum {
  TOS_LOG_NONE = 0,
  TOS_LOG_ERROR = 1,
  TOS_LOG_WARN = 2,
  TOS_LOG_INFO = 3,
  TOS_LOG_DEBUG = 4,
  TOS_LOG_VERBOSE = 5,
} tos_log_level_t;

/**
 * Capabilities an app requests in its manifest and the API enforces per call.
 * The loader grants only what the manifest asked for and the user approved; a
 * gated call by an app without the matching grant returns ESP_ERR_NOT_SUPPORTED.
 */
typedef enum {
  TOS_CAP_FS_READ = 1u << 0,
  TOS_CAP_FS_WRITE = 1u << 1,
  TOS_CAP_RADIO_TX = 1u << 2, // WiFi/BT/SubGHz/LoRa/IR transmit
  TOS_CAP_RADIO_RX = 1u << 3,
  TOS_CAP_HID = 1u << 4,     // BadUSB / HID injection
  TOS_CAP_UI = 1u << 5,      // screen / LED / on-device UI
  TOS_CAP_HOSTLINK = 1u << 6, // register companion (host_link) commands
  TOS_CAP_CONSOLE = 1u << 7,  // register console commands
} tos_cap_t;

// --- Device subsystems -----------------------------------------------------
// Each subsystem is its own sub-struct reached via a pointer in tos_api_t
// (`api->wifi->scan()`). This is the pattern for growing the ABI: add a new
// `tos_<subsystem>_api_t` and one pointer here; existing apps are unaffected.

/** One access point from a scan. */
typedef struct {
  char ssid[33];
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t authmode; ///< esp-idf wifi_auth_mode_t value
} tos_wifi_ap_t;

/**
 * WiFi. Discovery + station control run on the P4; monitors and attacks run on
 * the C5 radio. Reads are ungated; scan/monitor need `radio-rx`; connect and
 * anything that transmits (attacks, channel hop) need `radio-tx`. The session
 * calls return a session id; end them with `session_stop`.
 */
typedef struct tos_wifi_api {
  esp_err_t (*scan)(void); ///< blocks until the scan finishes (radio-rx)
  int (*ap_count)(void);
  esp_err_t (*ap_get)(int index, tos_wifi_ap_t *out);
  bool (*is_connected)(void);
  const char *(*connected_ssid)(void);
  esp_err_t (*connect)(const char *ssid, const char *password); ///< radio-tx
  esp_err_t (*disconnect)(void);                                ///< radio-tx

  esp_err_t (*sniffer_start)(uint8_t type, uint8_t channel, bool monitor, uint32_t *out_session);
  esp_err_t (*deauth_detect)(uint32_t *out_session);                             ///< radio-rx
  esp_err_t (*probe_monitor)(uint32_t *out_session);                             ///< radio-rx
  esp_err_t (*signal_monitor)(const uint8_t bssid[6], uint8_t channel, uint32_t *out_session);

  esp_err_t (*deauth)(const uint8_t bssid[6], const uint8_t client[6], uint8_t type,
                      uint8_t channel, uint32_t *out_session);            ///< radio-tx
  esp_err_t (*deauth_broadcast)(const uint8_t bssid[6], uint8_t type, uint8_t channel);
  esp_err_t (*flood)(uint8_t type, const uint8_t bssid[6], uint8_t channel, uint32_t *out_session);
  esp_err_t (*beacon_spam)(const char *ssid_list_path, uint32_t *out_session); ///< NULL = random
  esp_err_t (*evil_twin)(const char *ssid, uint32_t *out_session);

  esp_err_t (*session_stop)(uint32_t session); ///< stop any session above
  esp_err_t (*channel_hop)(bool enable);       ///< radio-tx
} tos_wifi_api_t;

/** One BLE device from a scan. */
typedef struct {
  char name[32];
  uint8_t addr[6];
  uint8_t addr_type; ///< NimBLE address type: 0 public, 1 random
  int8_t rssi;
} tos_ble_device_t;

/**
 * BLE. The C5 serialises the whole Bluetooth host, so BLE is one-owner: a call
 * fails with ESP_ERR_INVALID_STATE (BUSY) when the companion BLE link or another
 * app holds the radio. Scan/monitors need `radio-rx`; connect/MAC/adv/spam/flood
 * need `radio-tx`; the HID keyboard (BadBLE) needs `hid`. The session calls
 * return a session id ended with `session_stop`.
 */
typedef struct tos_ble_api {
  esp_err_t (*scan)(uint32_t duration_ms); ///< blocks until the scan finishes (radio-rx)
  int (*device_count)(void);
  esp_err_t (*device_get)(int index, tos_ble_device_t *out);

  esp_err_t (*connect)(const uint8_t addr[6], uint8_t addr_type); ///< radio-tx
  esp_err_t (*disconnect)(void);
  esp_err_t (*set_random_mac)(void); ///< MAC spoof (radio-tx)
  esp_err_t (*adv_start)(void);      ///< radio-tx
  esp_err_t (*adv_stop)(void);

  esp_err_t (*sniffer_start)(uint32_t *out_session);              ///< radio-rx
  esp_err_t (*spam)(uint8_t attack_index, uint32_t *out_session); ///< radio-tx (see SDK spam table)
  esp_err_t (*flood)(const uint8_t addr[6], uint8_t addr_type, uint32_t *out_session); ///< radio-tx
  esp_err_t (*skimmer_detect)(uint32_t *out_session);             ///< radio-rx
  esp_err_t (*tracker_detect)(uint32_t *out_session);             ///< radio-rx (tracker/AirTag detector)
  esp_err_t (*session_stop)(uint32_t session);

  // BLE HID keyboard (BadBLE). hid_start advertises a HID peripheral and holds
  // the radio until hid_stop; send keys once a target connects.
  esp_err_t (*hid_start)(void); ///< hid
  esp_err_t (*hid_stop)(void);
  bool (*hid_is_connected)(void);
  esp_err_t (*hid_send_key)(uint8_t modifier, uint8_t keycode); ///< hid
} tos_ble_api_t;

/**
 * The host API table. Built once and shared const by every app. Grouped by the
 * capability that gates each call; the lifecycle group is always available.
 */
typedef struct tos_api {
  uint32_t abi_major;
  uint32_t abi_minor;

  // Lifecycle / OS. Always available (no capability required).
  void (*log)(tos_log_level_t level, const char *tag, const char *fmt, ...);
  void *(*mem_alloc)(size_t size);
  void (*mem_free)(void *ptr);
  void (*delay_ms)(uint32_t ms);
  void (*yield)(void);      // cooperative yield that also feeds the task watchdog
  int (*should_stop)(void); // nonzero once the manager asked this app to exit
  // nonzero once a radio this app held was preempted by the UI/companion; the
  // app's C5 session is already stopped, so stop using it. Cleared on the next
  // successful radio acquire. Sibling of should_stop.
  int (*resource_lost)(void);

  // Command surfaces. host_link_* need TOS_CAP_HOSTLINK; console_* need TOS_CAP_CONSOLE.
  esp_err_t (*host_link_register)(const host_link_handler_t *h);
  esp_err_t (*host_link_unregister)(uint8_t category, uint8_t op_min);
  esp_err_t (*console_register)(const char *command,
                                const char *help,
                                int (*func)(int argc, char **argv));
  esp_err_t (*console_unregister)(const char *command);

  // Device surfaces (grow additively, minor-bumped). led_set needs TOS_CAP_UI.
  esp_err_t (*led_set)(uint8_t r, uint8_t g, uint8_t b);

  // Subsystem namespaces (each its own sub-struct; per-call capability gating).
  const tos_wifi_api_t *wifi;
  const tos_ble_api_t *ble;
} tos_api_t;

/** @brief The shared, const host API table handed to every app at entry. */
const tos_api_t *tos_api_get(void);

#ifdef __cplusplus
}
#endif

#endif // TOS_API_H
