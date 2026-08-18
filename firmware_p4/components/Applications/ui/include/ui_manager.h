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

#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "lvgl.h"
#include "input_manager.h"

/** @brief Screen identifiers for the UI navigation system. */
typedef enum {
  SCREEN_NONE,
  SCREEN_HOME,
  SCREEN_MENU,
  SCREEN_WIFI_MENU,
  SCREEN_WIFI_ATTACK_MENU,
  SCREEN_WIFI_DEAUTH_ATTACK,
  SCREEN_WIFI_DEAUTH_UNICAST,
  SCREEN_WIFI_BEACON_SPAM_SIMPLE,
  SCREEN_WIFI_PROBE_FLOOD,
  SCREEN_WIFI_PROBE_FLOOD_BROADCAST,
  SCREEN_WIFI_PROBE_FLOOD_UNICAST,
  SCREEN_WIFI_AUTH_FLOOD,
  SCREEN_WIFI_PACKETS_MENU,
  SCREEN_WIFI_SNIFFER_RAW,
  SCREEN_WIFI_SNIFFER_ATTACK,
  SCREEN_WIFI_SNIFFER_HANDSHAKE,
  SCREEN_WIFI_SCAN_MENU,
  SCREEN_WIFI_SCAN_AP,
  SCREEN_WIFI_SCAN_STATIONS,
  SCREEN_WIFI_SCAN_TARGET,
  SCREEN_WIFI_SCAN_PROBE,
  SCREEN_WIFI_SCAN_MONITOR,
  SCREEN_WIFI_SCAN,
  SCREEN_WIFI_AP_LIST,
  SCREEN_WIFI_DEAUTH,
  SCREEN_WIFI_EVIL_TWIN,
  SCREEN_WIFI_BEACON_SPAM,
  SCREEN_WIFI_PROBE,
  SCREEN_BLE_MENU,
  SCREEN_BLE_SPAM_SELECT,
  SCREEN_BLE_SPAM,
  SCREEN_BADUSB_MENU,
  SCREEN_BADUSB_BROWSER,
  SCREEN_BADUSB_LAYOUT,
  SCREEN_BADUSB_CONNECT,
  SCREEN_BADUSB_RUNNING,
  SCREEN_SUBGHZ_SPECTRUM,
  SCREEN_SETTINGS,
  SCREEN_DISPLAY_SETTINGS,
  SCREEN_INTERFACE_SETTINGS,
  SCREEN_SOUND_SETTINGS,
  SCREEN_BATTERY_SETTINGS,
  SCREEN_CONNECTION_SETTINGS,
  SCREEN_CONNECT_WIFI,
  SCREEN_CONNECT_BLUETOOTH,
  SCREEN_COMPANION_PAIRING,
  SCREEN_ABOUT_SETTINGS,
  SCREEN_STORAGE,
  SCREEN_NFC_MENU,
  SCREEN_FILES,
  SCREEN_THEME_SELECTOR,
  SCREEN_IR_MENU,
  SCREEN_IR_RECEIVE,
  SCREEN_IR_SEND,
  SCREEN_IR_CONTROLLER,
  SCREEN_IR_SAVED,
  SCREEN_IR_BURST,
  SCREEN_OCTOBIT_STATUS,
  SCREEN_DEV_MENU,
  SCREEN_GAMES_MENU,
  SCREEN_GAME_SNAKE,
  SCREEN_GAME_BREAKOUT,
  SCREEN_GAME_GB,
  SCREEN_GPIO,
  SCREEN_HAPTIC,
  SCREEN_SPEAKER,
  SCREEN_MIC_REC,
  SCREEN_WAV_PLAYER,
  SCREEN_MP4_PLAYER,
  SCREEN_MP3_PLAYER,
  SCREEN_PLAYER,
  SCREEN_SUBGHZ_MENU,
  SCREEN_SUBGHZ_READ,
  SCREEN_NFC_READ,
  SCREEN_NFC_SAVED,
  SCREEN_NFC_WRITE,
  SCREEN_NFC_EMULATE,
  SCREEN_NFC_CONFIG,
  SCREEN_CARD_EMU,
  SCREEN_RFID_MENU,
  SCREEN_LORA_CHAT,
  SCREEN_POWER,
  SCREEN_SPECTRUM,
  SCREEN_IR_REMOTE_TYPE,
  SCREEN_BLE_SCAN,
  SCREEN_BLE_MOUSE_PAIRING,
  SCREEN_BLE_MOUSE,
  SCREEN_WIFI_CHANNELS,
  SCREEN_WIFI_CLIENTS,
  SCREEN_WIFI_NAMES,
  SCREEN_APPS,
  SCREEN_SETTINGS_DEV,
  SCREEN_SUBGHZ_BRUTE,
  SCREEN_BLE_BEACON_SPAM,
  SCREEN_BLE_DETECT_MENU,
  SCREEN_BLE_SNIFFER,
  SCREEN_BLE_TRACKER,
  SCREEN_BLE_SKIMMER,
  SCREEN_BLE_EXPOSURE,
  SCREEN_GATT_EXPLORER,
  SCREEN_BLE_KEYBOARD,
  SCREEN_BLE_FLOOD,
  SCREEN_BLE_RADIO,
  SCREEN_BLE_TRACK_DEVICE,
  SCREEN_BLE_SPAM_NAMES,
  SCREEN_WIFI_PORT_SCAN,
  SCREEN_WIFI_PROBE_MON,
  SCREEN_WIFI_TARGET_CLIENTS,
  SCREEN_WIFI_DEAUTH_DETECTOR,
  SCREEN_WIFI_SIGNAL_LOCATOR,
  SCREEN_NFC_SCAN,
  SCREEN_SUBGHZ_SEND,
  SCREEN_SYSTEM_UPDATE,
  SCREEN_C5_STATUS,
  SCREEN_LED_CTRL,
  SCREEN_LORA_TRACEROUTE,
  SCREEN_LORA_RNODE,
  SCREEN_LORA_MQTT,
  SCREEN_SCRIPTS,
  SCREEN_DEV_CONSOLE,
  SCREEN_DEV_DIAG,
  SCREEN_BOOT_MAP,
  SCREEN_CRASH_REPORT,
  SCREEN_NFC_BANKCARD,
  SCREEN_NFC_DESFIRE,
  SCREEN_NFC_ISO15693,
  SCREEN_NFC_ULTRALIGHT,
  SCREEN_NFC_NDEF,
  SCREEN_NFC_FELICA,
  SCREEN_NFC_P2P,
  SCREEN_NFC_KEYDICT,
  SCREEN_SUBGHZ_CONFIG,
  SCREEN_IR_RAW,
  SCREEN_LORA_CHANNELS,
  SCREEN_LORA_POSITION,
  SCREEN_LORA_TELEMETRY,
  SCREEN_LORA_SECURE_DM,
  SCREEN_WIFI_HANDSHAKE,
  SCREEN_WIFI_HOTSPOT,
  SCREEN_IMU_MONITOR,
  SCREEN_SD_HEALTH,
  SCREEN_USB_MOUSE,
  SCREEN_TIME,
  SCREEN_IMAGE_VIEWER,
  SCREEN_USB_STORAGE,
  SCREEN_COUNT
} screen_id_t;

