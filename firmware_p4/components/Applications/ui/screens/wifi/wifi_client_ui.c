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

#include "wifi_client_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "st7789.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "WIFI_CLI_UI";

#define NAV_TIMER_MS     50
#define CLI_HEADER_ICON  "/assets/icons/devices.bin"
#define CLI_STATUS_ICON  "/assets/icons/wifi_find.bin"
#define CLI_MAX          12
#define SCAN_SIM_STEPS   4
#define SCAN_SIM_STEP_MS 220
#define TASK_STACK_SIZE  4096
#define TASK_PRIORITY SYS_PRIO_SERVICE_LO

#define COLOR_STRONG_HEX 0x00E676
#define COLOR_WEAK_HEX   0xF5B13D
#define COLOR_DIM_HEX    0x8A8594
#define RSSI_STRONG_DBM  -60

#define SCAN_WAVES_Y_OFS   -6
#define SCAN_CAPTION_Y_OFS 78

#define MAP_BODY_H      (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)
#define MAP_PAD         10
#define MAP_AP_X        8
#define MAP_AP_W        96
#define MAP_AP_H        44
#define MAP_CLI_X       140
#define MAP_CLI_W       92
#define MAP_CLI_H       40
#define MAP_LINK_START  (MAP_AP_X + MAP_AP_W)
#define MAP_LINK_END    MAP_CLI_X
#define MAP_CARD_RADIUS 8
#define MAP_GLOW_W      12
#define MAP_LINK_W_SEL  3
#define MAP_LINK_W      1

typedef enum { SCAN_RUNNING, SCAN_DONE, SCAN_FAIL } scan_state_t;

typedef struct {
  uint8_t addr[6];
  uint8_t channel;
  int8_t rssi;
} mock_client_t;

static const mock_client_t MOCK_CLIENTS[] = {
    {{0x3C, 0x5A, 0xB4, 0x11, 0x22, 0x33}, 6, -47},
    {{0xA4, 0x77, 0x33, 0xDE, 0xAD, 0xBE}, 1, -58},
    {{0x08, 0x00, 0x27, 0x0A, 0x1B, 0x2C}, 11, -63},
    {{0xF0, 0x9F, 0xC2, 0x44, 0x55, 0x66}, 6, -71},
};
#define MOCK_CLIENT_COUNT (sizeof(MOCK_CLIENTS) / sizeof(MOCK_CLIENTS[0]))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;

static scan_state_t s_scan_state = SCAN_RUNNING;
static bool s_scanning = false;

static int s_cli_count = 0;
static uint8_t s_cli_addr[CLI_MAX][6];
static uint8_t s_cli_channel[CLI_MAX];
static int8_t s_cli_rssi[CLI_MAX];
static int s_cli_ap[CLI_MAX];

static int s_ap_count = 0;
static uint8_t s_ap_channel[CLI_MAX];
static int s_ap_sta[CLI_MAX];

static int s_sel = 0;
static lv_obj_t *s_cli_card[CLI_MAX];
static lv_obj_t *s_ap_card[CLI_MAX];
static lv_obj_t *s_link[CLI_MAX];
static lv_point_precise_t s_link_pts[CLI_MAX][2];

static bool s_btn_up_last = false;
static bool s_btn_down_last = false;
static bool s_btn_left_last = false;
static bool s_btn_right_last = false;
static bool s_btn_ok_last = false;
static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *t);

static uint32_t link_color(int8_t rssi) {
  return rssi >= RSSI_STRONG_DBM ? COLOR_STRONG_HEX : COLOR_WEAK_HEX;
}

static void derive_aps(void) {
  s_ap_count = 0;
  for (int i = 0; i < s_cli_count; i++) {
    int found = -1;
    for (int a = 0; a < s_ap_count; a++) {
      if (s_ap_channel[a] == s_cli_channel[i]) {
        found = a;
        break;
      }
    }
    if (found < 0) {
      found = s_ap_count;
      s_ap_channel[found] = s_cli_channel[i];
      s_ap_sta[found] = 0;
      s_ap_count++;
    }
    s_cli_ap[i] = found;
    s_ap_sta[found]++;
  }
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_radius(card, MAP_CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_inactive, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  return card;
}

static void
add_card_text(lv_obj_t *card, const char *title, lv_color_t title_color, const char *sub) {
  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, title_color, 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 5);

  lv_obj_t *s = lv_label_create(card);
  lv_label_set_text(s, sub);
  lv_obj_set_style_text_color(s, lv_color_hex(COLOR_DIM_HEX), 0);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
  lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -4);
}

