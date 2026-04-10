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

#include "wifi_deauth_attack_ui.h"

#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "msgbox_ui.h"
#include "target_scanner.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_deauther.h"
#include "wifi_service.h"

static const char *TAG = "UI_DEAUTH_ATTACK";

/* ---- Layout constants ---- */
#define LABEL_TARGET_Y   30
#define LABEL_MODE_Y     55
#define LABEL_CLIENT_Y   80
#define BTN_ATTACK_W     170
#define BTN_ATTACK_H     45
#define BTN_ATTACK_Y     10
#define LABEL_PACKETS_Y  (-35)
#define LIST_W           230
#define LIST_H           160
#define LIST_Y           10
#define ITEM_H           40
#define ITEM_MARGIN_LEFT 8

/* ---- Style constants ---- */
#define STYLE_BORDER_W      2
#define STYLE_BORDER_W_ITEM 1
#define STYLE_PAD           4

/* ---- Timer periods ---- */
#define ATTACK_TICK_MS   200
#define CLIENT_SCAN_MS   500
#define PACKET_INCREMENT 10

typedef enum {
  DEAUTH_VIEW_APS = 0,
  DEAUTH_VIEW_ATTACK = 1,
  DEAUTH_VIEW_CLIENTS = 2,
} deauth_view_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list_cont = NULL;
static lv_obj_t *s_loading_label = NULL;
static lv_obj_t *s_lbl_target = NULL;
static lv_obj_t *s_lbl_mode = NULL;
static lv_obj_t *s_lbl_client = NULL;
static lv_obj_t *s_btn_attack = NULL;
static lv_obj_t *s_lbl_packets = NULL;
static lv_style_t s_style_menu;
static lv_style_t s_style_item;
static bool s_is_styles_init = false;

static deauth_view_t s_current_view = DEAUTH_VIEW_APS;
static wifi_ap_record_t s_selected_ap;
static bool s_is_broadcast_mode = true;
static bool s_is_attacking = false;
static uint32_t s_packet_count = 0;
static lv_timer_t *s_attack_timer = NULL;
static lv_timer_t *s_client_timer = NULL;
static bool s_has_client = false;
static uint8_t s_selected_client[6];
static uint16_t s_last_client_count = 0;

extern lv_group_t *main_group;

static void list_event_cb(lv_event_t *e);
static void show_client_view(void);

static void init_styles(void) {
  if (s_is_styles_init)
    return;

  lv_style_init(&s_style_menu);
  lv_style_set_bg_color(&s_style_menu, current_theme.screen_base);
  lv_style_set_bg_opa(&s_style_menu, LV_OPA_COVER);
  lv_style_set_border_width(&s_style_menu, STYLE_BORDER_W);
  lv_style_set_border_color(&s_style_menu, current_theme.border_interface);
  lv_style_set_radius(&s_style_menu, 0);
  lv_style_set_pad_all(&s_style_menu, STYLE_PAD);

  lv_style_init(&s_style_item);
  lv_style_set_bg_color(&s_style_item, current_theme.bg_item_bot);
  lv_style_set_bg_grad_color(&s_style_item, current_theme.bg_item_top);
  lv_style_set_bg_grad_dir(&s_style_item, LV_GRAD_DIR_VER);
  lv_style_set_border_width(&s_style_item, STYLE_BORDER_W_ITEM);
  lv_style_set_border_color(&s_style_item, current_theme.border_inactive);
  lv_style_set_radius(&s_style_item, 0);

  s_is_styles_init = true;
}

static void clear_list(void) {
  if (s_list_cont == NULL)
    return;
  uint32_t child_count = lv_obj_get_child_count(s_list_cont);
  for (uint32_t i = 0; i < child_count; i++) {
    lv_obj_del(lv_obj_get_child(s_list_cont, 0));
  }
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

static void item_focus_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *item = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_obj_set_style_border_color(item, ui_theme_get_accent(), 0);
    lv_obj_set_style_border_width(item, STYLE_BORDER_W, 0);
    lv_obj_scroll_to_view(item, LV_ANIM_ON);
  } else if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_set_style_border_color(item, current_theme.border_inactive, 0);
    lv_obj_set_style_border_width(item, STYLE_BORDER_W_ITEM, 0);
  } else if (code == LV_EVENT_KEY) {
    list_event_cb(e);
  }
}

