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

#include "subghz_menu_ui.h"

#include "esp_log.h"
#include "lvgl.h"
#include "st7789.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "sigwave_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "SUBGHZ_UI";

#define NAV_TIMER_MS 50
#define FADE_MS      200

#define OUTER_BORDER           4
#define TOP_BORDER_H           46
#define TOP_AREA_BORDER_WIDTH  3
#define TITLE_BAR_W            170
#define TITLE_BAR_H            30
#define TITLE_BAR_RADIUS       12
#define TITLE_BAR_BORDER_WIDTH 2

#define BAR_COUNT      16
#define BAR_W          8
#define BAR_GAP        4
#define BAR_MIN_H      6
#define BAR_MAX_H      96
#define BAR_BASELINE_Y 188
#define FREQ_CYCLE_MS  400

#define CARD_W       162
#define CARD_H       82
#define CARD_RISE_PX 70
#define CARD_RISE_MS 450

static const struct {
  const char *name;
  const char *icon;
  bool capture;
} ITEMS[] = {
    {"Read", "/assets/icons/signal_icon.bin", true},
    {"Read RAW", "/assets/icons/signal_icon.bin", true},
    {"Frequency Analyzer", "/assets/icons/search_menu_icon.bin", false},
    {"Brute Force", "/assets/icons/burst_menu_icon.bin", true},
    {"Saved", "/assets/icons/saved_icon.bin", false},
};
#define ITEM_COUNT ((int)(sizeof(ITEMS) / sizeof(ITEMS[0])))

#define IDX_ANALYZER 2
#define IDX_SAVED    4

static const char *SAVED_SIGS[] = {"Gate_433", "Doorbell", "TPMS_FL", "Garage"};
#define SAVED_COUNT ((int)(sizeof(SAVED_SIGS) / sizeof(SAVED_SIGS[0])))

static const char *ANALYZER_FREQS[] = {"433.92 MHz", "868.30 MHz", "315.00 MHz"};
#define ANALYZER_FREQ_COUNT ((int)(sizeof(ANALYZER_FREQS) / sizeof(ANALYZER_FREQS[0])))

typedef enum {
  VIEW_LIST = 0,
  VIEW_ANALYZER,
  VIEW_SAVED,
  VIEW_SAVED_INFO,
} view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_freq_timer = NULL;
static view_t s_view = VIEW_LIST;
static int s_saved_sel = 0;

static lv_obj_t *s_freq_lbl = NULL;
static int s_freq_idx = 0;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);

static void stop_freq_timer(void) {
  if (s_freq_timer != NULL) {
    lv_timer_delete(s_freq_timer);
    s_freq_timer = NULL;
  }
}

