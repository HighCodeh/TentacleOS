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

#include "wifi_evil_twin_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "evil_twin.h"
#include "footer_ui.h"
#include "header_ui.h"
#include "lv_port_indev.h"
#include "msgbox_ui.h"
#include "storage_assets.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "wifi_service.h"

static const char *TAG = "UI_EVIL_TWIN";

/* ---- Layout constants ---- */
#define LIST_W              230
#define LIST_H              160
#define LIST_Y              10
#define ITEM_H              40
#define ITEM_MARGIN_LEFT    8
#define TERMINAL_W          230
#define TERMINAL_H          125
#define TERMINAL_BORDER_W   1
#define STYLE_BORDER_W      2
#define STYLE_BORDER_W_ITEM 1
#define STYLE_PAD           4

/* ---- Timer / sizing ---- */
#define MONITOR_TIMER_MS  500
#define TEMPLATE_MAX      12
#define TEMPLATE_NAME_MAX 32
#define TEMPLATE_PATH_MAX 96
#define PASSWORD_MAX      64
#define TERMINAL_TEXT_MAX 256
#define DIR_PATH_MAX      128
#define HTML_EXT_LEN      5

typedef enum {
  EVIL_TWIN_VIEW_APS = 0,
  EVIL_TWIN_VIEW_TEMPLATE = 1,
  EVIL_TWIN_VIEW_MONITOR = 2,
} evil_twin_view_t;

typedef struct {
  char name[TEMPLATE_NAME_MAX];
  char path[TEMPLATE_PATH_MAX];
} template_item_t;

static template_item_t s_templates[TEMPLATE_MAX];
static int s_template_count = 0;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list_cont = NULL;
static lv_obj_t *s_loading_label = NULL;
static lv_obj_t *s_ta_term = NULL;
static lv_style_t s_style_menu;
static lv_style_t s_style_item;
static bool s_is_styles_init = false;

static evil_twin_view_t s_current_view = EVIL_TWIN_VIEW_APS;
static wifi_ap_record_t s_selected_ap;
static int s_selected_template = 0;
static lv_timer_t *s_monitor_timer = NULL;
static bool s_is_running = false;
static char s_last_password[PASSWORD_MAX];

extern lv_group_t *main_group;

static void list_event_cb(lv_event_t *e);
static void populate_template_list(void);
static bool load_templates(void);

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

static void update_monitor_terminal(void) {
  if (s_ta_term == NULL)
    return;
  const char *status = "Aguardando vitima...";
  if (evil_twin_has_password()) {
    evil_twin_get_last_password(s_last_password, sizeof(s_last_password));
    status = "Vitima Conectada!";
  }
  char text[TERMINAL_TEXT_MAX];
  snprintf(text,
           sizeof(text),
           "Target: %s\n"
           "Template: %s\n"
           "%s\n"
           "Senha: %s\n",
           s_selected_ap.ssid,
           (s_template_count > 0) ? s_templates[s_selected_template].name : "-",
           status,
           evil_twin_has_password() ? s_last_password : "-");
  lv_textarea_set_text(s_ta_term, text);
}

static void stop_attack(void) {
  if (s_monitor_timer != NULL) {
    lv_timer_del(s_monitor_timer);
    s_monitor_timer = NULL;
  }
  if (s_is_running) {
    evil_twin_stop_attack();
    s_is_running = false;
  }
}

static void monitor_timer_cb(lv_timer_t *t) {
  (void)t;
  update_monitor_terminal();
}