static void update_attack_labels(void) {
  if (s_lbl_target != NULL) {
    lv_label_set_text_fmt(
        s_lbl_target, "Target: %s  CH:%d", s_selected_ap.ssid, s_selected_ap.primary);
  }
  if (s_lbl_mode != NULL) {
    lv_label_set_text_fmt(s_lbl_mode, "Mode: %s", s_is_broadcast_mode ? "Broadcast" : "Targeted");
  }
  if (s_lbl_client != NULL) {
    if (!s_is_broadcast_mode && s_has_client) {
      lv_label_set_text_fmt(s_lbl_client,
                            "Client: %02X:%02X:%02X:%02X:%02X:%02X",
                            s_selected_client[0],
                            s_selected_client[1],
                            s_selected_client[2],
                            s_selected_client[3],
                            s_selected_client[4],
                            s_selected_client[5]);
    } else if (!s_is_broadcast_mode) {
      lv_label_set_text(s_lbl_client, "Client: <select>");
    } else {
      lv_label_set_text(s_lbl_client, "");
    }
  }
  if (s_lbl_packets != NULL) {
    lv_label_set_text_fmt(s_lbl_packets, "Packets: %lu", (unsigned long)s_packet_count);
  }
  if (s_btn_attack != NULL) {
    lv_label_set_text(lv_obj_get_child(s_btn_attack, 0),
                      s_is_attacking ? "ATTACKING..." : "START ATTACK");
    lv_obj_set_style_bg_color(
        s_btn_attack, s_is_attacking ? current_theme.bg_item_top : current_theme.border_accent, 0);
  }
}

static void attack_tick_cb(lv_timer_t *t) {
  (void)t;
  if (!s_is_attacking)
    return;
  s_packet_count += PACKET_INCREMENT;
  update_attack_labels();
}

static void stop_attack(void) {
  if (s_is_attacking) {
    wifi_deauther_stop();
    s_is_attacking = false;
  }
  if (s_attack_timer != NULL) {
    lv_timer_del(s_attack_timer);
    s_attack_timer = NULL;
  }
}

static void start_attack(void) {
  if (s_is_broadcast_mode) {
    if (!wifi_deauther_start(&s_selected_ap, WIFI_DEAUTHER_TYPE_INVALID_AUTH, true)) {
      msgbox_open(LV_SYMBOL_CLOSE, "Failed to start attack", "OK", NULL, NULL);
      return;
    }
  } else {
    if (!s_has_client) {
      show_client_view();
      return;
    }
    if (!wifi_deauther_start_targeted(
            &s_selected_ap, s_selected_client, WIFI_DEAUTHER_TYPE_INVALID_AUTH)) {
      msgbox_open(LV_SYMBOL_CLOSE, "Failed to start attack", "OK", NULL, NULL);
      return;
    }
  }

  s_is_attacking = true;
  if (s_attack_timer != NULL)
    lv_timer_del(s_attack_timer);
  s_attack_timer = lv_timer_create(attack_tick_cb, ATTACK_TICK_MS, NULL);
}

