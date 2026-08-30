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
// The subset of esp_err.h codes the ABI can return (values match ESP-IDF).
#define ESP_OK                  0
#define ESP_FAIL                -1
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
#define ESP_ERR_INVALID_SIZE    0x104
#define ESP_ERR_NOT_FOUND       0x105
#define ESP_ERR_NOT_SUPPORTED   0x106
#define ESP_ERR_TIMEOUT         0x107
#define ESP_ERR_INVALID_VERSION 0x10A
#endif

#define TOS_ABI_VERSION_MAJOR 1
// 1.9 = ui->image (retained image widget from app-provided LVGL .bin bytes).
// 1.8 = read-only fs subsystem. NOTE: 1.7 was briefly a (removed) IMU subsystem in
// dev; fs took a fresh minor so a firmware built during the IMU era rejects an
// fs app at the load gate instead of mismatching its api->fs pointer.
#define TOS_ABI_VERSION_MINOR 9

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

// UI subsystem (api->ui): create and drive a full-screen widget UI on the 240x320
// display. Optional — an app that never touches api->ui owns no screen. Every call
// needs TOS_CAP_UI. No LVGL type crosses this ABI: widgets are opaque handles,
// geometry is int, colour is packed 0x00RRGGBB. The display is single-owner: while
// one app holds the screen, another app's screen_open returns ESP_ERR_INVALID_STATE.
typedef uint32_t tos_ui_obj_t;        ///< opaque widget handle; 0 == none/error
#define TOS_UI_NONE ((tos_ui_obj_t)0) ///< the "no widget" / root-parent sentinel
typedef uint32_t tos_rgb_t;           ///< 0x00RRGGBB (firmware packs to the panel)

typedef enum {
  TOS_KEY_UP = 1,
  TOS_KEY_DOWN,
  TOS_KEY_LEFT,
  TOS_KEY_RIGHT,
  TOS_KEY_OK,
  TOS_KEY_BACK,
} tos_ui_key_t;
typedef enum { TOS_KEY_RELEASE = 0, TOS_KEY_PRESS = 1, TOS_KEY_REPEAT = 2 } tos_ui_action_t;
typedef struct {
  uint8_t key;    ///< tos_ui_key_t
  uint8_t action; ///< tos_ui_action_t
} tos_ui_event_t;

/** Runs on the app task with the LVGL lock already held (see batch). Keep it
 *  short and non-blocking: no delay/yield/should_stop, no SD/SPI I/O. */
typedef void (*tos_ui_batch_cb)(void *user);

