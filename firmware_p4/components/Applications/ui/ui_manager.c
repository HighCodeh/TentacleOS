#include "core/lv_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"
#include "esp_timer.h"
#include "home_ui.h"
#include "menu_ui.h"
#include "wifi_ui.h"
#include "wifi_attack_menu_ui.h"
#include "wifi_packets_menu_ui.h"
#include "wifi_deauth_attack_ui.h"
#include "wifi_beacon_spam_simple_ui.h"
#include "wifi_probe_flood_ui.h"
#include "wifi_auth_flood_ui.h"
#include "wifi_sniffer_raw_ui.h"
#include "wifi_sniffer_attack_ui.h"
#include "wifi_sniffer_handshake_ui.h"
#include "wifi_scan_menu_ui.h"
#include "wifi_scan_ap_ui.h"
#include "wifi_scan_stations_ui.h"
#include "wifi_scan_target_ui.h"
#include "wifi_scan_probe_ui.h"
#include "wifi_scan_monitor_ui.h"
#include "wifi_scan_ui.h"
#include "ui_ble_menu.h"
#include "settings_ui.h"
#include "display_settings_ui.h"
#include "interface_settings_ui.h"
#include "sound_settings_ui.h"
#include "battery_settings_ui.h"
#include "connection_settings_ui.h"
#include "connect_wifi_ui.h"
#include "connect_bt_ui.h"
#include "about_settings_ui.h"
#include "ui_ble_spam.h"
#include "ui_ble_spam_select.h"
#include "ui_badusb_menu.h"
#include "ui_badusb_browser.h"
#include "ui_badusb_layout.h"
#include "ui_badusb_connect.h"
#include "ui_badusb_running.h"
#include "subghz_spectrum_ui.h"
#include "wifi_ap_list_ui.h"
#include "wifi_deauth_ui.h"
#include "wifi_evil_twin_ui.h"
#include "wifi_beacon_spam_ui.h"
#include "wifi_probe_ui.h"
#include "esp_log.h"
#include "bluetooth_service.h"
#include "bad_usb.h"
#include "boot_ui.h"
#include "msgbox_ui.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "assets_manager.h"

// esp_lvgl_adapter - 接管 LVGL 任务管理和线程安全锁
#include "esp_lv_adapter.h"

#define TAG "UI_MANAGER"

// LVGL worker task 配置（由 esp_lv_adapter_start() 创建，在 kernel.c 的 adapter_cfg 中定义）
// 这里只保留 UI 逻辑相关的参数
static bool is_emergency_restart = false;

static void clear_current_screen(void);

static bool is_ble_screen(screen_id_t screen) {
  return (screen == SCREEN_BLE_MENU || screen == SCREEN_BLE_SPAM || screen == SCREEN_BLE_SPAM_SELECT);
}

static bool is_badusb_screen(screen_id_t screen) {
  return (screen == SCREEN_BADUSB_MENU || screen == SCREEN_BADUSB_BROWSER || 
  screen == SCREEN_BADUSB_LAYOUT || screen == SCREEN_BADUSB_CONNECT || 
  screen == SCREEN_BADUSB_RUNNING);
}

screen_id_t current_screen_id = SCREEN_NONE;

// ---------------------------------------------------------------------------
// Boot sequence task（在 LVGL worker task 之外单独执行）
// ---------------------------------------------------------------------------
static void ui_boot_task(void *pvParameter)
{
  ui_theme_init();

  bool is_recovery = is_emergency_restart;
  is_emergency_restart = false;

  if (ui_acquire()) {
    if (is_recovery) {
      ui_home_open();
      msgbox_open(LV_SYMBOL_WARNING, "UI Recovered!\nInterface task was restarted.", "OK", NULL, NULL);
    } else {
      ui_boot_show();
    }
    ui_release();
  }

  // 等待 5 秒后切换到主页
  vTaskDelay(pdMS_TO_TICKS(5000));

  if (ui_acquire()) {
    if (!is_recovery) {
      ui_home_open();
    }
    ui_release();
  }

  vTaskDelete(NULL);
}

void ui_init(void)
{
  ESP_LOGI(TAG, "Initializing UI Manager...");

  assets_manager_init();
  ui_theme_init();

  // 启动 esp_lvgl_adapter 的 LVGL worker task（替代原来手动创建的 ui_task + lv_tick_task）
  // adapter 自己管理 lv_timer_handler() 的调用和 tick 计时
  ESP_ERROR_CHECK(esp_lv_adapter_start());

  // 在单独的任务中处理启动画面（否则会阻塞 init 流程）
  xTaskCreatePinnedToCore(
    ui_boot_task,
    "UI Boot",
    4096 * 2,
    NULL,
    tskIDLE_PRIORITY + 2,
    NULL,
    1
  );

  ESP_LOGI(TAG, "UI Manager initialized successfully.");
}

void ui_hard_restart(void) {
  ESP_LOGW(TAG, "Executing UI Emergency Restart...");
  is_emergency_restart = true;
  xTaskCreatePinnedToCore(
    ui_boot_task,
    "UI Boot",
    4096 * 2,
    NULL,
    tskIDLE_PRIORITY + 2,
    NULL,
    1
  );
}

static void clear_current_screen(void){
  lv_group_remove_all_objs(main_group);
}