static void show_attack_view(void) {
  clear_list();
  clear_loading();
  s_current_view = DEAUTH_VIEW_ATTACK;
  s_is_broadcast_mode = true;
  s_is_attacking = false;
  s_packet_count = 0;

  if (s_lbl_target != NULL)
    lv_obj_del(s_lbl_target);
  if (s_lbl_mode != NULL)
    lv_obj_del(s_lbl_mode);
  if (s_lbl_client != NULL)
    lv_obj_del(s_lbl_client);
  if (s_btn_attack != NULL)
    lv_obj_del(s_btn_attack);
  if (s_lbl_packets != NULL)
    lv_obj_del(s_lbl_packets);

  s_lbl_target = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_lbl_target, current_theme.text_main, 0);
  lv_obj_align(s_lbl_target, LV_ALIGN_TOP_MID, 0, LABEL_TARGET_Y);

  s_lbl_mode = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_lbl_mode, current_theme.text_main, 0);
  lv_obj_align(s_lbl_mode, LV_ALIGN_TOP_MID, 0, LABEL_MODE_Y);

  s_lbl_client = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_lbl_client, current_theme.text_main, 0);
  lv_obj_align(s_lbl_client, LV_ALIGN_TOP_MID, 0, LABEL_CLIENT_Y);

  s_btn_attack = lv_btn_create(s_screen);
  lv_obj_set_size(s_btn_attack, BTN_ATTACK_W, BTN_ATTACK_H);
  lv_obj_align(s_btn_attack, LV_ALIGN_CENTER, 0, BTN_ATTACK_Y);
  lv_obj_set_style_bg_color(s_btn_attack, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_btn_attack, STYLE_BORDER_W, 0);
  lv_obj_set_style_border_color(s_btn_attack, ui_theme_get_accent(), 0);

  lv_obj_t *lbl_btn = lv_label_create(s_btn_attack);
  lv_label_set_text(lbl_btn, "START ATTACK");
  lv_obj_center(lbl_btn);
  lv_obj_add_event_cb(s_btn_attack, list_event_cb, LV_EVENT_KEY, NULL);

  s_lbl_packets = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_lbl_packets, current_theme.text_main, 0);
  lv_obj_align(s_lbl_packets, LV_ALIGN_BOTTOM_MID, 0, LABEL_PACKETS_Y);

  update_attack_labels();

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_btn_attack);
    lv_group_focus_obj(s_btn_attack);
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
    lv_obj_set_size(item, lv_pct(100), ITEM_H);
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
    lv_obj_set_style_margin_left(lbl, ITEM_MARGIN_LEFT, 0);

    lv_obj_set_user_data(item, ap);
    lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
    if (main_group != NULL)
      lv_group_add_obj(main_group, item);
  }

  if (main_group != NULL) {
    lv_obj_t *first = lv_obj_get_child(s_list_cont, 0);
    if (first != NULL)
      lv_group_focus_obj(first);
  }
}

static void clear_client_timer(void) {
  if (s_client_timer != NULL) {
    lv_timer_del(s_client_timer);
    s_client_timer = NULL;
  }
}

