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

#include "connect_wifi_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "st7789.h"

#include "keyboard_ui.h"
#include "led_control.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "waves_ui.h"
#include "wifi_service.h"
#include "wifi_live_scan_ui.h"

static const char *TAG = "CONNECT_WIFI_UI";

#define WIFI_MAX_APS 12

#define WIFI_TASK_STACK_SIZE 8192
#define CONNECT_POLL_MS      250
#define CONNECT_TIMEOUT_MS   20000

#define CONNECT_STATE_IDLE      0
#define CONNECT_STATE_FAILED    1
#define CONNECT_STATE_CONNECTED 2
#define CONNECT_STATE_PASSWORD  3
#define WIFI_SSID_BYTES         32
#define WIFI_PASSWORD_BYTES     64
#define JOIN_HINT               "UP/DOWN pick | LEFT/RIGHT page | OK join"

#define SCAN_WAVES_Y_OFS   -6
#define SCAN_CAPTION_Y_OFS 78

#define COLOR_OPEN_HEX 0x00E676
#define COLOR_LOCK_HEX 0xF5B13D

#define NET_BODY_H      (ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define NET_ROW_H       46
#define NET_ROW_GAP     6
#define NET_SIDE_PAD    8
#define NET_CARD_RADIUS 8
#define NET_GLOW_W      12
#define NET_GLYPH_SZ    16
#define NET_GLYPH_X     8
#define NET_TEXT_X      34
#define NET_KEYHOLE_SZ  4
#define NET_BAR_COUNT   3
#define NET_BAR_W       5
#define NET_BAR_GAP     3
#define NET_BAR_H0      8
#define NET_BAR_STEP    4
#define NET_ARC_X       -12
#define RSSI_L3_DBM     -55
#define RSSI_L2_DBM     -65
#define RSSI_L1_DBM     -75

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

typedef struct {
  char ssid[33];
  int8_t rssi;
  bool open;
  uint8_t channel;
} ap_entry_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;
static uint16_t s_ap_page;
static int s_ap_count = 0;
static ap_entry_t s_aps[WIFI_MAX_APS];

static int s_sel = 0;
static lv_obj_t *s_row[WIFI_MAX_APS];

static char s_connect_ssid[WIFI_SSID_BYTES + 1];
static char s_connect_pass[WIFI_PASSWORD_BYTES + 1];
static bool s_use_saved_password;
static bool s_is_secured;
static bool s_connect_cancelled;
static portMUX_TYPE s_connect_lock = portMUX_INITIALIZER_UNLOCKED;
static lv_timer_t *s_connect_timer;
static lv_obj_t *s_join_footer;
static bool s_connecting = false;
static uint8_t s_connect_state = 0;
static uint8_t s_connect_ip[4] = {0};

static void connect_wifi_input(const input_event_t *ev, void *ctx);

static int signal_level(int8_t rssi) {
  if (rssi >= RSSI_L3_DBM)
    return 3;
  if (rssi >= RSSI_L2_DBM)
    return 2;
  if (rssi >= RSSI_L1_DBM)
    return 1;
  return 0;
}

static void on_keyboard_submit(const char *text, void *user_data);

static bool connection_busy(void) {
  portENTER_CRITICAL(&s_connect_lock);
  bool is_busy = s_connecting;
  portEXIT_CRITICAL(&s_connect_lock);
  return is_busy;
}

static bool connection_cancelled(void) {
  portENTER_CRITICAL(&s_connect_lock);
  bool is_cancelled = s_connect_cancelled;
  portEXIT_CRITICAL(&s_connect_lock);
  return is_cancelled;
}

static void edit_password(bool confirm) {
  if (!confirm || ui_current_screen() != SCREEN_CONNECT_WIFI)
    return;
  keyboard_open_config(&(keyboard_config_t){.title = "Wi-Fi password",
                                            .initial = NULL,
                                            .max_chars = WIFI_PASSWORD_BYTES,
                                            .min_chars = 1,
                                            .mode = KEYBOARD_PASSWORD},
                       on_keyboard_submit,
                       NULL);
}

