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

#include "ui_manager.h"

#include "core/lv_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lvgl.h"
#include "sys_prio.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "dropdown_ui.h"
#include "keyboard_ui.h"
#include "lvgl_glue.h"
#include "lv_port_indev.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "power_policy.h"
#include "screen_tips.h"
#include "tutorial_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_theme.h"
#include "ui_liveness.h"
#include "vfs_sdcard.h"

#include "boot_ui.h"
#include "safe_mode_ui.h"
#include "boot_map_ui.h"
#include "crash_report_ui.h"
#include "home_ui.h"
#include "menu_ui.h"
#include "wifi_ui.h"
#include "wifi_channel_ui.h"
#include "wifi_client_ui.h"
#include "wifi_scan_ui.h"
#include "wifi_names_ui.h"
#include "connect_wifi_ui.h"
#include "ui_ble_menu.h"
#include "ble_scan_ui.h"
#include "ble_mouse_ui.h"
#include "connect_bt_ui.h"
#include "nfc_menu_ui.h"
#include "nfc_emulate_ui.h"
#include "nfc_read_ui.h"
#include "nfc_manager.h"
#include "subghz_receiver.h"
#include "nfc_saved_ui.h"
#include "nfc_write_ui.h"
#include "nfc_config_ui.h"
#include "card_emu_ui.h"
#include "power_ui.h"
#include "settings_ui.h"
#include "connection_settings_ui.h"
#include "ir_menu_ui.h"
#include "ir_send_ui.h"
#include "ir_receive_ui.h"
#include "ir_controller_ui.h"
#include "ir_remote_type_ui.h"
#include "ir_saved_ui.h"
#include "ir_burst_ui.h"
#include "haptic_ui.h"
#include "speaker_ui.h"
#include "micrec_ui.h"
#include "mp3_player_ui.h"
#include "mp4_player_ui.h"
#include "wav_player_ui.h"
#include "image_viewer_ui.h"
#include "usb_storage_ui.h"
#include "wav_library_ui.h"
#include "spectrum_ui.h"
#include "lora_chat_ui.h"
#include "games_menu_ui.h"
#include "snake_ui.h"
#include "breakout_ui.h"
#include "gb_ui.h"
#include "octobit_status_ui.h"
#include "dev_menu_ui.h"
#include "subghz_menu_ui.h"
#include "subghz_brute_ui.h"
#include "rfid_menu_ui.h"
#include "ble_beacon_ui.h"
#include "ble_detect_ui.h"
#include "ble_sniffer_ui.h"
#include "ble_tracker_ui.h"
#include "ble_skimmer_ui.h"
#include "ble_exposure_ui.h"
#include "gatt_explorer_ui.h"
#include "ble_keyboard_ui.h"
#include "ble_flood_ui.h"
#include "ble_radio_ui.h"
#include "ble_track_device_ui.h"
#include "ble_spam_names_ui.h"
#include "wifi_port_scan_ui.h"
#include "wifi_probe_mon_ui.h"
#include "wifi_target_clients_ui.h"
#include "wifi_deauth_detector_ui.h"
#include "wifi_signal_locator_ui.h"
#include "nfc_scan_ui.h"
#include "subghz_send_ui.h"
#include "system_update_ui.h"
#include "c5_status_ui.h"
#include "led_ctrl_ui.h"
#include "lora_traceroute_ui.h"
#include "lora_rnode_ui.h"
#include "lora_mqtt_ui.h"
#include "scripts_ui.h"
#include "dev_console_ui.h"
#include "dev_diag_ui.h"
#include "nfc_bankcard_ui.h"
#include "nfc_desfire_ui.h"
#include "nfc_iso15693_ui.h"
#include "nfc_ultralight_ui.h"
#include "nfc_ndef_ui.h"
#include "nfc_felica_ui.h"
#include "nfc_p2p_ui.h"
#include "nfc_keydict_ui.h"
#include "subghz_config_ui.h"
#include "ir_raw_ui.h"
#include "lora_channels_ui.h"
#include "lora_position_ui.h"
#include "lora_telemetry_ui.h"
#include "lora_securedm_ui.h"
#include "wifi_handshake_ui.h"
#include "wifi_hotspot_ui.h"
#include "imu_monitor_ui.h"
#include "sd_health_ui.h"
#include "usb_mouse_ui.h"
#include "badusb_menu_ui.h"
#include "gpio_ui.h"
#include "files_ui.h"
#include "wifi_attack_ui.h"
#include "wifi_packets_ui.h"
#include "wifi_evil_twin_ui.h"
#include "ble_companion_ui.h"
#include "ble_spam_ui.h"
#include "display_settings_ui.h"
#include "interface_settings_ui.h"
#include "sound_settings_ui.h"
#include "battery_settings_ui.h"
#include "about_settings_ui.h"
#include "storage_settings_ui.h"
#include "theme_selector_ui.h"
#include "time_settings_ui.h"

