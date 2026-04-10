// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "wifi_auth_flood_ui.h"

#include "lvgl.h"
#include "core/lv_group.h"

#include "ui_theme.h"
#include "header_ui.h"
#include "footer_ui.h"
#include "ui_manager.h"
#include "lv_port_indev.h"
#include "wifi_service.h"
#include "wifi_flood.h"
#include "button_ui.h"

#define LIST_CONT_W              230
#define LIST_CONT_H              160
#define LIST_CONT_OFFSET_Y       10
#define LIST_ITEM_H              40
#define LIST_ITEM_BORDER_W       1
#define LIST_ITEM_BORDER_SEL     2
#define LIST_ITEM_LABEL_MARGIN_L 8

#define ATTACK_BTN_W            170
#define ATTACK_BTN_H            45
#define ATTACK_BTN_OFFSET_Y     10
#define TARGET_LABEL_OFFSET_Y   30
#define ATTEMPTS_LABEL_OFFSET_Y (-35)

#define ATTEMPTS_TICK_MS  200
#define ATTEMPTS_PER_TICK 10

#define TARGET_LABEL_FMT   "Target: %s  CH:%d"
#define ATTEMPTS_LABEL_FMT "Attempts: %lu"
#define BTN_LABEL_RUNNING  "FLOODING..."
#define BTN_LABEL_IDLE     "START FLOOD"

typedef enum {
  AUTH_FLOOD_VIEW_APS = 0,
  AUTH_FLOOD_VIEW_ATTACK = 1,
} auth_flood_view_t;

extern lv_group_t *main_group;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list_cont = NULL;
static lv_obj_t *s_loading_label = NULL;
static lv_obj_t *s_lbl_target = NULL;
static lv_obj_t *s_btn_attack = NULL;
static lv_obj_t *s_lbl_attempts = NULL;
static lv_style_t s_style_menu;
static lv_style_t s_style_item;
static bool s_styles_initialized = false;

static auth_flood_view_t s_current_view = AUTH_FLOOD_VIEW_APS;
static wifi_ap_record_t s_selected_ap;
static bool s_is_running = false;
static uint32_t s_attempts_count = 0;
static lv_timer_t *s_attempts_timer = NULL;

static void list_event_cb(lv_event_t *e);
static void init_styles(void);
static void clear_list(void);
static void set_loading(const char *text);
static void clear_loading(void);
static void update_attack_labels(void);
static void attempts_tick_cb(lv_timer_t *t);
static void stop_attack(void);
static void start_attack(void);
static void show_attack_view(void);
static void on_item_event(lv_event_t *e);
static void populate_ap_list(wifi_ap_record_t *results, uint16_t count);
static void scan_and_populate(void);

static void init_styles(void) {
  if (s_styles_initialized)
    return;

  lv_style_init(&s_style_menu);
  lv_style_set_bg_color(&s_style_menu, current_theme.screen_base);
  lv_style_set_bg_opa(&s_style_menu, LV_OPA_COVER);
  lv_style_set_border_width(&s_style_menu, 2);
  lv_style_set_border_color(&s_style_menu, current_theme.border_interface);
  lv_style_set_radius(&s_style_menu, 0);
  lv_style_set_pad_all(&s_style_menu, 4);

  lv_style_init(&s_style_item);
  lv_style_set_bg_color(&s_style_item, current_theme.bg_item_bot);
  lv_style_set_bg_grad_color(&s_style_item, current_theme.bg_item_top);
  lv_style_set_bg_grad_dir(&s_style_item, LV_GRAD_DIR_VER);
  lv_style_set_border_width(&s_style_item, LIST_ITEM_BORDER_W);
  lv_style_set_border_color(&s_style_item, current_theme.border_inactive);
  lv_style_set_radius(&s_style_item, 0);

  s_styles_initialized = true;
}

static void clear_list(void) {
  if (s_list_cont == NULL)
    return;

  uint32_t count = lv_obj_get_child_count(s_list_cont);
  for (uint32_t i = 0; i < count; i++)
    lv_obj_del(lv_obj_get_child(s_list_cont, 0));

  if (main_group != NULL)
    lv_group_remove_all_objs(main_group);
}