/** @brief Initialize the UI manager and start the UI task. */
void ui_init(void);

/**
 * @brief Bring up a minimal UI showing only the safe-mode recovery screen.
 *
 * Used when the OK + BACK boot combo is held. Sets up the LVGL infrastructure
 * (theme, input pump, render heartbeat) but skips the normal boot animation,
 * home screen and power policy. Radios, custom themes and SD assets are not
 * initialized by the caller in this mode.
 */
void ui_init_safe_mode(void);

/** @brief Acquire the UI mutex for thread-safe LVGL access. */
bool ui_acquire(void);

/** @brief Release the UI mutex. */
void ui_release(void);

/**
 * @brief Schedule an lv_async_call safely from any thread.
 *
 * lv_async_call() creates an lv_timer under the hood, mutating LVGL's global
 * timer list. Called from a non-LVGL thread (radio/bridge worker tasks) it races
 * lv_timer_handler on the render thread. This wrapper takes the (recursive) UI
 * mutex first, so it is safe to call from any context; it silently drops the
 * call only if the lock cannot be taken within the UI timeout.
 */
void ui_async_call(lv_async_cb_t cb, void *user_data);

/** @brief Switch to a new screen by identifier. */
void ui_switch_screen(screen_id_t new_screen);

/**
 * @brief Load a screen. Drop-in replacement for lv_screen_load used by the
 *        ported screens.
 *
 * @param scr  Screen object to load.
 */
void ui_screen_load(lv_obj_t *scr);

/**
 * @brief Load a screen and bind @p slot to it. When the screen object is later
 *        deleted (freed on navigation), @p slot is set to NULL so the owning
 *        screen never double-frees or dereferences a stale pointer.
 *
 * @param slot  Address of the screen's own object pointer (e.g. &s_screen).
 * @param scr   Screen object to load.
 */
void ui_screen_load_owned(lv_obj_t **slot, lv_obj_t *scr);

/** @brief Returns the currently active screen id. */
screen_id_t ui_current_screen(void);

/**
 * @brief Rebuild the active screen so it re-lays-out at the current logical
 *        resolution. Call after a rotation change to reflow it immediately.
 */
void ui_relayout_current_screen(void);

/**
 * @brief Whether a screen shows the global chrome (status bar + dropdown).
 *
 * false for active "operation" screens — reading/sending/scanning/emulating/
 * recording/playing/running — where the status bar and the quick-settings
 * dropdown are hidden; true for browse screens (menus, lists, settings). Used
 * both to gate the chrome header's status cluster and to gate the global
 * dropdown's long-press-to-open.
 */
bool ui_screen_shows_chrome(screen_id_t s);

/** @brief Re-open the active screen (no-op here: no runtime rotation). */
void ui_manager_relayout_current(void);

/** @brief Rotation-aware button polling (portrait pass-through on this build). */
bool ui_btn_up(void);
bool ui_btn_down(void);
bool ui_btn_left(void);
bool ui_btn_right(void);

/** @brief Handler a screen registers to receive input events. */
typedef void (*ui_input_handler_t)(const input_event_t *ev, void *ctx);

/**
 * @brief Register the active screen's input handler (event-driven input).
 *
 * The UI manager runs one pump that drains input_manager events and dispatches
 * them to this handler, but only while input is not locked and no modal overlay
 * (msgbox/keyboard) is up. A screen sets its handler in its open function; it is
 * cleared automatically on the next screen switch. This replaces the per-screen
 * polling lv_timers. Pass NULL to clear.
 */
void ui_input_set_screen_handler(ui_input_handler_t handler, void *ctx);

/** @brief Check if user input is temporarily locked. */
bool ui_input_is_locked(void);

/**
 * @brief Guard for actions that persist to the SD card. Returns true if the SD
 *        is mounted; otherwise shows a warning toast ("Insert SD card") and
 *        returns false, so a save can be skipped cleanly.
 */
bool ui_sd_ready(void);

/**
 * @brief Temporarily lock user input for @p ms milliseconds.
 *
 * Screens that poll buttons gate on ui_input_is_locked(), so this swallows
 * stray presses — e.g. the button that wakes the display from sleep should not
 * also navigate. Extends (never shortens) any lock already in effect.
 */
void ui_input_lock(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif // UI_MANAGER_H