static const char *TAG = "UI_MANAGER";

#define UI_BOOT_TASK_STACK_SIZE (4096 * 4)
#define UI_BOOT_TASK_PRIORITY   SYS_PRIO_SERVICE_LO
#define UI_BOOT_TASK_CORE       SYS_CORE_UI
#define INPUT_LOCK_MS           500
#define BOOT_SPLASH_DURATION_MS 5000
#define RENDER_BEAT_MS          250
#define UI_INPUT_PUMP_MS        20
#define UI_LOCK_TIMEOUT_MS      1000

static uint32_t input_lock_until = 0;
static volatile uint32_t s_render_beat = 0;

static void ui_boot_task(void *pvParameter);
static void clear_current_screen(void);

screen_id_t current_screen_id = SCREEN_NONE;

uint32_t ui_render_beat(void) {
  return s_render_beat;
}

// Bumped by an lv_timer inside the LVGL port task, so it advances only while
// that task is servicing timers. It stalls on a frozen renderer (lock deadlock,
// runaway screen callback, or a dead/suspended task); sys_monitor polls it.
static void render_beat_cb(lv_timer_t *t) {
  (void)t;
  s_render_beat++;
}

void ui_render_beat_kick(void) {
  s_render_beat++;
}

// Event-driven input. One pump (created in ui_init) drains input_manager events
// and dispatches them to the active screen's handler, replacing the ~110
// per-screen polling lv_timers. Input is swallowed while a transition lock is
// active or a modal overlay (msgbox/keyboard) is up, matching the old per-screen
// guards; the dropdown refreshes the transition lock while open, so it is covered
// too.
static ui_input_handler_t s_screen_handler = NULL;
static void *s_screen_handler_ctx = NULL;

void ui_input_set_screen_handler(ui_input_handler_t handler, void *ctx) {
  s_screen_handler = handler;
  s_screen_handler_ctx = ctx;
}

bool ui_sd_ready(void) {
  if (vfs_sdcard_is_mounted())
    return true;
  notify(NOTIFY_WARNING, "Insert SD card");
  return false;
}

static bool input_dispatch_blocked(void) {
  // While the screen is asleep, swallow input: the press that wakes it (tracked
  // by input_manager, which wakes the power policy) must not also act on the UI.
  return ui_input_is_locked() || msgbox_is_open() || keyboard_is_open() || power_policy_is_asleep();
}

static void ui_input_pump(lv_timer_t *t) {
  (void)t;
  input_event_t ev;
  while (input_get_event(&ev, 0)) {
    if (screen_tips_active()) {
      screen_tips_handle_input(&ev);
      continue;
    }
    if (s_screen_handler == NULL || input_dispatch_blocked()) {
      continue; // drain and discard so stale events do not fire once unblocked
    }
    ui_input_handler_t handler = s_screen_handler;
    handler(&ev, s_screen_handler_ctx);
    // If the handler switched screens, stop draining: remaining events belong to
    // the transition (and are flushed by the input lock ui_switch_screen sets).
    if (s_screen_handler != handler) {
      break;
    }
  }
}