void ui_switch_screen(screen_id_t new_screen) {
  if (ui_acquire()) {

    // Power Management for BLE
    bool was_ble = is_ble_screen(current_screen_id);
    bool is_ble = is_ble_screen(new_screen);

    if (is_ble && !was_ble) {
      ESP_LOGI(TAG, "Entering BLE Mode: Initializing Service...");
      bluetooth_service_init();
      bluetooth_service_start();
    } else if (!is_ble && was_ble) {
      ESP_LOGI(TAG, "Exiting BLE Mode: Stopping Service...");
      bluetooth_service_stop();
    }

    clear_current_screen();

    switch (new_screen) {
      case SCREEN_HOME:
        ui_home_open();
        break;

      case SCREEN_MENU:
        ui_menu_open();
        break;

      case SCREEN_SETTINGS:
        ui_settings_open();
        break;

      case SCREEN_DISPLAY_SETTINGS:
        ui_display_settings_open();
        break;

      case SCREEN_INTERFACE_SETTINGS:
        ui_interface_settings_open();
        break;

      case SCREEN_SOUND_SETTINGS:
        ui_sound_settings_open();
        break;

      case SCREEN_BATTERY_SETTINGS:
        ui_battery_settings_open();
        break;

      case SCREEN_CONNECTION_SETTINGS:
        ui_connection_settings_open();
        break;

      case SCREEN_CONNECT_BLUETOOTH:
        ui_connect_bt_open();
        break;

      case SCREEN_CONNECT_WIFI:
        ui_connect_wifi_open();
        break;

      case SCREEN_ABOUT_SETTINGS:
        ui_about_settings_open();
        break;

      case SCREEN_WIFI_MENU:
        ui_wifi_menu_open();
        break;

      case SCREEN_WIFI_ATTACK_MENU:
        ui_wifi_attack_menu_open();
        break;

      case SCREEN_WIFI_PACKETS_MENU:
        ui_wifi_packets_menu_open();
        break;

      case SCREEN_WIFI_SNIFFER_RAW:
        ui_wifi_sniffer_raw_open();
        break;
      case SCREEN_WIFI_SNIFFER_ATTACK:
        ui_wifi_sniffer_attack_open();
        break;
      case SCREEN_WIFI_SNIFFER_HANDSHAKE:
        ui_wifi_sniffer_handshake_open();
        break;

      case SCREEN_WIFI_DEAUTH_ATTACK:
        ui_wifi_deauth_attack_open();
        break;

      case SCREEN_WIFI_BEACON_SPAM_SIMPLE:
        ui_wifi_beacon_spam_simple_open();
        break;

      case SCREEN_WIFI_PROBE_FLOOD:
        ui_wifi_probe_flood_open();
        break;

      case SCREEN_WIFI_AUTH_FLOOD:
        ui_wifi_auth_flood_open();
        break;

      case SCREEN_WIFI_SCAN_MENU:
        ui_wifi_scan_menu_open();
        break;

      case SCREEN_WIFI_SCAN_AP:
        ui_wifi_scan_ap_open();
        break;

      case SCREEN_WIFI_SCAN_STATIONS:
        ui_wifi_scan_stations_open();
        break;

      case SCREEN_WIFI_SCAN_TARGET:
        ui_wifi_scan_target_open();
        break;

      case SCREEN_WIFI_SCAN_PROBE:
        ui_wifi_scan_probe_open();
        break;

      case SCREEN_WIFI_SCAN_MONITOR:
        ui_wifi_scan_monitor_open();
        break;

      case SCREEN_WIFI_SCAN:
        ui_wifi_scan_open();
        break;

      case SCREEN_WIFI_AP_LIST:
        ui_wifi_ap_list_open();
        break;

      case SCREEN_WIFI_DEAUTH:
        ui_wifi_deauth_open();
        break;

      case SCREEN_WIFI_EVIL_TWIN:
        ui_wifi_evil_twin_open();
        break;

      case SCREEN_WIFI_BEACON_SPAM:
        ui_wifi_beacon_spam_open();
        break;

      case SCREEN_WIFI_PROBE:
        ui_wifi_probe_open();
        break;

      case SCREEN_BLE_MENU:
        ui_ble_menu_open();
        break;

      case SCREEN_BLE_SPAM_SELECT:
        ui_ble_spam_select_open();
        break;

      case SCREEN_BLE_SPAM:
        ui_ble_spam_open();
        break;

      case SCREEN_SUBGHZ_SPECTRUM:
        ui_subghz_spectrum_open();
        break;

      case SCREEN_BADUSB_MENU:
        ui_badusb_menu_open();
        break;

      case SCREEN_BADUSB_BROWSER:
        ui_badusb_browser_open();
        break;

      case SCREEN_BADUSB_LAYOUT:
        ui_badusb_layout_open();
        break;

      case SCREEN_BADUSB_CONNECT:
        ui_badusb_connect_open();
        break;

      case SCREEN_BADUSB_RUNNING:
        ui_badusb_running_open();
        break;

      default:
        break;
    }

    current_screen_id = new_screen;
    ui_release();
  }
}

// ---------------------------------------------------------------------------
// 线程安全 API（委托给 esp_lv_adapter 的递归互斥锁）
// ---------------------------------------------------------------------------

bool ui_acquire(void)
{
  // esp_lv_adapter_lock(-1) 表示无限等待，与原 portMAX_DELAY 行为一致
  return (esp_lv_adapter_lock(-1) == ESP_OK);
}

void ui_release(void)
{
  esp_lv_adapter_unlock();
}
