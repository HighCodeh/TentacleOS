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

#include "wifi_attack_ui.h"

#include "esp_log.h"
#include "st7789.h"

#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "WIFI_ATTACK_UI";

#define ATTACK_TICK_MS 140

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2

typedef struct {
  const char *name;
  const char *counter_label;
  int step;
  const char *target_ssid;
  int target_ch;
} attack_def_t;

static const attack_def_t ATTACKS[] = {
    {"Deauth", "Deauth sent", 3, "HOME-5G", 6},
    {"Beacon Spam", "Beacons", 11, "<broadcast>", 1},
    {"Probe Flood", "Probes", 7, "Cafe_Guest", 11},
    {"Auth Flood", "Auth", 5, "Office-WiFi", 3},
    {"Karma", "Probes", 4, "<any>", 9},
};
#define ATTACKS_COUNT (sizeof(ATTACKS) / sizeof(ATTACKS[0]))

static const char *const ATTACK_ICONS[] = {
    "/assets/icons/wifi_off.bin",
    "/assets/icons/wifi_tethering.bin",
    "/assets/icons/wifi_find.bin",
    "/assets/icons/bolt.bin",
    "/assets/icons/wifi_tethering.bin",
};

typedef enum {
  VIEW_LIST,
  VIEW_RUNNING,
} view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_attack_timer = NULL;

static view_t s_view = VIEW_LIST;
static int s_attack_idx = 0;
static long s_count = 0;
static long s_count_prev = 0;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_rate_label = NULL;
static lv_obj_t *s_waves = NULL;
static int s_tick_accum = 0;

static void wifi_attack_input(const input_event_t *ev, void *ctx);
static void attack_tick_cb(lv_timer_t *timer);
static void build_list_view(void);
static void build_running_view(int idx);
static void stop_attack_timer(void);

static void fade_in(lv_obj_t *obj, uint32_t ms) {
  if (obj != NULL)
    lv_obj_fade_in(obj, ms, 0);
}