static void populate_client_list(target_scanner_record_t *results, uint16_t count) {
  if (s_list_cont == NULL)
    return;
  clear_list();

  if (results == NULL || count == 0) {
    lv_obj_t *empty = lv_label_create(s_list_cont);
    lv_label_set_text(empty, "NO CLIENTS FOUND");
    lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
    if (main_group != NULL)
      lv_group_add_obj(main_group, empty);
    return;
  }

  for (uint16_t i = 0; i < count; i++) {
    target_scanner_record_t *rec = &results[i];
    lv_obj_t *item = lv_obj_create(s_list_cont);
    lv_obj_set_size(item, lv_pct(100), ITEM_H);
    lv_obj_add_style(item, &s_style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(item);
    lv_label_set_text(icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text_fmt(lbl,
                          "%02X:%02X:%02X:%02X:%02X:%02X  RSSI %d",
                          rec->client_mac[0],
                          rec->client_mac[1],
                          rec->client_mac[2],
                          rec->client_mac[3],
                          rec->client_mac[4],
                          rec->client_mac[5],
                          rec->rssi);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_margin_left(lbl, ITEM_MARGIN_LEFT, 0);

    lv_obj_set_user_data(item, rec);
    lv_obj_add_event_cb(item, item_focus_cb, LV_EVENT_ALL, NULL);
    if (main_group != NULL)
      lv_group_add_obj(main_group, item);
  }

  if (main_group != NULL) {
    lv_obj_t *first = lv_obj_get_child(s_list_cont, 0);
    if (first != NULL)
      lv_group_focus_obj(first);
  }
}

static void update_clients_cb(lv_timer_t *t) {
  (void)t;
  uint16_t count = 0;
  bool is_scanning = false;
  target_scanner_record_t *results = target_scanner_get_live_results(&count, &is_scanning);
  if (count != s_last_client_count) {
    s_last_client_count = count;
    populate_client_list(results, count);
  }
}

static void show_client_view(void) {
  clear_list();
  clear_loading();
  s_current_view = DEAUTH_VIEW_CLIENTS;
  s_last_client_count = 0;

  if (s_lbl_target != NULL) {
    lv_obj_del(s_lbl_target);
    s_lbl_target = NULL;
  }
  if (s_lbl_mode != NULL) {
    lv_obj_del(s_lbl_mode);
    s_lbl_mode = NULL;
  }
  if (s_lbl_client != NULL) {
    lv_obj_del(s_lbl_client);
    s_lbl_client = NULL;
  }
  if (s_btn_attack != NULL) {
    lv_obj_del(s_btn_attack);
    s_btn_attack = NULL;
  }
  if (s_lbl_packets != NULL) {
    lv_obj_del(s_lbl_packets);
    s_lbl_packets = NULL;
  }

  set_loading("SCANNING CLIENTS...");
  lv_refr_now(NULL);

  target_scanner_start(s_selected_ap.bssid, s_selected_ap.primary);
  clear_client_timer();
  s_client_timer = lv_timer_create(update_clients_cb, CLIENT_SCAN_MS, NULL);
}

static void list_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_current_view == DEAUTH_VIEW_CLIENTS) {
      target_scanner_free_results();
      clear_client_timer();
      show_attack_view();
      return;
    }
    if (s_current_view == DEAUTH_VIEW_ATTACK) {
      stop_attack();
      s_current_view = DEAUTH_VIEW_APS;
      if (s_lbl_target != NULL) {
        lv_obj_del(s_lbl_target);
        s_lbl_target = NULL;
      }
      if (s_lbl_mode != NULL) {
        lv_obj_del(s_lbl_mode);
        s_lbl_mode = NULL;
      }
      if (s_lbl_client != NULL) {
        lv_obj_del(s_lbl_client);
        s_lbl_client = NULL;
      }
      if (s_btn_attack != NULL) {
        lv_obj_del(s_btn_attack);
        s_btn_attack = NULL;
      }
      if (s_lbl_packets != NULL) {
        lv_obj_del(s_lbl_packets);
        s_lbl_packets = NULL;
      }
      clear_list();
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
    } else {
      stop_attack();
      ui_switch_screen(SCREEN_WIFI_ATTACK_MENU);
    }
  } else if (key == LV_KEY_ENTER) {
    if (s_current_view == DEAUTH_VIEW_CLIENTS) {
      lv_obj_t *focused = lv_group_get_focused(main_group);
      if (focused == NULL)
        return;
      target_scanner_record_t *rec = (target_scanner_record_t *)lv_obj_get_user_data(focused);
      if (rec == NULL)
        return;
      memcpy(s_selected_client, rec->client_mac, sizeof(s_selected_client));
      s_has_client = true;
      target_scanner_free_results();
      clear_client_timer();
      show_attack_view();
      return;
    }
    if (s_current_view == DEAUTH_VIEW_APS) {
      lv_obj_t *focused = lv_group_get_focused(main_group);
      if (focused == NULL)
        return;
      wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_obj_get_user_data(focused);
      if (ap == NULL)
        return;
      s_selected_ap = *ap;
      s_has_client = false;
      show_attack_view();
    } else if (s_current_view == DEAUTH_VIEW_ATTACK) {
      if (!s_is_attacking) {
        start_attack();
      } else {
        stop_attack();
      }
      update_attack_labels();
    }
  } else if (s_current_view == DEAUTH_VIEW_ATTACK) {
    if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
      s_is_broadcast_mode = !s_is_broadcast_mode;
      update_attack_labels();
    }
  }
}

void ui_wifi_deauth_attack_open(void) {
  init_styles();
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  header_ui_create(s_screen);
  footer_ui_create(s_screen);

  s_list_cont = lv_obj_create(s_screen);
  lv_obj_set_size(s_list_cont, LIST_W, LIST_H);
  lv_obj_align(s_list_cont, LV_ALIGN_CENTER, 0, LIST_Y);
  lv_obj_add_style(s_list_cont, &s_style_menu, 0);
  lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scrollbar_mode(s_list_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_list_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_list_cont, LV_DIR_VER);
  lv_obj_add_event_cb(s_list_cont, list_event_cb, LV_EVENT_KEY, NULL);

  set_loading("SCANNING APS...");
  lv_screen_load(s_screen);
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