static void set_loading(const char *text) {
  if (s_loading_label == NULL) {
    s_loading_label = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_loading_label, current_theme.text_main, 0);
    lv_obj_center(s_loading_label);
  }
  lv_label_set_text(s_loading_label, text);
}

static void clear_loading(void) {
  if (s_loading_label != NULL) {
    lv_obj_del(s_loading_label);
    s_loading_label = NULL;
  }
}

static void update_attack_labels(void) {
  if (s_lbl_target != NULL)
    lv_label_set_text_fmt(
        s_lbl_target, TARGET_LABEL_FMT, s_selected_ap.ssid, s_selected_ap.primary);

  if (s_lbl_attempts != NULL)
    lv_label_set_text_fmt(s_lbl_attempts, ATTEMPTS_LABEL_FMT, (unsigned long)s_attempts_count);

  if (s_btn_attack != NULL) {
    lv_label_set_text(lv_obj_get_child(s_btn_attack, 0),
                      s_is_running ? BTN_LABEL_RUNNING : BTN_LABEL_IDLE);
    lv_obj_set_style_bg_color(s_btn_attack, current_theme.border_accent, 0);
  }
}

static void attempts_tick_cb(lv_timer_t *t) {
  if (!s_is_running)
    return;
  s_attempts_count += ATTEMPTS_PER_TICK;
  update_attack_labels();
}

static void stop_attack(void) {
  if (s_is_running) {
    wifi_flood_stop();
    s_is_running = false;
  }
  if (s_attempts_timer != NULL) {
    lv_timer_del(s_attempts_timer);
    s_attempts_timer = NULL;
  }
}

static void start_attack(void) {
  if (!wifi_flood_auth_start(s_selected_ap.bssid, s_selected_ap.primary))
    return;

  s_is_running = true;

  if (s_attempts_timer != NULL)
    lv_timer_del(s_attempts_timer);

  s_attempts_timer = lv_timer_create(attempts_tick_cb, ATTEMPTS_TICK_MS, NULL);
}

static void show_attack_view(void) {
  clear_list();
  clear_loading();

  s_current_view = AUTH_FLOOD_VIEW_ATTACK;
  s_is_running = false;
  s_attempts_count = 0;

  if (s_lbl_target != NULL) {
    lv_obj_del(s_lbl_target);
    s_lbl_target = NULL;
  }
  if (s_btn_attack != NULL) {
    lv_obj_del(s_btn_attack);
    s_btn_attack = NULL;
  }
  if (s_lbl_attempts != NULL) {
    lv_obj_del(s_lbl_attempts);
    s_lbl_attempts = NULL;
  }

  s_lbl_target = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_lbl_target, current_theme.text_main, 0);
  lv_obj_align(s_lbl_target, LV_ALIGN_TOP_MID, 0, TARGET_LABEL_OFFSET_Y);

  button_ui_t btn_ui =
      button_ui_create(s_screen, ATTACK_BTN_W, ATTACK_BTN_H, BTN_LABEL_IDLE, NULL, NULL);
  s_btn_attack = btn_ui.obj;
  lv_obj_align(s_btn_attack, LV_ALIGN_CENTER, 0, ATTACK_BTN_OFFSET_Y);
  lv_obj_add_event_cb(s_btn_attack, list_event_cb, LV_EVENT_KEY, NULL);

  s_lbl_attempts = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_lbl_attempts, current_theme.text_main, 0);
  lv_obj_align(s_lbl_attempts, LV_ALIGN_BOTTOM_MID, 0, ATTEMPTS_LABEL_OFFSET_Y);

  update_attack_labels();

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_btn_attack);
    lv_group_focus_obj(s_btn_attack);
  }
}

