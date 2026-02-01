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

#define TAG "UI_MANAGER"

#define UI_TASK_STACK_SIZE      (4096 * 2) 
#define UI_TASK_PRIORITY        (tskIDLE_PRIORITY + 2) 
#define UI_TASK_CORE            1 

#define LVGL_TICK_PERIOD_MS     5

static SemaphoreHandle_t xGuiSemaphore = NULL;
static bool is_emergency_restart = false;

static void ui_task(void *pvParameter);
static void lv_tick_task(void *arg);
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

void ui_init(void)
{
  ESP_LOGI(TAG, "Initializing UI Manager...");

  assets_manager_init();

  ui_theme_init();

  xGuiSemaphore = xSemaphoreCreateRecursiveMutex();
  if (!xGuiSemaphore) {
    ESP_LOGE(TAG, "Failed to create UI Mutex");
    return;
  }

  const esp_timer_create_args_t periodic_timer_args = {
    .callback = &lv_tick_task,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t periodic_timer;
  ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LVGL_TICK_PERIOD_MS * 1000));

  xTaskCreatePinnedToCore(
    ui_task,            
    "UI Task",          
    UI_TASK_STACK_SIZE, 
    NULL,               
    UI_TASK_PRIORITY,   
    NULL,               
    UI_TASK_CORE        
  );

  ESP_LOGI(TAG, "UI Manager initialized successfully.");
}

void ui_hard_restart(void) {
  ESP_LOGW(TAG, "Executing UI Task Emergency Restart...");
  is_emergency_restart = true;
  xTaskCreatePinnedToCore(
    ui_task,            
    "UI Task",          
    UI_TASK_STACK_SIZE, 
    NULL,               
    UI_TASK_PRIORITY,   
    NULL,               
    UI_TASK_CORE        
  );
}

static void ui_task(void *pvParameter)
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

  TickType_t start_tick = xTaskGetTickCount();
  bool boot_screen_done = is_recovery; 

  while (1) {
    if (ui_acquire()) {
      if (!boot_screen_done && (xTaskGetTickCount() - start_tick >= pdMS_TO_TICKS(5000))) {
        ui_home_open();
        boot_screen_done = true;
      }

      lv_timer_handler();
      ui_release();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
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

static void lv_tick_task(void *arg)
{
  (void) arg;
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}


bool ui_acquire(void)
{
  if (xGuiSemaphore != NULL) {
    return (xSemaphoreTakeRecursive(xGuiSemaphore, portMAX_DELAY) == pdTRUE);
  }
  return false;
}

void ui_release(void)
{
  if (xGuiSemaphore != NULL) {
    xSemaphoreGiveRecursive(xGuiSemaphore);
  }
}


