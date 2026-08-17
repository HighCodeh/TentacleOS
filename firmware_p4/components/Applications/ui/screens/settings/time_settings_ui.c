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

#include "time_settings_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

#include "notify_ui.h"
#include "sys_time.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TIME_ICON "/assets/icons/timer.bin"
#define TIME_FONT "A:assets/fonts/Inter.bin"

#define YEAR_MIN 2020
#define YEAR_MAX 2099

#define FIELD_YEAR  0
#define FIELD_MONTH 1
#define FIELD_DAY   2
#define FIELD_HOUR  3
#define FIELD_MIN   4
#define FIELD_COUNT 5

#define COL_DIM 0x6D7A75
#define CARD_W  208

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_fields[FIELD_COUNT];
static lv_obj_t *s_wkday = NULL;
static lv_font_t *s_big_font = NULL;
static struct tm s_tm;
static int s_focus = 0;

static void str_upper(char *s) {
  for (; *s != '\0'; s++)
    if (*s >= 'a' && *s <= 'z')
      *s = (char)(*s - 'a' + 'A');
}

static int days_in_month(int year, int mon0) {
  static const int base[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mon0 == 1) {
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return leap ? 29 : 28;
  }
  return base[mon0];
}

static void clamp_day(void) {
  int dim = days_in_month(s_tm.tm_year + 1900, s_tm.tm_mon);
  if (s_tm.tm_mday > dim)
    s_tm.tm_mday = dim;
  if (s_tm.tm_mday < 1)
    s_tm.tm_mday = 1;
}

static void field_adjust(int delta) {
  switch (s_focus) {
    case FIELD_YEAR: {
      int y = s_tm.tm_year + 1900 + delta;
      if (y < YEAR_MIN)
        y = YEAR_MAX;
      if (y > YEAR_MAX)
        y = YEAR_MIN;
      s_tm.tm_year = y - 1900;
      clamp_day();
      break;
    }
    case FIELD_MONTH:
      s_tm.tm_mon += delta;
      if (s_tm.tm_mon < 0)
        s_tm.tm_mon = 11;
      if (s_tm.tm_mon > 11)
        s_tm.tm_mon = 0;
      clamp_day();
      break;
    case FIELD_DAY: {
      int dim = days_in_month(s_tm.tm_year + 1900, s_tm.tm_mon);
      s_tm.tm_mday += delta;
      if (s_tm.tm_mday < 1)
        s_tm.tm_mday = dim;
      if (s_tm.tm_mday > dim)
        s_tm.tm_mday = 1;
      break;
    }
    case FIELD_HOUR:
      s_tm.tm_hour += delta;
      if (s_tm.tm_hour < 0)
        s_tm.tm_hour = 23;
      if (s_tm.tm_hour > 23)
        s_tm.tm_hour = 0;
      break;
    case FIELD_MIN:
      s_tm.tm_min += delta;
      if (s_tm.tm_min < 0)
        s_tm.tm_min = 59;
      if (s_tm.tm_min > 59)
        s_tm.tm_min = 0;
      break;
    default:
      break;
  }
}

static void update_display(void) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d", s_tm.tm_year + 1900);
  lv_label_set_text(s_fields[FIELD_YEAR], buf);
  snprintf(buf, sizeof(buf), "%02d", s_tm.tm_mon + 1);
  lv_label_set_text(s_fields[FIELD_MONTH], buf);
  snprintf(buf, sizeof(buf), "%02d", s_tm.tm_mday);
  lv_label_set_text(s_fields[FIELD_DAY], buf);
  snprintf(buf, sizeof(buf), "%02d", s_tm.tm_hour);
  lv_label_set_text(s_fields[FIELD_HOUR], buf);
  snprintf(buf, sizeof(buf), "%02d", s_tm.tm_min);
  lv_label_set_text(s_fields[FIELD_MIN], buf);

  for (int i = 0; i < FIELD_COUNT; i++) {
    bool sel = (i == s_focus);
    lv_obj_set_style_text_color(
        s_fields[i], sel ? current_theme.border_accent : current_theme.text_main, 0);
    lv_obj_set_style_border_width(s_fields[i], sel ? 2 : 0, 0);
  }

  struct tm t = s_tm;
  t.tm_sec = 0;
  t.tm_isdst = 0;
  mktime(&t);
  char wk[16];
  if (strftime(wk, sizeof(wk), "%A", &t) == 0)
    wk[0] = '\0';
  str_upper(wk);
  lv_label_set_text(s_wkday, wk);
}