static void connect_done_cb(lv_timer_t *timer) {
  (void)timer;
  uint8_t ip[sizeof(s_connect_ip)];
  portENTER_CRITICAL(&s_connect_lock);
  uint8_t state = s_connect_state;
  s_connect_state = CONNECT_STATE_IDLE;
  memcpy(ip, s_connect_ip, sizeof(ip));
  portEXIT_CRITICAL(&s_connect_lock);
  if (state == CONNECT_STATE_IDLE || ui_current_screen() != SCREEN_CONNECT_WIFI)
    return;

  if (s_join_footer != NULL)
    ui_chrome_footer_set_text(s_join_footer, JOIN_HINT);
  if (state == CONNECT_STATE_PASSWORD) {
    edit_password(true);
  } else if (state == CONNECT_STATE_CONNECTED) {
    char msg[48];
    snprintf(msg, sizeof(msg), "CONNECTED\n%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    ESP_LOGI(TAG, "Wi-Fi connected");
    led_signal_info();
    msgbox_open(LV_SYMBOL_OK, msg, "OK", NULL, NULL);
  } else {
    ESP_LOGW(TAG, "Wi-Fi connection failed");
    led_signal_warning();
    msgbox_open(LV_SYMBOL_CLOSE,
                "CONNECT FAILED\nCheck network or password",
                s_is_secured ? "Password" : "OK",
                s_is_secured ? "Back" : NULL,
                s_is_secured ? edit_password : NULL);
  }
}

static void wifi_connect_task(void *arg) {
  (void)arg;
  uint8_t ip[sizeof(s_connect_ip)] = {0};
  uint8_t state = CONNECT_STATE_FAILED;
  esp_err_t err =
      wifi_service_connect_to_ap(s_connect_ssid, s_use_saved_password ? NULL : s_connect_pass);
  if (err == ESP_ERR_NOT_FOUND && s_use_saved_password) {
    state = CONNECT_STATE_PASSWORD;
  } else {
    uint32_t waited = 0;
    while (err == ESP_OK && !connection_cancelled() && waited < CONNECT_TIMEOUT_MS) {
      const char *ssid = wifi_service_get_connected_ssid();
      if (ssid != NULL && strcmp(ssid, s_connect_ssid) == 0 &&
          wifi_service_get_ip(0, ip) == ESP_OK && (ip[0] || ip[1] || ip[2] || ip[3])) {
        state = CONNECT_STATE_CONNECTED;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(CONNECT_POLL_MS));
      waited += CONNECT_POLL_MS;
    }
  }
  portENTER_CRITICAL(&s_connect_lock);
  memset(s_connect_pass, 0, sizeof(s_connect_pass));
  memcpy(s_connect_ip, ip, sizeof(ip));
  s_connect_state = s_connect_cancelled ? CONNECT_STATE_IDLE : state;
  s_connecting = false;
  portEXIT_CRITICAL(&s_connect_lock);
  vTaskDelete(NULL);
}

static void start_connection(const char *password) {
  if (connection_busy() || s_scanning || s_connect_timer == NULL)
    return;
  s_use_saved_password = password == NULL;
  strncpy(s_connect_pass, password != NULL ? password : "", sizeof(s_connect_pass) - 1);
  s_connect_pass[sizeof(s_connect_pass) - 1] = '\0';
  portENTER_CRITICAL(&s_connect_lock);
  s_connect_cancelled = false;
  s_connect_state = CONNECT_STATE_IDLE;
  s_connecting = true;
  portEXIT_CRITICAL(&s_connect_lock);
  if (s_join_footer != NULL)
    ui_chrome_footer_set_text(s_join_footer, "Connecting... | BACK return");
  ESP_LOGI(TAG, "Connecting to '%s'", s_connect_ssid);
  led_signal_info();
  if (xTaskCreatePinnedToCore(wifi_connect_task,
                              "wifi_conn",
                              WIFI_TASK_STACK_SIZE,
                              NULL,
                              SYS_PRIO_SERVICE_LO,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    portENTER_CRITICAL(&s_connect_lock);
    memset(s_connect_pass, 0, sizeof(s_connect_pass));
    s_connecting = false;
    s_connect_state = CONNECT_STATE_FAILED;
    portEXIT_CRITICAL(&s_connect_lock);
    ESP_LOGE(TAG, "Failed to create Wi-Fi connection worker");
    led_signal_error();
  }
}

static void on_keyboard_submit(const char *text, void *user_data) {
  (void)user_data;
  if (ui_current_screen() == SCREEN_CONNECT_WIFI)
    start_connection(text);
}

static void add_sec_glyph(lv_obj_t *card, bool open) {
  lv_obj_t *g = lv_obj_create(card);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g, NET_GLYPH_SZ, NET_GLYPH_SZ);
  lv_obj_align(g, LV_ALIGN_LEFT_MID, NET_GLYPH_X, 0);
  if (open) {
    lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g, 2, 0);
    lv_obj_set_style_border_color(g, lv_color_hex(COLOR_OPEN_HEX), 0);
  } else {
    lv_obj_set_style_radius(g, 3, 0);
    lv_obj_set_style_bg_color(g, lv_color_hex(COLOR_LOCK_HEX), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g, 0, 0);
    lv_obj_t *hole = lv_obj_create(g);
    lv_obj_remove_flag(hole, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hole, NET_KEYHOLE_SZ, NET_KEYHOLE_SZ);
    lv_obj_center(hole);
    lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hole, current_theme.screen_base, 0);
    lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hole, 0, 0);
  }
}