static void blink_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void attach_blink(lv_obj_t *obj, uint32_t period_ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, blink_opa_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
  lv_anim_set_duration(&a, period_ms);
  lv_anim_set_playback_duration(&a, period_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

static void scanbar_x_cb(void *var, int32_t v) {
  lv_obj_set_x((lv_obj_t *)var, v);
}

void ui_wifi_attack_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_attack_timer = NULL;
  s_view = VIEW_LIST;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  build_list_view();

  ui_input_set_screen_handler(wifi_attack_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void clear_screen_children(void) {
  lv_obj_clean(s_screen);
  s_count_label = NULL;
  s_rate_label = NULL;
  s_waves = NULL;
}

static void build_list_view(void) {
  clear_screen_children();
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  s_menu = menu_component_create(s_screen, "ATTACKS", "/assets/icons/bolt.bin");
  for (size_t i = 0; i < ATTACKS_COUNT; i++)
    menu_component_add_item(&s_menu, ATTACK_ICONS[i], ATTACKS[i].name);

  fade_in(s_menu.title_bar, 200);
  fade_in(s_menu.items_cont, 200);

  s_view = VIEW_LIST;
}

static void build_running_view(int idx) {
  stop_attack_timer();
  clear_screen_children();

  s_attack_idx = idx;
  s_count = 0;
  s_count_prev = 0;
  s_tick_accum = 0;

  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *header = ui_chrome_header(s_screen, ATTACKS[idx].name, "/assets/icons/bolt.bin");

  lv_obj_t *status_label = lv_label_create(s_screen);
  lv_label_set_text(status_label, "Running...");
  lv_obj_set_style_text_color(status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 8);

  lv_obj_t *target_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(
      target_label, "AP: %s  CH %d", ATTACKS[idx].target_ssid, ATTACKS[idx].target_ch);
  lv_obj_set_style_text_color(target_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(target_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(target_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(target_label, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 30);

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, 14, LV_SYMBOL_WIFI, NULL);

  lv_obj_t *scan_track = lv_obj_create(s_screen);
  lv_obj_set_size(scan_track, 150, 6);
  lv_obj_remove_flag(scan_track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(scan_track, 3, 0);
  lv_obj_set_style_bg_color(scan_track, current_theme.border_inactive, 0);
  lv_obj_set_style_bg_opa(scan_track, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scan_track, 0, 0);
  lv_obj_set_style_pad_all(scan_track, 0, 0);
  lv_obj_align(scan_track, LV_ALIGN_BOTTOM_MID, 0, -64);

  lv_obj_t *scan_fill = lv_obj_create(scan_track);
  lv_obj_set_size(scan_fill, 44, 6);
  lv_obj_remove_flag(scan_fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(scan_fill, 3, 0);
  lv_obj_set_style_bg_color(scan_fill, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(scan_fill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(scan_fill, 0, 0);
  lv_obj_set_y(scan_fill, 0);
  {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scan_fill);
    lv_anim_set_exec_cb(&a, scanbar_x_cb);
    lv_anim_set_values(&a, 0, 150 - 44);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_playback_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }

  s_count_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_count_label, "%s: %ld", ATTACKS[idx].counter_label, s_count);
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_count_label, LV_ALIGN_BOTTOM_MID, 0, -42);

  s_rate_label = lv_label_create(s_screen);
  lv_label_set_text(s_rate_label, "rate: 0/s");
  lv_obj_set_style_text_color(s_rate_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_rate_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_rate_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_rate_label, LV_ALIGN_BOTTOM_MID, 0, -28);

  ui_chrome_footer(s_screen, "BACK to stop");

  fade_in(header, 200);
  fade_in(status_label, 200);
  fade_in(target_label, 200);
  fade_in(s_count_label, 200);
  fade_in(s_rate_label, 200);

  s_view = VIEW_RUNNING;
  s_attack_timer = lv_timer_create(attack_tick_cb, ATTACK_TICK_MS, NULL);
}

static void stop_attack_timer(void) {
  if (s_attack_timer != NULL) {
    lv_timer_delete(s_attack_timer);
    s_attack_timer = NULL;
  }
}

static void attack_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_attack_timer == timer)
      s_attack_timer = NULL;
    return;
  }
  if (s_view != VIEW_RUNNING || s_count_label == NULL)
    return;

  s_count += ATTACKS[s_attack_idx].step;
  lv_label_set_text_fmt(s_count_label, "%s: %ld", ATTACKS[s_attack_idx].counter_label, s_count);

  s_tick_accum++;
  int ticks_per_sec = (1000 + ATTACK_TICK_MS - 1) / ATTACK_TICK_MS;
  if (s_tick_accum >= ticks_per_sec && s_rate_label != NULL) {
    long delta = s_count - s_count_prev;
    long per_sec = delta * 1000 / (s_tick_accum * ATTACK_TICK_MS);
    lv_label_set_text_fmt(s_rate_label, "rate: %ld/s", per_sec);
    s_count_prev = s_count;
    s_tick_accum = 0;
  }
}

static void wifi_attack_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_view == VIEW_LIST) {
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
      case INPUT_BTN_OK:
        if (press) {
          int sel = menu_component_get_selected(&s_menu);
          if (sel >= 0 && sel < (int)ATTACKS_COUNT) {
            ESP_LOGI(TAG, "mock attack start: %s", ATTACKS[sel].name);
            build_running_view(sel);
          }
        }
        break;
      default:
        break;
    }
  } else {
    switch (ev->button) {
      case INPUT_BTN_BACK:
      case INPUT_BTN_LEFT:
        if (press) {
          stop_attack_timer();
          ESP_LOGI(TAG, "mock attack stopped: %s", ATTACKS[s_attack_idx].name);
          build_list_view();
        }
        break;
      default:
        break;
    }
  }
}