static void show_monitor_view(void) {
  clear_list();
  clear_loading();
  s_current_view = EVIL_TWIN_VIEW_MONITOR;

  if (s_list_cont != NULL)
    lv_obj_add_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
  if (s_ta_term != NULL)
    lv_obj_del(s_ta_term);

  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }
  size_t sz = 0;
  if (s_template_count == 0) {
    msgbox_open(LV_SYMBOL_CLOSE, "NO TEMPLATES FOUND", "OK", NULL, NULL);
    s_current_view = EVIL_TWIN_VIEW_TEMPLATE;
    if (s_list_cont != NULL)
      lv_obj_clear_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
    populate_template_list();
    return;
  }
  char *probe = (char *)storage_assets_load_file(s_templates[s_selected_template].path, &sz);
  if (probe == NULL) {
    msgbox_open(LV_SYMBOL_CLOSE, "TEMPLATE NOT FOUND", "OK", NULL, NULL);
    s_current_view = EVIL_TWIN_VIEW_TEMPLATE;
    if (s_list_cont != NULL)
      lv_obj_clear_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
    populate_template_list();
    return;
  }
  free(probe);

  s_ta_term = lv_textarea_create(s_screen);
  lv_obj_set_size(s_ta_term, TERMINAL_W, TERMINAL_H);
  lv_obj_align(s_ta_term, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_font(s_ta_term, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_ta_term, current_theme.screen_base, 0);
  lv_obj_set_style_text_color(s_ta_term, current_theme.border_accent, 0);
  lv_obj_set_style_radius(s_ta_term, 0, 0);
  lv_obj_set_style_border_width(s_ta_term, TERMINAL_BORDER_W, 0);
  lv_obj_set_style_border_color(s_ta_term, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(s_ta_term, TERMINAL_BORDER_W, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_color(s_ta_term, current_theme.border_accent, LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(s_ta_term, 0, LV_STATE_FOCUS_KEY);

  evil_twin_reset_capture();
  memset(s_last_password, 0, sizeof(s_last_password));
  evil_twin_start_attack_with_template((const char *)s_selected_ap.ssid,
                                       s_templates[s_selected_template].path);
  s_is_running = true;

  update_monitor_terminal();
  if (s_monitor_timer != NULL)
    lv_timer_del(s_monitor_timer);
  s_monitor_timer = lv_timer_create(monitor_timer_cb, MONITOR_TIMER_MS, NULL);

  lv_obj_add_event_cb(s_ta_term, list_event_cb, LV_EVENT_KEY, NULL);
  lv_obj_add_event_cb(s_screen, list_event_cb, LV_EVENT_KEY, NULL);

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, s_ta_term);
    lv_group_focus_obj(s_ta_term);
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

static void populate_template_list(void) {
  clear_list();
  if (s_template_count == 0) {
    lv_obj_t *empty = lv_label_create(s_list_cont);
    lv_label_set_text(empty, "NO TEMPLATES FOUND");
    lv_obj_set_style_text_color(empty, current_theme.text_main, 0);
    if (main_group != NULL)
      lv_group_add_obj(main_group, empty);
    return;
  }
  for (int i = 0; i < s_template_count; i++) {
    lv_obj_t *item = lv_obj_create(s_list_cont);
    lv_obj_set_size(item, lv_pct(100), ITEM_H);
    lv_obj_add_style(item, &s_style_item, 0);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(item);
    lv_label_set_text(icon, LV_SYMBOL_FILE);
    lv_obj_set_style_text_color(icon, current_theme.text_main, 0);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, s_templates[i].name);
    lv_obj_set_style_text_color(lbl, current_theme.text_main, 0);
    lv_obj_set_flex_grow(lbl, 1);
    lv_obj_set_style_margin_left(lbl, ITEM_MARGIN_LEFT, 0);

    lv_obj_set_user_data(item, (void *)(uintptr_t)i);
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

static bool load_templates(void) {
  s_template_count = 0;
  if (!storage_assets_is_mounted()) {
    storage_assets_init();
  }
  const char *base = storage_assets_get_mount_point();
  char dir_path[DIR_PATH_MAX];
  snprintf(dir_path, sizeof(dir_path), "%s/html/captive_portal", base);

  DIR *dir = opendir(dir_path);
  if (dir == NULL) {
    return false;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;
    const char *name = entry->d_name;
    size_t len = strlen(name);
    if (len < (HTML_EXT_LEN + 1))
      continue;
    if (strcmp(name + len - HTML_EXT_LEN, ".html") != 0)
      continue;
    if (s_template_count >= TEMPLATE_MAX)
      break;

    template_item_t *t = &s_templates[s_template_count++];
    const char *prefix = "html/captive_portal/";
    int max_name = (int)sizeof(t->path) - (int)strlen(prefix) - 1;
    if (max_name <= 0) {
      continue;
    }
    snprintf(t->path, sizeof(t->path), "%s%.*s", prefix, max_name, name);
    size_t base_len = len - HTML_EXT_LEN;
    if (base_len >= sizeof(t->name))
      base_len = sizeof(t->name) - 1;
    memcpy(t->name, name, base_len);
    t->name[base_len] = '\0';
  }

  closedir(dir);
  return s_template_count > 0;
}

static void list_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  uint32_t key = lv_event_get_key(e);

  if (key == LV_KEY_ESC || key == LV_KEY_LEFT) {
    if (s_current_view == EVIL_TWIN_VIEW_MONITOR) {
      stop_attack();
      if (s_ta_term != NULL) {
        lv_obj_del(s_ta_term);
        s_ta_term = NULL;
      }
      if (s_list_cont != NULL)
        lv_obj_clear_flag(s_list_cont, LV_OBJ_FLAG_HIDDEN);
      s_current_view = EVIL_TWIN_VIEW_TEMPLATE;
      populate_template_list();
      return;
    }
    if (s_current_view == EVIL_TWIN_VIEW_TEMPLATE) {
      s_current_view = EVIL_TWIN_VIEW_APS;
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
      return;
    }
    ui_switch_screen(SCREEN_WIFI_MENU);
    return;
  }

  if (key == LV_KEY_ENTER) {
    if (s_current_view == EVIL_TWIN_VIEW_APS) {
      lv_obj_t *focused = lv_group_get_focused(main_group);
      if (focused == NULL)
        return;
      wifi_ap_record_t *ap = (wifi_ap_record_t *)lv_obj_get_user_data(focused);
      if (ap == NULL)
        return;
      s_selected_ap = *ap;
      s_current_view = EVIL_TWIN_VIEW_TEMPLATE;
      load_templates();
      populate_template_list();
    } else if (s_current_view == EVIL_TWIN_VIEW_TEMPLATE) {
      lv_obj_t *focused = lv_group_get_focused(main_group);
      if (focused == NULL)
        return;
      s_selected_template = (int)(uintptr_t)lv_obj_get_user_data(focused);
      show_monitor_view();
    }
  }
}

void ui_wifi_evil_twin_open(void) {
  init_styles();
  if (s_screen != NULL)
    lv_obj_del(s_screen);

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

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
  lv_obj_add_event_cb(s_screen, list_event_cb, LV_EVENT_KEY, NULL);

  set_loading("SCANNING APS...");
  lv_screen_load(s_screen);
  lv_refr_now(NULL);

  s_current_view = EVIL_TWIN_VIEW_APS;
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

void ui_wifi_evil_twin_set_ssid(const char *ssid) {
  (void)ssid;
}