void ui_init(void) {
  ESP_LOGI(TAG, "Initializing UI Manager...");

  assets_manager_init();

  ui_theme_init();

  ui_feedback_init();

  // Global power policy (display sleep + low-battery) and the global quick-
  // settings dropdown. Both live on the top layer so they survive screen
  // switches; created under the LVGL lock. The display is already registered
  // (lvgl_glue_init ran before ui_init), so lv_layer_top() is valid here.
  if (ui_acquire()) {
    power_policy_init();
    dropdown_ui_global_init();
    // Render-progress heartbeat: this lv_timer runs inside the LVGL port task,
    // so it advances only while that task is servicing timers. sys_monitor polls
    // ui_render_beat() to detect a frozen renderer. See ui_liveness.h.
    lv_timer_create(render_beat_cb, RENDER_BEAT_MS, NULL);
    // Single input pump for all event-driven screens (replaces per-screen timers).
    lv_timer_create(ui_input_pump, UI_INPUT_PUMP_MS, NULL);
    ui_release();
  }

  // Boot orchestration runs once in a transient task (it needs the 5 s splash
  // delay and a deep stack for screen construction), then deletes itself. There
  // is no perpetual UI task: rendering lives in the LVGL port task and each
  // screen's lv_timers, and liveness is supervised by sys_monitor.
  xTaskCreatePinnedToCore(ui_boot_task,
                          "ui_boot",
                          UI_BOOT_TASK_STACK_SIZE,
                          NULL,
                          UI_BOOT_TASK_PRIORITY,
                          NULL,
                          UI_BOOT_TASK_CORE);

  ESP_LOGI(TAG, "UI Manager initialized successfully.");
}

void ui_init_safe_mode(void) {
  ESP_LOGW(TAG, "Initializing UI in safe mode");

  assets_manager_init();
  ui_theme_init();
  ui_feedback_init();

  // Minimal LVGL infra: render heartbeat (so sys_monitor still sees liveness)
  // and the shared input pump. No power policy (screen stays on), no dropdown,
  // no boot animation, no home. Just the recovery screen.
  if (ui_acquire()) {
    lv_timer_create(render_beat_cb, RENDER_BEAT_MS, NULL);
    lv_timer_create(ui_input_pump, UI_INPUT_PUMP_MS, NULL);
    ui_safe_mode_open();
    ui_release();
  }

  ESP_LOGW(TAG, "Safe mode UI ready");
}

static void ui_boot_task(void *pvParameter) {
  (void)pvParameter;

  if (ui_acquire()) {
    ui_boot_show();
    ui_release();
  }

  vTaskDelay(pdMS_TO_TICKS(BOOT_SPLASH_DURATION_MS));

  if (ui_acquire()) {
    lv_obj_t *outgoing = lv_screen_active();
    ui_home_open();
    current_screen_id = SCREEN_HOME;
    if (outgoing != NULL && outgoing != lv_screen_active())
      lv_obj_del_async(outgoing);
    if (tutorial_should_run())
      tutorial_start();
    else
      screen_tips_hook(SCREEN_HOME);
    ui_release();
  }

  vTaskDelete(NULL); // boot done; nothing to loop on
}

static void clear_current_screen(void) {
  // Drop the outgoing screen's input handler; a migrated screen re-registers its
  // own in its open function. Non-migrated screens leave it NULL and keep using
  // their own polling timer.
  ui_input_set_screen_handler(NULL, NULL);
  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
  }
}

// Tick-safe comparisons: lv_tick_get() is a uint32_t ms counter that wraps every
// ~49.7 days, so compare signed deltas instead of the raw values.
bool ui_input_is_locked(void) {
  return (int32_t)(lv_tick_get() - input_lock_until) < 0;
}

void ui_input_lock(uint32_t ms) {
  uint32_t until = lv_tick_get() + ms;
  if ((int32_t)(until - input_lock_until) > 0)
    input_lock_until = until;
}

typedef void (*ui_open_fn_t)(void);
typedef void (*ui_close_fn_t)(void);

// Lifecycle contract (item 13): the close fn a screen registers to release the
// hardware / stop the task it started. ui_switch_screen() calls it on the
// outgoing screen before opening the next, so a radio/capture never keeps running
// after the user leaves its screen. Only screens that own hardware or a task need
// one; every other screen returns NULL (its widgets are freed by the screen
// swap). New hardware screens: add a public *_stop and a case here.
static ui_close_fn_t screen_close_fn(screen_id_t s) {
  switch (s) {
    case SCREEN_SUBGHZ_READ:
      return subghz_receiver_stop;
    case SCREEN_NFC_READ:
    case SCREEN_NFC_EMULATE:
      return nfc_manager_stop;
    case SCREEN_WAV_PLAYER:
      return ui_wav_player_stop;
    case SCREEN_MP3_PLAYER:
      return ui_mp3_player_stop;
    case SCREEN_IMAGE_VIEWER:
      return ui_image_viewer_stop;
    case SCREEN_USB_STORAGE:
      return ui_usb_storage_stop;
    default:
      return NULL;
  }
}