static void opa_anim_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fade_in(lv_obj_t *obj, uint32_t duration_ms) {
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_anim_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, duration_ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void build_title(const char *text) {
  lv_obj_t *top_area = lv_obj_create(s_screen);
  lv_obj_set_size(top_area, LCD_H_RES - OUTER_BORDER * 2, TOP_BORDER_H);
  lv_obj_align(top_area, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_remove_flag(top_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(top_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(top_area, TOP_AREA_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(top_area, current_theme.border_interface, 0);
  lv_obj_set_style_border_side(top_area, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_radius(top_area, 0, 0);
  lv_obj_set_style_pad_all(top_area, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(top_area);
  lv_obj_set_size(title_bar, TITLE_BAR_W, TITLE_BAR_H);
  lv_obj_align(title_bar, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(title_bar, TITLE_BAR_RADIUS, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(title_bar, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(title_bar, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_border_width(title_bar, TITLE_BAR_BORDER_WIDTH, 0);
  lv_obj_set_style_border_color(title_bar, current_theme.border_accent, 0);

  lv_obj_t *title_lbl = lv_label_create(title_bar);
  lv_label_set_text(title_lbl, text);
  lv_obj_set_style_text_color(title_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(title_lbl);
}

static void bar_height_cb(void *var, int32_t v) {
  lv_obj_t *bar = (lv_obj_t *)var;
  lv_obj_set_height(bar, v);
  lv_obj_set_y(bar, BAR_BASELINE_Y - v);
}

static void freq_cycle_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen || s_view != VIEW_ANALYZER) {
    lv_timer_delete(t);
    s_freq_timer = NULL;
    return;
  }
  s_freq_idx = (s_freq_idx + 1) % ANALYZER_FREQ_COUNT;
  if (s_freq_lbl)
    lv_label_set_text(s_freq_lbl, ANALYZER_FREQS[s_freq_idx]);
}

#define SIGNAL_STRONG_COLOR 0x00E676

static void build_analyzer(void) {
  ui_chrome_header(s_screen, "ANALYZER", "/assets/icons/search_menu_icon.bin");

  s_freq_idx = 0;
  s_freq_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_freq_lbl, ANALYZER_FREQS[0]);
  lv_obj_set_style_text_color(s_freq_lbl, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_freq_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_freq_lbl, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 14);

  int total_w = BAR_COUNT * BAR_W + (BAR_COUNT - 1) * BAR_GAP;
  int x0 = (LCD_H_RES - total_w) / 2;

  static lv_point_precise_t base_pts[2];
  base_pts[0].x = 0;
  base_pts[0].y = 0;
  base_pts[1].x = total_w;
  base_pts[1].y = 0;
  lv_obj_t *baseline = lv_line_create(s_screen);
  lv_line_set_points(baseline, base_pts, 2);
  lv_obj_set_pos(baseline, x0, BAR_BASELINE_Y);
  lv_obj_set_style_line_color(baseline, current_theme.border_inactive, 0);
  lv_obj_set_style_line_opa(baseline, LV_OPA_50, 0);
  lv_obj_set_style_line_width(baseline, 2, 0);
  lv_obj_set_style_line_dash_width(baseline, 4, 0);
  lv_obj_set_style_line_dash_gap(baseline, 4, 0);

  for (int i = 0; i < BAR_COUNT; i++) {
    int peak = BAR_MAX_H - (i % 5) * 12;

    lv_obj_t *bar = lv_obj_create(s_screen);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, BAR_W, BAR_MIN_H);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_color_t bar_color;
    if (peak >= (BAR_MAX_H - 12))
      bar_color = lv_color_hex(SIGNAL_STRONG_COLOR);
    else if (peak >= (BAR_MAX_H - 36))
      bar_color = current_theme.border_accent;
    else
      bar_color = current_theme.border_inactive;
    lv_obj_set_style_bg_color(bar, bar_color, 0);
    lv_obj_set_style_bg_grad_color(bar, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);

    lv_obj_set_pos(bar, x0 + i * (BAR_W + BAR_GAP), BAR_BASELINE_Y - BAR_MIN_H);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, bar_height_cb);
    lv_anim_set_values(&a, BAR_MIN_H, peak);
    lv_anim_set_duration(&a, 420 + (i % 4) * 90);
    lv_anim_set_playback_duration(&a, 420 + (i % 3) * 80);
    lv_anim_set_delay(&a, i * 55);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }

  fade_in(s_freq_lbl, FADE_MS);
  s_freq_timer = lv_timer_create(freq_cycle_cb, FREQ_CYCLE_MS, NULL);
}

#define SAVED_NAME_COLOR 0x00E676

static void build_saved_list(void) {
  s_menu = menu_component_create(s_screen, "SAVED", "/assets/icons/saved_icon.bin");
  for (int i = 0; i < SAVED_COUNT; i++) {
    menu_component_add_item(&s_menu, NULL, SAVED_SIGS[i]);
    menu_component_set_item_label_color(&s_menu, i, lv_color_hex(SAVED_NAME_COLOR));
  }
  if (s_saved_sel > 0 && s_saved_sel < SAVED_COUNT)
    menu_component_select(&s_menu, s_saved_sel);

  lv_obj_t *freq = lv_label_create(s_menu.title_bar);
  lv_label_set_text(freq, "433.92");
  lv_obj_set_style_text_color(freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(freq, &lv_font_montserrat_12, 0);
  lv_obj_align(freq, LV_ALIGN_RIGHT_MID, -10, 0);

  fade_in(s_menu.items_cont, FADE_MS);
  fade_in(s_menu.title_bar, FADE_MS);
}

static void card_rise_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void build_saved_info(void) {
  ui_chrome_header(s_screen, "SAVED", "/assets/icons/saved_icon.bin");

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, CARD_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(card, 6, 0);

  lv_obj_t *name = lv_label_create(card);
  lv_label_set_text(name, SAVED_SIGS[s_saved_sel]);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *proto = lv_label_create(card);
  lv_label_set_text(proto, "Princeton  0x1A2B3C");
  lv_obj_set_style_text_color(proto, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(proto, &lv_font_montserrat_12, 0);
  lv_obj_align(proto, LV_ALIGN_TOP_LEFT, 0, 20);

  sigwave_create_static(card, LV_ALIGN_BOTTOM_MID, 0, -2);

  ui_chrome_footer(s_screen, "BACK to return");

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_exec_cb(&a, card_rise_cb);
  lv_anim_set_values(&a, CARD_RISE_PX, 0);
  lv_anim_set_duration(&a, CARD_RISE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void build_screen(void) {
  stop_freq_timer();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_freq_lbl = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_view) {
    case VIEW_ANALYZER:
      build_analyzer();
      break;
    case VIEW_SAVED:
      build_saved_list();
      break;
    case VIEW_SAVED_INFO:
      build_saved_info();
      break;
    case VIEW_LIST:
    default:
      s_menu = menu_component_create(s_screen, "SUB-GHZ", "/assets/icons/radar_icon.bin");
      for (int i = 0; i < ITEM_COUNT; i++)
        menu_component_add_item(&s_menu, ITEMS[i].icon, ITEMS[i].name);
      fade_in(s_menu.items_cont, FADE_MS);
      fade_in(s_menu.title_bar, FADE_MS);
      break;
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;
  if (msgbox_is_open())
    return;

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  switch (s_view) {
    case VIEW_LIST:
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (ok && !s_ok_last) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel == IDX_ANALYZER) {
          s_view = VIEW_ANALYZER;
          build_screen();
          goto latch;
        } else if (sel == IDX_SAVED) {
          s_saved_sel = 0;
          s_view = VIEW_SAVED;
          build_screen();
          goto latch;
        } else if (sel >= 0 && sel < ITEM_COUNT && ITEMS[sel].capture) {
          ui_switch_screen(SCREEN_SUBGHZ_READ);
        }
      }
      if (back && !s_back_last)
        ui_switch_screen(SCREEN_MENU);
      break;

    case VIEW_ANALYZER:
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_SAVED:
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (ok && !s_ok_last) {
        s_saved_sel = menu_component_get_selected(&s_menu);
        ESP_LOGI(TAG, "mock saved open: %s", SAVED_SIGS[s_saved_sel]);
        s_view = VIEW_SAVED_INFO;
        build_screen();
        goto latch;
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_SAVED_INFO:
      if (back && !s_back_last) {
        s_view = VIEW_SAVED;
        build_screen();
        goto latch;
      }
      break;
  }

  s_up_last = up;
  s_down_last = down;
  s_ok_last = ok;
  s_back_last = back;
  return;

latch:

  s_up_last = up;
  s_down_last = down;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_subghz_menu_open(void) {
  s_view = VIEW_LIST;
  s_saved_sel = 0;
  build_screen();
}
