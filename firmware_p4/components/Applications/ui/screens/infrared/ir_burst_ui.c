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

#include "ir_burst_ui.h"

#include "esp_log.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "IR_BURST_UI";

#define SIG_GREEN 0x00E676

#define HEADER_TITLE_Y     10
#define HEADER_RULE_Y      32
#define HEADER_RULE_W      70
#define HEADER_RULE_H      2
#define HEADER_RULE_RADIUS 1

#define STATUS_Y 48
#define COUNT_Y  66
#define WAVES_Y  8

#define NAV_TIMER_INTERVAL_MS 50
#define BURST_TICK_MS         180
#define BURST_TOTAL           24

#define STATUS_BUSY "Burst running..."
#define STATUS_DONE "Burst complete!"
#define HINT_BUSY   "BACK to cancel"
#define HINT_DONE   "BACK = Exit"

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_burst_timer = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_footer = NULL;
static lv_obj_t *s_waves = NULL;
static int s_sent = 0;

static bool s_btn_back_last = false;

static void nav_timer_cb(lv_timer_t *timer);
static void burst_tick_cb(lv_timer_t *timer);

void ui_ir_burst_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sent = 0;
  s_burst_timer = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "IR BURST", "/assets/icons/burst_menu_icon.bin");

  s_status_label = lv_label_create(s_screen);
  lv_label_set_text(s_status_label, STATUS_BUSY);
  lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_count_label = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_count_label, "0 / %d files", BURST_TOTAL);
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(s_count_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_count_label, LV_ALIGN_TOP_MID, 0, COUNT_Y);

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y, LV_SYMBOL_UPLOAD, NULL);

  s_footer = ui_chrome_footer(s_screen, HINT_BUSY);

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_INTERVAL_MS, NULL);
  s_burst_timer = lv_timer_create(burst_tick_cb, BURST_TICK_MS, NULL);

  ui_screen_load(s_screen);
}

static void burst_tick_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    if (s_burst_timer == timer)
      s_burst_timer = NULL;
    return;
  }

  s_sent++;
  lv_label_set_text_fmt(s_count_label, "%d / %d files", s_sent, BURST_TOTAL);

  if (s_sent >= BURST_TOTAL) {
    lv_label_set_text(s_status_label, STATUS_DONE);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(SIG_GREEN), 0);
    lv_label_set_text_fmt(s_count_label, "%d signals sent", s_sent);
    if (s_footer)
      ui_chrome_footer_set_text(s_footer, HINT_DONE);
    if (s_waves)
      lv_obj_add_flag(s_waves, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(timer);
    s_burst_timer = NULL;
  }
}

static void nav_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(timer);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool is_back = back_button_is_down();
  if (is_back && !s_btn_back_last) {
    if (s_burst_timer != NULL) {
      lv_timer_delete(s_burst_timer);
      s_burst_timer = NULL;
    }
    ESP_LOGI(TAG, "mock burst cancelled");
    ui_switch_screen(SCREEN_IR_MENU);
  }
  s_btn_back_last = is_back;
}
