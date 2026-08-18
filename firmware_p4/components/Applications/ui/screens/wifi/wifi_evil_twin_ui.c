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

#include "wifi_evil_twin_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "ap_scanner.h"
#include "evil_twin.h"
#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "WIFI_EVIL_TWIN_UI";
static const char *ET_ICON = "/assets/icons/wifi_tethering.bin";

#define ET_AP_MAX       20
#define ET_SSID_LEN     33
#define ET_PW_LEN       64
#define ET_POLL_MS      500
#define TASK_STACK_SIZE 8192
#define TASK_PRIORITY   SYS_PRIO_SERVICE_LO

#define ET_WAVES_Y_OFS   8
#define ET_CAPTION_Y_OFS 78
#define COLOR_CAPTURED   0x00E676

typedef enum { ET_SCANNING, ET_PICK, ET_ATTACK } et_state_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_ssid_label = NULL;
static lv_obj_t *s_result_label = NULL;

static et_state_t s_state = ET_SCANNING;
static bool s_is_scanning = false;
static bool s_is_scan_cued = false;
static int s_ap_count = 0;
static char s_ap_ssids[ET_AP_MAX][ET_SSID_LEN];

static char s_target_ssid[ET_SSID_LEN];
static char s_password[ET_PW_LEN];
static volatile bool s_is_attacking = false;
static volatile bool s_is_stop_requested = false;
static bool s_is_captured = false;