static ui_open_fn_t screen_open_fn(screen_id_t s) {
  switch (s) {
    case SCREEN_HOME:
      return ui_home_open;
    case SCREEN_MENU:
      return ui_menu_open;
    case SCREEN_WIFI_MENU:
      return ui_wifi_menu_open;
    case SCREEN_WIFI_CHANNELS:
      return ui_wifi_channel_open;
    case SCREEN_WIFI_CLIENTS:
      return ui_wifi_client_open;
    case SCREEN_WIFI_SCAN_MENU:
      return ui_wifi_scan_open;
    case SCREEN_WIFI_NAMES:
      return ui_wifi_names_open;
    case SCREEN_CONNECT_WIFI:
      return ui_connect_wifi_open;
    case SCREEN_BLE_MENU:
      return ui_ble_menu_open;
    case SCREEN_BLE_SCAN:
      return ui_ble_scan_open;
    case SCREEN_BLE_MOUSE_PAIRING:
      return ui_ble_mouse_pairing_open;
    case SCREEN_BLE_MOUSE:
      return ui_ble_mouse_open;
    case SCREEN_CONNECT_BLUETOOTH:
      return ui_connect_bt_open;
    case SCREEN_NFC_MENU:
      return ui_nfc_menu_open;
    case SCREEN_NFC_EMULATE:
      return ui_nfc_emulate_open;
    case SCREEN_NFC_READ:
      return ui_nfc_read_open;
    case SCREEN_NFC_SAVED:
      return ui_nfc_saved_open;
    case SCREEN_NFC_WRITE:
      return ui_nfc_write_open;
    case SCREEN_NFC_CONFIG:
      return ui_nfc_config_open;
    case SCREEN_CARD_EMU:
      return ui_card_emu_open;
    case SCREEN_POWER:
      return ui_power_open;
    case SCREEN_SETTINGS:
      return ui_settings_open;
    case SCREEN_SETTINGS_DEV:
      return ui_settings_open_dev;
    case SCREEN_CONNECTION_SETTINGS:
      return ui_connection_settings_open;
    case SCREEN_IR_MENU:
      return ui_ir_menu_open;
    case SCREEN_IR_RECEIVE:
      return ui_ir_receive_open;
    case SCREEN_IR_SEND:
      return ui_ir_send_open;
    case SCREEN_IR_REMOTE_TYPE:
      return ui_ir_remote_type_open;
    case SCREEN_IR_CONTROLLER:
      return ui_ir_controller_open;
    case SCREEN_IR_SAVED:
      return ui_ir_saved_open;
    case SCREEN_IR_BURST:
      return ui_ir_burst_open;
    case SCREEN_HAPTIC:
      return ui_haptic_open;
    case SCREEN_SPEAKER:
      return ui_speaker_open;
    case SCREEN_MIC_REC:
      return ui_micrec_open;
    case SCREEN_WAV_PLAYER:
      return ui_wav_player_open;
    case SCREEN_MP4_PLAYER:
      return ui_mp4_player_open;
    case SCREEN_MP3_PLAYER:
      return ui_mp3_player_open;
    case SCREEN_PLAYER:
      return ui_wav_library_open;
    case SCREEN_IMAGE_VIEWER:
      return ui_image_viewer_open;
    case SCREEN_USB_STORAGE:
      return ui_usb_storage_open;
    case SCREEN_SPECTRUM:
      return ui_spectrum_open;
    case SCREEN_LORA_CHAT:
      return ui_lora_chat_open;
    case SCREEN_GAMES_MENU:
      return ui_games_menu_open;
    case SCREEN_GAME_SNAKE:
      return ui_snake_open;
    case SCREEN_GAME_BREAKOUT:
      return ui_breakout_open;
    case SCREEN_GAME_GB:
      return ui_gb_open;
    case SCREEN_OCTOBIT_STATUS:
      return ui_octobit_status_open;
    case SCREEN_DEV_MENU:
      return ui_dev_menu_open;
    case SCREEN_SUBGHZ_MENU:
      return ui_subghz_menu_open;
    case SCREEN_SUBGHZ_READ:
      return ui_subghz_read_open;
    case SCREEN_SUBGHZ_BRUTE:
      return ui_subghz_brute_open;
    case SCREEN_BLE_BEACON_SPAM:
      return ui_beacon_spam_open;
    case SCREEN_RFID_MENU:
      return ui_rfid_menu_open;
    case SCREEN_BADUSB_MENU:
      return ui_badusb_menu_open;
    case SCREEN_GPIO:
      return ui_gpio_open;
    case SCREEN_FILES:
      return ui_files_open;
    case SCREEN_WIFI_ATTACK_MENU:
      return ui_wifi_attack_open;
    case SCREEN_WIFI_PACKETS_MENU:
      return ui_wifi_packets_open;
    case SCREEN_WIFI_EVIL_TWIN:
      return ui_wifi_evil_twin_open;
    case SCREEN_COMPANION_PAIRING:
      return ui_companion_pairing_open;
    case SCREEN_BLE_SPAM_SELECT:
      return ui_ble_spam_select_open;
    case SCREEN_BLE_SPAM:
      return ui_ble_spam_open;
    case SCREEN_DISPLAY_SETTINGS:
      return ui_display_settings_open;
    case SCREEN_INTERFACE_SETTINGS:
      return ui_interface_settings_open;
    case SCREEN_SOUND_SETTINGS:
      return ui_sound_settings_open;
    case SCREEN_BATTERY_SETTINGS:
      return ui_battery_settings_open;
    case SCREEN_ABOUT_SETTINGS:
      return ui_about_settings_open;
    case SCREEN_STORAGE:
      return ui_storage_settings_open;
    case SCREEN_TIME:
      return ui_time_open;
    case SCREEN_THEME_SELECTOR:
      return ui_theme_selector_open;
    case SCREEN_BLE_DETECT_MENU:
      return ui_ble_detect_open;
    case SCREEN_BLE_SNIFFER:
      return ui_ble_sniffer_open;
    case SCREEN_BLE_TRACKER:
      return ui_ble_tracker_open;
    case SCREEN_BLE_SKIMMER:
      return ui_ble_skimmer_open;
    case SCREEN_BLE_EXPOSURE:
      return ui_ble_exposure_open;
    case SCREEN_GATT_EXPLORER:
      return ui_gatt_explorer_open;
    case SCREEN_BLE_KEYBOARD:
      return ui_ble_keyboard_open;
    case SCREEN_BLE_FLOOD:
      return ui_ble_flood_open;
    case SCREEN_BLE_RADIO:
      return ui_ble_radio_open;
    case SCREEN_BLE_TRACK_DEVICE:
      return ui_ble_track_device_open;
    case SCREEN_BLE_SPAM_NAMES:
      return ui_ble_spam_names_open;
    case SCREEN_WIFI_PORT_SCAN:
      return ui_wifi_port_scan_open;
    case SCREEN_WIFI_PROBE_MON:
      return ui_wifi_probe_mon_open;
    case SCREEN_WIFI_TARGET_CLIENTS:
      return ui_wifi_target_clients_open;
    case SCREEN_WIFI_DEAUTH_DETECTOR:
      return ui_wifi_deauth_detector_open;
    case SCREEN_WIFI_SIGNAL_LOCATOR:
      return ui_wifi_signal_locator_open;
    case SCREEN_NFC_SCAN:
      return ui_nfc_scan_open;
    case SCREEN_SUBGHZ_SEND:
      return ui_subghz_send_open;
    case SCREEN_SYSTEM_UPDATE:
      return ui_system_update_open;
    case SCREEN_C5_STATUS:
      return ui_c5_status_open;
    case SCREEN_LED_CTRL:
      return ui_led_ctrl_open;
    case SCREEN_LORA_TRACEROUTE:
      return ui_lora_traceroute_open;
    case SCREEN_LORA_RNODE:
      return ui_lora_rnode_open;
    case SCREEN_LORA_MQTT:
      return ui_lora_mqtt_open;
    case SCREEN_SCRIPTS:
      return ui_scripts_open;
    case SCREEN_DEV_CONSOLE:
      return ui_dev_console_open;
    case SCREEN_DEV_DIAG:
      return ui_dev_diag_open;
    case SCREEN_BOOT_MAP:
      return ui_boot_map_open;
    case SCREEN_CRASH_REPORT:
      return ui_crash_report_open;
    case SCREEN_NFC_BANKCARD:
      return ui_nfc_bankcard_open;
    case SCREEN_NFC_DESFIRE:
      return ui_nfc_desfire_open;
    case SCREEN_NFC_ISO15693:
      return ui_nfc_iso15693_open;
    case SCREEN_NFC_ULTRALIGHT:
      return ui_nfc_ultralight_open;
    case SCREEN_NFC_NDEF:
      return ui_nfc_ndef_open;
    case SCREEN_NFC_FELICA:
      return ui_nfc_felica_open;
    case SCREEN_NFC_P2P:
      return ui_nfc_p2p_open;
    case SCREEN_NFC_KEYDICT:
      return ui_nfc_keydict_open;
    case SCREEN_SUBGHZ_CONFIG:
      return ui_subghz_config_open;
    case SCREEN_IR_RAW:
      return ui_ir_raw_open;
    case SCREEN_LORA_CHANNELS:
      return ui_lora_channels_open;
    case SCREEN_LORA_POSITION:
      return ui_lora_position_open;
    case SCREEN_LORA_TELEMETRY:
      return ui_lora_telemetry_open;
    case SCREEN_LORA_SECURE_DM:
      return ui_lora_securedm_open;
    case SCREEN_WIFI_HANDSHAKE:
      return ui_wifi_handshake_open;
    case SCREEN_WIFI_HOTSPOT:
      return ui_wifi_hotspot_open;
    case SCREEN_IMU_MONITOR:
      return ui_imu_monitor_open;
    case SCREEN_SD_HEALTH:
      return ui_sd_health_open;
    case SCREEN_USB_MOUSE:
      return ui_usb_mouse_open;
    default:
      return NULL;
  }
}

