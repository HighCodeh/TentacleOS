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

#include "wifi_probe_mon_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "lvgl.h"

#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *SCAN_ICON = "/assets/icons/wifi_find.bin";

#define ADD_TICK_MS 900
#define PROBE_MAX    9
#define COLOR_SSID   0xB89AFF

typedef struct {
  char line[40];
} probe_row_t;

static const char *SSID_POOL[] = {
    "HOME-5G",
    "Starbucks",
    "iPhone de Ana",
    "NETVIRTUA_9988",
    "eduroam",
    "AndroidAP_77",
    "Free_WiFi",
    "GVT-A1B2",
    "Office-Guest",
    "MOVISTAR_2EF1",
};
#define SSID_POOL_N ((int)(sizeof(SSID_POOL) / sizeof(SSID_POOL[0])))

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_add_timer = NULL;

static int s_count = 0;
static int s_total = 0;
static probe_row_t s_rows[PROBE_MAX];

static void wifi_probe_mon_input(const input_event_t *ev, void *ctx);

static void make_row(probe_row_t *r) {
  const char *ssid = SSID_POOL[esp_random() % SSID_POOL_N];
  snprintf(r->line,
           sizeof(r->line),
           "%02X:%02X  %s",
           (unsigned)(esp_random() & 0xFF),
           (unsigned)(esp_random() & 0xFF),
           ssid);
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

  char title[24];
  snprintf(title, sizeof(title), "PROBES  %d", s_total);
  s_menu = menu_component_create(s_screen, title, SCAN_ICON);
  if (s_count == 0) {
    menu_component_add_item(&s_menu, SCAN_ICON, "Listening...");
  } else {
    for (int i = 0; i < s_count; i++) {
      menu_component_add_item(&s_menu, SCAN_ICON, s_rows[i].line);
      menu_component_set_item_label_color(&s_menu, i, lv_color_hex(COLOR_SSID));
    }
  }
  menu_component_set_hint(&s_menu, LV_SYMBOL_LEFT "  Back");

  ui_input_set_screen_handler(wifi_probe_mon_input, NULL);
  ui_screen_load(s_screen);
}

static void add_tick_cb(lv_timer_t *t) {
  if (ui_current_screen() != SCREEN_WIFI_PROBE_MON) {
    lv_timer_delete(t);
    s_add_timer = NULL;
    return;
  }
  if (s_count < PROBE_MAX) {
    make_row(&s_rows[s_count++]);
  } else {
    for (int i = 0; i < PROBE_MAX - 1; i++)
      s_rows[i] = s_rows[i + 1];
    make_row(&s_rows[PROBE_MAX - 1]);
  }
  s_total++;
  build_screen();
  ui_feedback(UI_FB_NAV);
}

static void wifi_probe_mon_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);
  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav)
        menu_component_next(&s_menu);
      break;
    case INPUT_BTN_UP:
      if (nav)
        menu_component_prev(&s_menu);
      break;
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_WIFI_MENU);
      break;
    default:
      break;
  }
}

void ui_wifi_probe_mon_open(void) {
  s_count = 0;
  s_total = 0;
  build_screen();
  if (s_add_timer == NULL)
    s_add_timer = lv_timer_create(add_tick_cb, ADD_TICK_MS, NULL);
}
