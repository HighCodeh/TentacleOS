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
#define TOS_ABI_VERSION_MINOR 1

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

  // Command surfaces. host_link_* need TOS_CAP_HOSTLINK; console_* need TOS_CAP_CONSOLE.
  esp_err_t (*host_link_register)(const host_link_handler_t *h);
  esp_err_t (*host_link_unregister)(uint8_t category, uint8_t op_min);
  esp_err_t (*console_register)(const char *command,
                                const char *help,
                                int (*func)(int argc, char **argv));
  esp_err_t (*console_unregister)(const char *command);

  // Device surfaces (grow additively, minor-bumped). led_set needs TOS_CAP_UI.
  esp_err_t (*led_set)(uint8_t r, uint8_t g, uint8_t b);
} tos_api_t;

/** @brief The shared, const host API table handed to every app at entry. */
const tos_api_t *tos_api_get(void);

#ifdef __cplusplus
}
#endif

#endif // TOS_API_H