static void wifi_evil_twin_input(const input_event_t *ev, void *ctx);

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_label = NULL;
  s_ssid_label = NULL;
  s_result_label = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  s_menu = (menu_component_t){0};

  if (s_state == ET_SCANNING) {
    ui_chrome_header(s_screen, "EVIL TWIN", ET_ICON);
    waves_create(s_screen, LV_ALIGN_CENTER, 0, ET_WAVES_Y_OFS, LV_SYMBOL_WIFI, ET_ICON);
    lv_obj_t *cap = lv_label_create(s_screen);
    lv_label_set_text(cap, "Scanning APs...");
    lv_obj_set_style_text_color(cap, current_theme.text_main, 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(cap, LV_ALIGN_CENTER, 0, ET_CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else if (s_state == ET_PICK) {
    s_menu = menu_component_create(s_screen, "EVIL TWIN", ET_ICON);
    if (s_ap_count == 0) {
      menu_component_add_item(&s_menu, ET_ICON, "No networks found");
    } else {
      for (int i = 0; i < s_ap_count; i++)
        menu_component_add_item(&s_menu, ET_ICON, s_ap_ssids[i]);
    }
    menu_component_set_hint(&s_menu, "OK clone   BACK exit");
  } else {
    ui_chrome_header(s_screen, "EVIL TWIN", ET_ICON);

    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "Broadcasting...");
    lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 12);

    s_ssid_label = lv_label_create(s_screen);
    lv_label_set_text_fmt(s_ssid_label, "SSID: %s", s_target_ssid);
    lv_obj_set_style_text_color(s_ssid_label, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(s_ssid_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_ssid_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_ssid_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 34);

    waves_create(s_screen, LV_ALIGN_CENTER, 0, ET_WAVES_Y_OFS, NULL, NULL);

    s_result_label = lv_label_create(s_screen);
    if (s_is_captured) {
      lv_label_set_text_fmt(s_result_label, "Password: %s", s_password);
      lv_obj_set_style_text_color(s_result_label, lv_color_hex(COLOR_CAPTURED), 0);
    } else {
      lv_label_set_text(s_result_label, "Waiting for password...");
      lv_obj_set_style_text_color(s_result_label, current_theme.border_inactive, 0);
    }
    lv_obj_set_style_text_font(s_result_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(s_result_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_result_label, LV_PCT(90));
    lv_label_set_long_mode(s_result_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_result_label, LV_ALIGN_CENTER, 0, ET_CAPTION_Y_OFS);

    ui_chrome_footer(s_screen, "BACK: Stop");
  }

  ui_input_set_screen_handler(wifi_evil_twin_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_EVIL_TWIN)
    return;
  s_state = ET_PICK;
  build_screen();
  if (s_ap_count > 0 && !s_is_scan_cued) {
    s_is_scan_cued = true;
    ui_feedback(UI_FB_READ);
  }
}

static void ap_scan_task(void *arg) {
  (void)arg;

  int n = 0;
  if (ap_scanner_start()) {
    uint16_t count = 0;
    wifi_ap_record_t *recs = ap_scanner_get_results(&count);
    if (recs != NULL) {
      for (uint16_t i = 0; i < count && n < ET_AP_MAX; i++) {
        if (recs[i].ssid[0] == '\0')
          continue;
        strncpy(s_ap_ssids[n], (const char *)recs[i].ssid, ET_SSID_LEN - 1);
        s_ap_ssids[n][ET_SSID_LEN - 1] = '\0';
        n++;
      }
    }
    ap_scanner_free_results();
  }

  s_ap_count = n;
  s_is_scanning = false;
  ui_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void password_ready_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_EVIL_TWIN || s_state != ET_ATTACK)
    return;
  if (s_status_label != NULL)
    lv_label_set_text(s_status_label, "Captured!");
  if (s_result_label != NULL) {
    lv_label_set_text_fmt(s_result_label, "Password: %s", s_password);
    lv_obj_set_style_text_color(s_result_label, lv_color_hex(COLOR_CAPTURED), 0);
  }
  ui_feedback(UI_FB_READ);
}

static void attack_task(void *arg) {
  (void)arg;

  evil_twin_reset_capture();
  evil_twin_start_attack(s_target_ssid);

  bool captured = false;
  while (!s_is_stop_requested) {
    if (!captured && evil_twin_has_password()) {
      evil_twin_get_last_password(s_password, sizeof(s_password));
      s_is_captured = true;
      captured = true;
      ui_async_call(password_ready_cb, NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(ET_POLL_MS));
  }

  evil_twin_stop_attack();
  s_is_attacking = false;
  vTaskDelete(NULL);
}

static void start_attack_selected(void) {
  int sel = menu_component_get_selected(&s_menu);
  if (sel < 0 || sel >= s_ap_count)
    return;

  strncpy(s_target_ssid, s_ap_ssids[sel], ET_SSID_LEN - 1);
  s_target_ssid[ET_SSID_LEN - 1] = '\0';

  s_is_captured = false;
  s_is_stop_requested = false;
  s_state = ET_ATTACK;
  build_screen();

  if (!s_is_attacking) {
    s_is_attacking = true;
    if (xTaskCreatePinnedToCore(
            attack_task, "evil_twin", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_RADIO) !=
        pdPASS) {
      s_is_attacking = false;
    }
  }
  ui_feedback(UI_FB_SELECT);
}

static void wifi_evil_twin_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav && s_state == ET_PICK)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav && s_state == ET_PICK)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press) {
        if (s_state == ET_ATTACK)
          s_is_stop_requested = true;
        ESP_LOGI(TAG, "evil-twin screen closed");
        ui_switch_screen(SCREEN_WIFI_MENU);
      }
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press && s_state == ET_PICK && s_ap_count > 0)
        start_attack_selected();
      break;
    default:
      break;
  }
}

void ui_wifi_evil_twin_open(void) {
  if (s_is_attacking)
    s_is_stop_requested = true;

  s_state = ET_SCANNING;
  s_ap_count = 0;
  s_is_scan_cued = false;
  s_is_captured = false;
  build_screen();

  if (!s_is_scanning) {
    s_is_scanning = true;
    if (xTaskCreatePinnedToCore(ap_scan_task,
                                "et_ap_scan",
                                TASK_STACK_SIZE,
                                NULL,
                                TASK_PRIORITY,
                                NULL,
                                SYS_CORE_RADIO) != pdPASS) {
      s_is_scanning = false;
      s_state = ET_PICK;
      s_ap_count = 0;
      build_screen();
    }
  }
}