bool ui_screen_shows_chrome(screen_id_t s) {
  if (s == SCREEN_NONE) // boot splash / no screen yet: no chrome, no dropdown
    return false;
  switch (s) {
    // --- Wi-Fi: live scan / attack / capture / monitor ---
    case SCREEN_WIFI_SCAN_MENU:
    case SCREEN_WIFI_CHANNELS:
    case SCREEN_WIFI_CLIENTS:
    case SCREEN_WIFI_ATTACK_MENU:
    case SCREEN_WIFI_PACKETS_MENU:
    case SCREEN_WIFI_EVIL_TWIN:
    case SCREEN_WIFI_PORT_SCAN:
    case SCREEN_WIFI_PROBE_MON:
    case SCREEN_WIFI_TARGET_CLIENTS:
    case SCREEN_WIFI_DEAUTH_DETECTOR:
    case SCREEN_WIFI_SIGNAL_LOCATOR:
    case SCREEN_WIFI_HANDSHAKE:
    case SCREEN_WIFI_HOTSPOT:
    // --- BLE: live scan / spam / sniff / emulate ---
    case SCREEN_BLE_SCAN:
    case SCREEN_BLE_SPAM:
    case SCREEN_BLE_BEACON_SPAM:
    case SCREEN_BLE_SNIFFER:
    case SCREEN_BLE_TRACKER:
    case SCREEN_BLE_SKIMMER:
    case SCREEN_BLE_EXPOSURE:
    case SCREEN_BLE_FLOOD:
    case SCREEN_BLE_TRACK_DEVICE:
    case SCREEN_BLE_KEYBOARD:
    case SCREEN_BLE_MOUSE:
    // --- NFC: live read / write / emulate / scan ---
    case SCREEN_NFC_READ:
    case SCREEN_NFC_WRITE:
    case SCREEN_NFC_EMULATE:
    case SCREEN_CARD_EMU:
    case SCREEN_NFC_SCAN:
    case SCREEN_NFC_P2P:
    // --- IR: transmit / capture ---
    case SCREEN_IR_RECEIVE:
    case SCREEN_IR_SEND:
    case SCREEN_IR_BURST:
    case SCREEN_IR_CONTROLLER:
    case SCREEN_IR_RAW:
    // --- SubGHz: read / send / brute ---
    case SCREEN_SUBGHZ_READ:
    case SCREEN_SUBGHZ_SEND:
    case SCREEN_SUBGHZ_BRUTE:
    // --- Audio / LoRa / sensors: play / record / live monitor ---
    case SCREEN_WAV_PLAYER:
    case SCREEN_MP4_PLAYER:
    case SCREEN_MP3_PLAYER:
    case SCREEN_IMAGE_VIEWER:
    case SCREEN_USB_STORAGE:
    case SCREEN_SPECTRUM:
    case SCREEN_MIC_REC:
    case SCREEN_LORA_RNODE:
    case SCREEN_LORA_TELEMETRY:
    case SCREEN_USB_MOUSE:
    case SCREEN_IMU_MONITOR:
    // --- Dev / system: live terminal / running payload / update ---
    case SCREEN_DEV_CONSOLE:
    case SCREEN_DEV_DIAG:
    case SCREEN_BOOT_MAP:
    case SCREEN_CRASH_REPORT:
    case SCREEN_SYSTEM_UPDATE:
    case SCREEN_SCRIPTS:
    case SCREEN_TIME:
      return false;
    default:
      return true;
  }
}