static int ap_center_y(int idx) {
  int usable = MAP_BODY_H - MAP_PAD * 2;
  return MAP_PAD + (idx * 2 + 1) * usable / (s_ap_count * 2);
}

static int cli_center_y(int idx) {
  int usable = MAP_BODY_H - MAP_PAD * 2;
  return MAP_PAD + (idx * 2 + 1) * usable / (s_cli_count * 2);
}

static void apply_selection(void) {
  int sel_ap = s_cli_ap[s_sel];
  for (int a = 0; a < s_ap_count; a++) {
    bool on = (a == sel_ap);
    lv_obj_set_style_border_color(
        s_ap_card[a], on ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(s_ap_card[a], on ? MAP_GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_ap_card[a], on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
  }
  for (int i = 0; i < s_cli_count; i++) {
    bool on = (i == s_sel);
    lv_obj_set_style_border_color(
        s_cli_card[i], on ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(s_cli_card[i], on ? MAP_GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_cli_card[i], on ? LV_OPA_50 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_line_width(s_link[i], on ? MAP_LINK_W_SEL : MAP_LINK_W, 0);
    lv_obj_set_style_line_color(
        s_link[i], on ? current_theme.border_accent : lv_color_hex(link_color(s_cli_rssi[i])), 0);
  }
}

static void build_map(void) {
  ui_chrome_header(s_screen, "Clients", CLI_HEADER_ICON);

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_set_size(body, LCD_H_RES, MAP_BODY_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);

  for (int i = 0; i < s_cli_count; i++) {
    int ay = ap_center_y(s_cli_ap[i]);
    int cy = cli_center_y(i);
    s_link_pts[i][0].x = MAP_LINK_START;
    s_link_pts[i][0].y = ay;
    s_link_pts[i][1].x = MAP_LINK_END;
    s_link_pts[i][1].y = cy;
    lv_obj_t *line = lv_line_create(body);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_line_set_points(line, s_link_pts[i], 2);
    s_link[i] = line;
  }

  for (int a = 0; a < s_ap_count; a++) {
    int y = ap_center_y(a) - MAP_AP_H / 2;
    lv_obj_t *card = make_card(body, MAP_AP_X, y, MAP_AP_W, MAP_AP_H);
    char title[12];
    char sub[16];
    snprintf(title, sizeof(title), "CH %d", s_ap_channel[a]);
    snprintf(sub, sizeof(sub), "%d sta", s_ap_sta[a]);
    add_card_text(card, title, current_theme.text_main, sub);
    s_ap_card[a] = card;
  }

  for (int i = 0; i < s_cli_count; i++) {
    int y = cli_center_y(i) - MAP_CLI_H / 2;
    lv_obj_t *card = make_card(body, MAP_CLI_X, y, MAP_CLI_W, MAP_CLI_H);
    char title[12];
    char sub[12];
    snprintf(title, sizeof(title), "STA %02X", s_cli_addr[i][5]);
    snprintf(sub, sizeof(sub), "%d dBm", s_cli_rssi[i]);
    add_card_text(card, title, lv_color_hex(link_color(s_cli_rssi[i])), sub);
    s_cli_card[i] = card;
  }

  apply_selection();
  ui_chrome_footer(s_screen,
                   LV_SYMBOL_UP LV_SYMBOL_DOWN " Sel  " LV_SYMBOL_OK " Detail  " LV_SYMBOL_LEFT
                                               " Back");
}

static void open_detail(void) {
  int ap = s_cli_ap[s_sel];
  char msg[96];
  snprintf(msg,
           sizeof(msg),
           "%02X:%02X:%02X:%02X:%02X:%02X\nCH %d   %d dBm\nAP: CH %d (%d sta)",
           s_cli_addr[s_sel][0],
           s_cli_addr[s_sel][1],
           s_cli_addr[s_sel][2],
           s_cli_addr[s_sel][3],
           s_cli_addr[s_sel][4],
           s_cli_addr[s_sel][5],
           s_cli_channel[s_sel],
           s_cli_rssi[s_sel],
           s_ap_channel[ap],
           s_ap_sta[ap]);
  msgbox_open_info(
      "/assets/icons/smartphone.bin", "Client", msg, lv_color_hex(link_color(s_cli_rssi[s_sel])));
}

static void build_screen(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = (menu_component_t){0};

  if (s_scan_state == SCAN_RUNNING) {
    ui_chrome_header(s_screen, "Clients", CLI_HEADER_ICON);
    waves_create(s_screen, LV_ALIGN_CENTER, 0, SCAN_WAVES_Y_OFS, LV_SYMBOL_WIFI, CLI_STATUS_ICON);
    lv_obj_t *caption = lv_label_create(s_screen);
    lv_label_set_text(caption, "Scanning...");
    lv_obj_set_style_text_color(caption, current_theme.text_main, 0);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(caption, LV_ALIGN_CENTER, 0, SCAN_CAPTION_Y_OFS);
    ui_chrome_footer(s_screen, LV_SYMBOL_LEFT "  Back");
  } else if (s_scan_state == SCAN_FAIL) {
    s_menu = menu_component_create(s_screen, "Clients", CLI_HEADER_ICON);
    menu_component_add_item(&s_menu, CLI_STATUS_ICON, "Scan failed (C5?)");
  } else if (s_cli_count == 0) {
    s_menu = menu_component_create(s_screen, "Clients", CLI_HEADER_ICON);
    menu_component_add_item(&s_menu, CLI_STATUS_ICON, "No clients found");
  } else {
    build_map();
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void scan_done_cb(void *unused) {
  (void)unused;
  if (ui_current_screen() != SCREEN_WIFI_CLIENTS)
    return;
  build_screen();
  ESP_LOGI(TAG, "client scan finished: state=%d, %d client(s)", (int)s_scan_state, s_cli_count);
}

static void wifi_client_task(void *arg) {
  (void)arg;
  int count = 0;

  for (int i = 0; i < SCAN_SIM_STEPS; i++)
    vTaskDelay(pdMS_TO_TICKS(SCAN_SIM_STEP_MS));

  for (size_t i = 0; i < MOCK_CLIENT_COUNT && count < CLI_MAX; i++) {
    memcpy(s_cli_addr[count], MOCK_CLIENTS[i].addr, sizeof(s_cli_addr[count]));
    s_cli_channel[count] = MOCK_CLIENTS[i].channel;
    s_cli_rssi[count] = MOCK_CLIENTS[i].rssi;
    count++;
  }

  s_cli_count = count;
  derive_aps();
  s_scan_state = SCAN_DONE;
  s_scanning = false;
  lv_async_call(scan_done_cb, NULL);
  vTaskDelete(NULL);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (msgbox_is_open() || ui_input_is_locked()) {
    s_btn_up_last = up;
    s_btn_down_last = down;
    s_btn_left_last = left;
    s_btn_right_last = right;
    s_btn_ok_last = ok;
    s_btn_back_last = back;
    return;
  }

  bool map = (s_scan_state == SCAN_DONE && s_cli_count > 0);

  if (down && !s_btn_down_last) {
    if (map) {
      s_sel = (s_sel + 1) % s_cli_count;
      apply_selection();
    } else {
      menu_component_next(&s_menu);
    }
  }
  if (up && !s_btn_up_last) {
    if (map) {
      s_sel = (s_sel - 1 + s_cli_count) % s_cli_count;
      apply_selection();
    } else {
      menu_component_prev(&s_menu);
    }
  }

  if ((back && !s_btn_back_last) || (left && !s_btn_left_last))
    ui_switch_screen(SCREEN_WIFI_MENU);

  if (((ok && !s_btn_ok_last) || (right && !s_btn_right_last)) && map)
    open_detail();

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_wifi_client_open(void) {
  s_scan_state = SCAN_RUNNING;
  s_cli_count = 0;
  s_ap_count = 0;
  s_sel = 0;
  build_screen();

  if (!s_scanning) {
    s_scanning = true;
    if (xTaskCreatePinnedToCore(wifi_client_task, "wifi_cli", TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL, SYS_CORE_UI) !=
        pdPASS) {
      s_scanning = false;
      s_scan_state = SCAN_FAIL;
      build_screen();
    }
  }

  ESP_LOGI(TAG, "Client scan screen opened (mock scan)");
}