static void apply_and_save(void) {
  struct tm t = s_tm;
  t.tm_sec = 0;
  t.tm_isdst = 0;
  time_t epoch = mktime(&t);
  if (epoch == (time_t)-1) {
    notify(NOTIFY_WARNING, "Invalid date");
    return;
  }
  if (sys_time_set(epoch, SYS_TIME_SOURCE_MANUAL) == ESP_OK)
    notify(NOTIFY_SAVED, "Time saved");
  else
    notify(NOTIFY_WARNING, "Could not set time");
}

static lv_obj_t *make_field(lv_obj_t *parent, const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, current_theme.text_main, 0);
  lv_obj_set_style_border_color(l, current_theme.border_accent, 0);
  lv_obj_set_style_border_side(l, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(l, 0, 0);
  lv_obj_set_style_pad_left(l, 3, 0);
  lv_obj_set_style_pad_right(l, 3, 0);
  lv_obj_set_style_pad_bottom(l, 2, 0);
  return l;
}

static void make_sep(lv_obj_t *parent, const char *txt, const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(COL_DIM), 0);
}

static lv_obj_t *make_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  return row;
}

static void time_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_LEFT:
      if (nav) {
        s_focus = (s_focus == 0) ? FIELD_COUNT - 1 : s_focus - 1;
        ui_feedback(UI_FB_NAV);
        update_display();
      }
      break;
    case INPUT_BTN_RIGHT:
      if (nav) {
        s_focus = (s_focus + 1) % FIELD_COUNT;
        ui_feedback(UI_FB_NAV);
        update_display();
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        field_adjust(+1);
        ui_feedback(UI_FB_NAV);
        update_display();
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        field_adjust(-1);
        ui_feedback(UI_FB_NAV);
        update_display();
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        apply_and_save();
        ui_feedback(UI_FB_SELECT);
      }
      break;
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS);
      break;
    default:
      break;
  }
}

void ui_time_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  time_t now = sys_time_now();
  localtime_r(&now, &s_tm);
  s_focus = 0;

  if (s_big_font == NULL)
    s_big_font = lv_binfont_create(TIME_FONT);
  const lv_font_t *bigf = s_big_font != NULL ? s_big_font : &lv_font_montserrat_16;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "DATE & TIME", TIME_ICON);
  ui_chrome_footer(s_screen,
                   LV_SYMBOL_OK " save   " LV_SYMBOL_UP LV_SYMBOL_DOWN
                                " edit   " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " field");

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_set_size(card, CARD_W, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_set_style_pad_row(card, 10, 0);
  lv_obj_set_style_shadow_width(card, 22, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *drow = make_row(card);
  s_fields[FIELD_YEAR] = make_field(drow, &lv_font_montserrat_16);
  make_sep(drow, "-", &lv_font_montserrat_16);
  s_fields[FIELD_MONTH] = make_field(drow, &lv_font_montserrat_16);
  make_sep(drow, "-", &lv_font_montserrat_16);
  s_fields[FIELD_DAY] = make_field(drow, &lv_font_montserrat_16);

  s_wkday = lv_label_create(card);
  lv_obj_set_style_text_font(s_wkday, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_wkday, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_text_letter_space(s_wkday, 2, 0);

  lv_obj_t *trow = make_row(card);
  s_fields[FIELD_HOUR] = make_field(trow, bigf);
  make_sep(trow, ":", bigf);
  s_fields[FIELD_MIN] = make_field(trow, bigf);

  update_display();

  ui_input_set_screen_handler(time_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}