static void add_signal_arc(lv_obj_t *card, int level) {
  int total_w = NET_BAR_COUNT * NET_BAR_W + (NET_BAR_COUNT - 1) * NET_BAR_GAP;
  int max_h = NET_BAR_H0 + (NET_BAR_COUNT - 1) * NET_BAR_STEP;
  lv_obj_t *arc = lv_obj_create(card);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(arc, total_w, max_h);
  lv_obj_align(arc, LV_ALIGN_RIGHT_MID, NET_ARC_X, 0);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(arc, 0, 0);
  lv_obj_set_style_pad_all(arc, 0, 0);

  for (int b = 0; b < NET_BAR_COUNT; b++) {
    int h = NET_BAR_H0 + b * NET_BAR_STEP;
    lv_obj_t *bar = lv_obj_create(arc);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, NET_BAR_W, h);
    lv_obj_set_pos(bar, b * (NET_BAR_W + NET_BAR_GAP), max_h - h);
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(
        bar, b < level ? lv_color_hex(COLOR_OPEN_HEX) : current_theme.border_inactive, 0);
  }
}

static void apply_net_selection(void) {
  for (int i = 0; i < s_ap_count; i++) {
    bool on = (i == s_sel);
    lv_obj_set_style_border_color(
        s_row[i], on ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(s_row[i], on ? NET_GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_row[i], on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  }
  if (s_ap_count > 0)
    lv_obj_scroll_to_view(s_row[s_sel], LV_ANIM_OFF);
}

static void build_join_list(void) {
  ui_chrome_header(s_screen, "Networks", "/assets/icons/wifi_find.bin");

  lv_obj_t *col = lv_obj_create(s_screen);
  lv_obj_set_size(col, ui_screen_w(), NET_BODY_H);
  lv_obj_align(col, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, NET_SIDE_PAD, 0);
  lv_obj_set_style_pad_row(col, NET_ROW_GAP, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);

  for (int i = 0; i < s_ap_count; i++) {
    lv_obj_t *card = lv_obj_create(col);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, NET_ROW_H);
    lv_obj_set_style_radius(card, NET_CARD_RADIUS, 0);
    lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    add_sec_glyph(card, s_aps[i].open);

    lv_obj_t *ssid = lv_label_create(card);
    lv_label_set_text(ssid, s_aps[i].ssid);
    lv_obj_set_style_text_color(ssid, current_theme.text_main, 0);
    lv_obj_set_style_text_font(ssid, &lv_font_montserrat_14, 0);
    lv_obj_align(ssid, LV_ALIGN_TOP_LEFT, NET_TEXT_X, 7);

    lv_obj_t *sec = lv_label_create(card);
    lv_label_set_text_fmt(sec, "%s  ch %d", s_aps[i].open ? "Open" : "WPA2", s_aps[i].channel);
    lv_obj_set_style_text_color(sec, current_theme.text_secondary, 0);
    lv_obj_set_style_text_font(sec, &lv_font_montserrat_12, 0);
    lv_obj_align(sec, LV_ALIGN_BOTTOM_LEFT, NET_TEXT_X, -6);

    add_signal_arc(card, signal_level(s_aps[i].rssi));
    s_row[i] = card;
  }

  apply_net_selection();
  s_join_footer = ui_chrome_footer(s_screen, JOIN_HINT);
}

static void screen_deleted(lv_event_t *event) {
  if (lv_event_get_target(event) == s_screen)
    ui_connect_wifi_close();
}

static void build_screen(void) {
  s_join_footer = NULL;
  if (s_screen != NULL) {
    lv_obj_t *old_screen = s_screen;
    s_screen = NULL;
    lv_obj_del(old_screen);
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_add_event_cb(s_screen, screen_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = (menu_component_t){0};

  if (s_scan_state == SCAN_RUNNING) {
    ui_chrome_header(s_screen, "Networks", "/assets/icons/wifi_find.bin");
    waves_create(s_screen,
                 LV_ALIGN_CENTER,
                 0,
                 SCAN_WAVES_Y_OFS,
                 LV_SYMBOL_WIFI,
                 "/assets/icons/wifi_find.bin");
    lv_obj_t *caption = lv_label_create(s_screen);
    lv_label_set_text(caption, "Scanning...");
    lv_obj_set_style_text_color(caption, current_theme.text_main, 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, SCAN_CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else if (s_scan_state == SCAN_FAIL) {
    s_menu = menu_component_create(s_screen, "Networks", "/assets/icons/wifi_find.bin");
    menu_component_add_item(&s_menu, "/assets/icons/wifi_find.bin", "Scan failed (C5?)");
  } else if (s_ap_count == 0) {
    s_menu = menu_component_create(s_screen, "Networks", "/assets/icons/wifi_find.bin");
    menu_component_add_item(&s_menu, "/assets/icons/wifi_find.bin", "No networks found");
  } else {
    build_join_list();
  }

  ui_input_set_screen_handler(connect_wifi_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_CONNECT_WIFI)
    return;
  build_screen();
  if (s_connect_timer == NULL)
    s_connect_timer = lv_timer_create(connect_done_cb, CONNECT_POLL_MS, NULL);
  if (s_connect_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create Wi-Fi connection timer");
    led_signal_error();
    s_scan_state = SCAN_FAIL;
    build_screen();
    return;
  }
  ESP_LOGI(TAG, "scan finished: state=%d, %d AP(s)", (int)s_scan_state, s_ap_count);
}

static void wifi_scan_task(void *arg) {
  (void)arg;
  int count = 0;

  wifi_live_scan_status_t status;
  wifi_live_scan_get_status(&status);
  esp_err_t scan_err = status.error;
  if (scan_err == ESP_OK || status.count > 0) {
    uint16_t total = status.count;
    for (uint16_t i = s_ap_page * WIFI_MAX_APS; i < total && count < WIFI_MAX_APS; i++) {
      wifi_ap_record_t record;
      const wifi_ap_record_t *ap = &record;
      if (wifi_live_scan_get_record(i, &record) != ESP_OK)
        continue;
      strncpy(s_aps[count].ssid, (const char *)ap->ssid, sizeof(s_aps[count].ssid) - 1);
      s_aps[count].ssid[sizeof(s_aps[count].ssid) - 1] = '\0';
      s_aps[count].rssi = ap->rssi;
      s_aps[count].open = (ap->authmode == WIFI_AUTH_OPEN);
      s_aps[count].channel = ap->primary;
      count++;
    }
  }

  for (int i = 1; i < count; i++) {
    for (int j = i; j > 0 && s_aps[j].rssi > s_aps[j - 1].rssi; j--) {
      ap_entry_t tmp = s_aps[j];
      s_aps[j] = s_aps[j - 1];
      s_aps[j - 1] = tmp;
    }
  }

  s_sel = 0;
  s_ap_count = count;
  s_scan_state = (scan_err == ESP_OK || count > 0) ? SCAN_DONE : SCAN_FAIL;
  s_scanning = false;
  scan_done_cb(NULL);
}

static void connect_wifi_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  const bool join = (s_scan_state == SCAN_DONE && s_ap_count > 0);
  if (connection_busy() && ev->button != INPUT_BTN_BACK)
    return;

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) {
        if (join) {
          s_sel = (s_sel + 1) % s_ap_count;
          apply_net_selection();
        } else {
          menu_component_next(&s_menu);
        }
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        if (join) {
          s_sel = (s_sel - 1 + s_ap_count) % s_ap_count;
          apply_net_selection();
        } else {
          menu_component_prev(&s_menu);
        }
      }
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_WIFI_SETTINGS);
      break;
    case INPUT_BTN_LEFT:
    case INPUT_BTN_RIGHT:
      if (press && !s_scanning &&
          wifi_live_scan_ui_change_page(&s_ap_page, WIFI_MAX_APS, ev->button == INPUT_BTN_RIGHT))
        wifi_scan_task(NULL);
      break;
    case INPUT_BTN_OK:
      if (press && join && s_sel >= 0 && s_sel < s_ap_count) {
        strncpy(s_connect_ssid, s_aps[s_sel].ssid, sizeof(s_connect_ssid) - 1);
        s_connect_ssid[sizeof(s_connect_ssid) - 1] = '\0';
        s_is_secured = !s_aps[s_sel].open;
        start_connection(s_is_secured ? NULL : "");
      }
      break;
    default:
      break;
  }
}

void ui_connect_wifi_close(void) {
  if (s_connect_timer != NULL) {
    lv_timer_delete(s_connect_timer);
    s_connect_timer = NULL;
  }
  portENTER_CRITICAL(&s_connect_lock);
  s_connect_cancelled = true;
  s_connect_state = CONNECT_STATE_IDLE;
  portEXIT_CRITICAL(&s_connect_lock);
  s_join_footer = NULL;
}

void ui_connect_wifi_open(void) {
  ui_connect_wifi_close();
  s_scanning = false;
  s_scan_state = SCAN_RUNNING;
  s_ap_count = 0;
  s_ap_page = 0;
  s_sel = 0;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (wifi_live_scan_ui_open(SPI_WIFI_BAND_DUAL, wifi_scan_task, NULL) != ESP_OK) {
      s_scanning = false;
      s_scan_state = SCAN_FAIL;
      build_screen();
    }
  }

  ESP_LOGI(TAG, "Networks screen opened (C5 hardware scan)");
}