void ui_switch_screen(screen_id_t new_screen) {
  ui_open_fn_t open_fn = screen_open_fn(new_screen);
  if (open_fn == NULL) {
    ESP_LOGW(TAG, "Screen %d not available — staying put", (int)new_screen);
    return;
  }

  input_lock_until = lv_tick_get() + INPUT_LOCK_MS;

  if (ui_acquire()) {
    lv_obj_t *outgoing = lv_screen_active();
    // Lifecycle: stop the outgoing screen's hardware/task before tearing it down.
    ui_close_fn_t close_fn = screen_close_fn(current_screen_id);
    if (close_fn != NULL) {
      close_fn();
    }
    clear_current_screen();
    // Returning to the top level clears the breadcrumb root so a stale category
    // ("NFC") never prefixes a fresh navigation. Category menus re-set it on open.
    if (new_screen == SCREEN_HOME || new_screen == SCREEN_MENU)
      ui_chrome_set_breadcrumb_root("");
    // Tell the chrome header whether to carry the shared status cluster before
    // the screen builds it (current_screen_id only updates after open_fn()).
    ui_chrome_set_status_enabled(ui_screen_shows_chrome(new_screen));
    open_fn();
    current_screen_id = new_screen;
    if (outgoing != NULL && outgoing != lv_screen_active() && lv_obj_is_valid(outgoing))
      lv_obj_del_async(outgoing);
    screen_tips_hook(new_screen);
    ui_release();
  }
}

