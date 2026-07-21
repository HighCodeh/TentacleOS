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

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "lvgl_glue.h"
#include "lv_port_indev.h"
#include "msgbox_ui.h"
#include "ui_feedback.h"
#include "ui_theme.h"

#include "boot_ui.h"
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
#include "spectrum_ui.h"
#include "lora_chat_ui.h"
#include "games_menu_ui.h"
#include "flappy_ui.h"
#include "snake_ui.h"
#include "breakout_ui.h"
#include "octopet_ui.h"
#include "octobit_status_ui.h"
#include "dev_menu_ui.h"
#include "subghz_menu_ui.h"
#include "rfid_menu_ui.h"
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
#include "theme_selector_ui.h"

static const char *TAG = "UI_MANAGER";

#define UI_TASK_STACK_SIZE      (4096 * 4)
#define UI_TASK_PRIORITY        (tskIDLE_PRIORITY + 4)
#define UI_TASK_CORE            1
#define INPUT_LOCK_MS           500
#define BOOT_SPLASH_DURATION_MS 5000
#define UI_IDLE_LOOP_MS         1000

static bool is_emergency_restart = false;
static uint32_t input_lock_until = 0;

static void ui_task(void *pvParameter);
static void clear_current_screen(void);

screen_id_t current_screen_id = SCREEN_NONE;

void ui_init(void) {
  ESP_LOGI(TAG, "Initializing UI Manager...");

  assets_manager_init();

  ui_theme_init();

  ui_feedback_init();

  xTaskCreatePinnedToCore(
      ui_task, "UI Task", UI_TASK_STACK_SIZE, NULL, UI_TASK_PRIORITY, NULL, UI_TASK_CORE);

  ESP_LOGI(TAG, "UI Manager initialized successfully.");
}

void ui_hard_restart(void) {
  ESP_LOGW(TAG, "Executing UI Task Emergency Restart...");
  is_emergency_restart = true;
  xTaskCreatePinnedToCore(
      ui_task, "UI Task", UI_TASK_STACK_SIZE, NULL, UI_TASK_PRIORITY, NULL, UI_TASK_CORE);
}

static void ui_task(void *pvParameter) {
  (void)pvParameter;

  bool is_recovery = is_emergency_restart;
  is_emergency_restart = false;

  if (ui_acquire()) {
    if (is_recovery) {
      ui_home_open();
      msgbox_open(
          LV_SYMBOL_WARNING, "UI Recovered!\nInterface task was restarted.", "OK", NULL, NULL);
    } else {
      ui_boot_show();
    }
    ui_release();
  }

  if (!is_recovery) {
    vTaskDelay(pdMS_TO_TICKS(BOOT_SPLASH_DURATION_MS));
    if (ui_acquire()) {
      ui_home_open();
      ui_release();
    }
  }

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(UI_IDLE_LOOP_MS));
  }
}

static void clear_current_screen(void) {
  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
  }
}

bool ui_input_is_locked(void) {
  return (lv_tick_get() < input_lock_until);
}

typedef void (*ui_open_fn_t)(void);

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
    case SCREEN_SPECTRUM:
      return ui_spectrum_open;
    case SCREEN_LORA_CHAT:
      return ui_lora_chat_open;
    case SCREEN_GAMES_MENU:
      return ui_games_menu_open;
    case SCREEN_GAME_FLAPPY:
      return ui_flappy_open;
    case SCREEN_GAME_SNAKE:
      return ui_snake_open;
    case SCREEN_GAME_BREAKOUT:
      return ui_breakout_open;
    case SCREEN_GAME_OCTOPET:
      return ui_octopet_open;
    case SCREEN_OCTOBIT_STATUS:
      return ui_octobit_status_open;
    case SCREEN_DEV_MENU:
      return ui_dev_menu_open;
    case SCREEN_SUBGHZ_MENU:
      return ui_subghz_menu_open;
    case SCREEN_SUBGHZ_READ:
      return ui_subghz_read_open;
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
    case SCREEN_THEME_SELECTOR:
      return ui_theme_selector_open;
    default:
      return NULL;
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
    clear_current_screen();
    open_fn();
    current_screen_id = new_screen;
    ui_release();
  }
}

bool ui_acquire(void) {
  return lvgl_glue_lock(-1);
}

void ui_release(void) {
  lvgl_glue_unlock();
}

void ui_screen_load(lv_obj_t *scr) {
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