typedef struct tos_ui_api {
  // Screen lifecycle. Opt-in: nothing exists until screen_open().
  esp_err_t (*screen_open)(void); ///< create + show the full-bleed 240x320 screen
  void (*screen_close)(void);     ///< dismiss now (a normal return / kill also reclaims it)
  void (*metrics)(int32_t *w, int32_t *h); ///< usable size in px (240x320)

  // Retained builders. parent == TOS_UI_NONE attaches to the screen root. Return a
  // handle to keep, or TOS_UI_NONE on error. Strings are COPIED by the firmware.
  tos_ui_obj_t (*label)(tos_ui_obj_t parent, int32_t x, int32_t y, const char *text);
  tos_ui_obj_t (*rect)(tos_ui_obj_t parent, int32_t x, int32_t y, int32_t w, int32_t h);
  tos_ui_obj_t (*bar)(tos_ui_obj_t parent, int32_t x, int32_t y, int32_t w, int32_t h,
                      int32_t min, int32_t max);

  // Retained mutators. Call anytime from the app loop; each locks internally and
  // is a harmless no-op on a stale/invalid handle.
  esp_err_t (*set_text)(tos_ui_obj_t h, const char *text); ///< label; copies the string
  esp_err_t (*set_value)(tos_ui_obj_t h, int32_t v);       ///< bar
  esp_err_t (*set_pos)(tos_ui_obj_t h, int32_t x, int32_t y);
  esp_err_t (*set_size)(tos_ui_obj_t h, int32_t w, int32_t ht);
  esp_err_t (*set_text_color)(tos_ui_obj_t h, tos_rgb_t rgb);
  esp_err_t (*set_bg_color)(tos_ui_obj_t h, tos_rgb_t rgb);
  esp_err_t (*set_hidden)(tos_ui_obj_t h, bool hidden);
  esp_err_t (*del)(tos_ui_obj_t h); ///< delete a widget and its descendants

  // Atomic frame: take the LVGL lock once, run cb (app task, lock held), release.
  // Cheaper than N separate setters for a periodic redraw; no mid-frame repaint.
  esp_err_t (*batch)(tos_ui_batch_cb cb, void *user);

  // Input: non-blocking drain on the app task. 1 = event dequeued, 0 = none.
  int (*poll_event)(tos_ui_event_t *out);

  // Canvas (direct-panel) mode (ABI 1.6) — for games/emulators that push full
  // frames. Mutually exclusive with the widget builders on the same session: after
  // canvas_begin(), label/rect/bar return TOS_UI_NONE. canvas_begin() reports the
  // physical panel size (240x320, always portrait). canvas_blit() copies a
  // little-endian RGB565 rect (row-major, stride == w) to (x,y); the firmware owns
  // byte-order and DMA. canvas_end() (or a normal return / kill) reclaims it.
  esp_err_t (*canvas_begin)(int32_t *w, int32_t *h);
  esp_err_t (*canvas_blit)(const void *rgb565, int32_t x, int32_t y, int32_t w, int32_t h);
  void (*canvas_end)(void);

  // Retained image widget (ABI 1.9). Decodes an LVGL .bin (the same format the
  // asset tooling and the .hb icon use: [magic_cf u32][w u16][h u16][pixels]) that
  // the app carries in its own image, copies the pixels into firmware memory and
  // shows them at (x,y). Widget-mode only (TOS_UI_NONE in canvas mode). Bounded to
  // the 240x320 panel; returns TOS_UI_NONE on a bad or oversized blob. The copy is
  // freed with the widget, so the app's bytes need not outlive the call.
  tos_ui_obj_t (*image)(tos_ui_obj_t parent, int32_t x, int32_t y, const void *bin,
                        uint32_t bin_len);
} tos_ui_api_t;

/** One directory entry from tos_fs_api.list(). */
typedef struct {
  char name[64];  ///< entry name only (no path)
  uint32_t size;  ///< file size in bytes (0 for directories)
  bool is_dir;    ///< true if a subdirectory
} tos_fs_dirent_t;

/**
 * Read-only filesystem access to the SD card (ABI 1.8). Needs TOS_CAP_FS_READ.
 * Every path must be absolute under "/sdcard" (others are refused). Provides
 * directory listing and streaming file reads; there is no write path.
 */
typedef struct tos_fs_api {
  int (*list)(const char *path, tos_fs_dirent_t *out, int max); ///< entry count, or <0
  long (*size)(const char *path);                               ///< bytes, or -1 if missing
  void *(*open)(const char *path);                              ///< read handle, or NULL
  int (*read)(void *handle, void *buf, int len);                ///< bytes read, 0=EOF, <0 err
  int (*seek)(void *handle, long offset);                       ///< absolute seek; 0 ok, <0 err
  void (*close)(void *handle);
} tos_fs_api_t;

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
  const tos_ui_api_t *ui; ///< on-device screen (minor 5+); needs TOS_CAP_UI
  const tos_fs_api_t *fs; ///< read-only SD access (minor 8+); needs TOS_CAP_FS_READ
} tos_api_t;

/** @brief The shared, const host API table handed to every app at entry. */
const tos_api_t *tos_api_get(void);

#ifdef __cplusplus
}
#endif

#endif // TOS_API_H