bool ui_acquire(void) {
  // Finite timeout so a task that never releases the lock can no longer freeze
  // every other UI caller forever. Callers already treat false as "skip".
  if (!lvgl_glue_lock(UI_LOCK_TIMEOUT_MS)) {
    ESP_LOGW(TAG, "ui_acquire timed out after %d ms; UI lock held elsewhere", UI_LOCK_TIMEOUT_MS);
    return false;
  }
  return true;
}

void ui_release(void) {
  lvgl_glue_unlock();
}

void ui_async_call(lv_async_cb_t cb, void *user_data) {
  if (ui_acquire()) {
    lv_async_call(cb, user_data);
    ui_release();
  }
}

void ui_screen_load(lv_obj_t *scr) {
  lv_screen_load(scr);
}

static void screen_slot_clear_cb(lv_event_t *e) {
  lv_obj_t **slot = lv_event_get_user_data(e);
  if (slot != NULL)
    *slot = NULL;
}

void ui_screen_load_owned(lv_obj_t **slot, lv_obj_t *scr) {
  if (slot != NULL)
    *slot = scr;
  lv_obj_add_event_cb(scr, screen_slot_clear_cb, LV_EVENT_DELETE, slot);
  lv_screen_load(scr);
}

screen_id_t ui_current_screen(void) {
  return current_screen_id;
}

void ui_manager_relayout_current(void) {}

bool ui_btn_up(void) {
  return up_button_is_down();
}

bool ui_btn_down(void) {
  return down_button_is_down();
}

bool ui_btn_left(void) {
  return left_button_is_down();
}

bool ui_btn_right(void) {
  return right_button_is_down();
}