static void on_item_event(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *item = lv_event_get_target(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_obj_set_style_border_color(item, ui_theme_get_accent(), 0);
    lv_obj_set_style_border_width(item, LIST_ITEM_BORDER_SEL, 0);
    lv_obj_scroll_to_view(item, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(item, LIST_ITEM_BORDER_W, 0);
  } else if (code == LV_EVENT_KEY) {
    list_event_cb(e);
  }
}

static void populate_ap_list(wifi_ap_record_t *results, uint16_t count) {
  if (results == NULL || count == 0) {
    lv_obj_t *empty = lv_label_create(s_list_cont);
    lv_label_set_text(empty, "NO APS FOUND");
    lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
    if (main_group != NULL)
      lv_group_add_obj(main_group, empty);
    return;
  }

  for (uint16_t i = 0; i < count; i++) {
    wifi_ap_record_t *ap = &results[i];
    lv_obj_t *item = lv_obj_create(s_list_cont);
    lv_obj_set_size(item, lv_pct(100), LIST_ITEM_H);
    lv_obj_add_style(item, &s_style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(item);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, (char *)ap->ssid);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_margin_left(lbl, LIST_ITEM_LABEL_MARGIN_L, 0);

    lv_obj_set_user_data(item, ap);
    lv_obj_add_event_cb(item, on_item_event, LV_EVENT_ALL, NULL);

    if (main_group != NULL)
      lv_group_add_obj(main_group, item);
  }

  if (main_group != NULL) {
    lv_obj_t *first = lv_obj_get_child(s_list_cont, 0);
    if (first != NULL)
      lv_group_focus_obj(first);
  }
}

static void scan_and_populate(void) {
  set_loading("SCANNING APS...");
  lv_refr_now(NULL);

  if (!wifi_service_is_active()) {
    set_loading("WIFI OFF");
    return;
  }

  wifi_service_scan();
  clear_loading();

  uint16_t count = wifi_service_get_ap_count();
  wifi_ap_record_t *results = (count > 0) ? wifi_service_get_ap_record(0) : NULL;
  populate_ap_list(results, count);
}

static void list_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;

  uint32_t key = lv_event_get_key(e);

  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_current_view == AUTH_FLOOD_VIEW_ATTACK) {
      stop_attack();
      s_current_view = AUTH_FLOOD_VIEW_APS;

      if (s_lbl_target != NULL) {
        lv_obj_del(s_lbl_target);
        s_lbl_target = NULL;
      }
      if (s_btn_attack != NULL) {
        lv_obj_del(s_btn_attack);
        s_btn_attack = NULL;
      }
      if (s_lbl_attempts != NULL) {
        lv_obj_del(s_lbl_attempts);
        s_lbl_attempts = NULL;
      }

      clear_list();
      scan_and_populate();
    } else {
      stop_attack();
      ui_switch_screen(SCREEN_WIFI_ATTACK_MENU);
    }
    return;
  }

  if (key == LV_KEY_ENTER) {
    if (s_current_view == AUTH_FLOOD_VIEW_APS) {
      if (main_group == NULL)
        return;
      lv_obj_t *focused = lv_group_get_focused(main_group);
      if (focused == NULL)
        return;
      wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_obj_get_user_data(focused);
      if (ap == NULL)
        return;
      s_selected_ap = *ap;
      show_attack_view();
    } else {
      if (!s_is_running)
        start_attack();
      else
        stop_attack();
      update_attack_labels();
    }
  }
}

void ui_wifi_auth_flood_open(void) {
  init_styles();

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_list_cont = lv_obj_create(s_screen);
  lv_obj_set_size(s_list_cont, LIST_CONT_W, LIST_CONT_H);
  lv_obj_align(s_list_cont, LV_ALIGN_CENTER, 0, LIST_CONT_OFFSET_Y);
  lv_obj_add_style(s_list_cont, &s_style_menu, 0);
  lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_list_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_list_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_list_cont, LV_DIR_VER);
  lv_obj_add_event_cb(s_list_cont, list_event_cb, LV_EVENT_KEY, NULL);

  lv_screen_load(s_screen);
  scan_and_populate();
}