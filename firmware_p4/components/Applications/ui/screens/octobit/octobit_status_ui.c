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

#include "octobit_status_ui.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "nvs.h"

#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "bq25896.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define MX        8
#define CONTENT_W (LCD_H_RES - 2 * MX)
#define CARD_Y    46
#define CARD_H    64
#define AVA       50
#define SELHD_Y   116
#define SELWRAP_Y 134
#define SELWRAP_H 156
#define ROW_H     34
#define ROW_GAP   5
#define ROW_STEP  (ROW_H + ROW_GAP)
#define VIS       4

#define AVATAR_ASSET "/assets/img/octobit_portrait.bin"

#define OCTO_LEVEL  7
#define OCTO_XP     1240
#define OCTO_XP_MAX 2000

#define COL_RAISE 0x170A28
#define COL_DIM   0x8A8594

#define REFRESH_MS   1000

enum {
  ST_UPTIME = 0,
  ST_SCANS,
  ST_CARDS,
  ST_SIGNALS,
  ST_BOOTS,
  ST_BATTERY,
  ST_STORAGE,
  ST_HEAP,
  ST_COUNT,
};

typedef struct {
  const char *sym;
  const char *name;
} stat_def_t;

static const stat_def_t STAT_DEFS[ST_COUNT] = {
    {LV_SYMBOL_REFRESH, "Uptime"},
    {LV_SYMBOL_EYE_OPEN, "Scans"},
    {LV_SYMBOL_SD_CARD, "Cards saved"},
    {LV_SYMBOL_WIFI, "Signals"},
    {LV_SYMBOL_POWER, "Boots"},
    {LV_SYMBOL_CHARGE, "Battery"},
    {LV_SYMBOL_DRIVE, "Storage"},
    {LV_SYMBOL_BARS, "Heap free"},
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_sellist = NULL;
static lv_obj_t *s_counter = NULL;
static lv_obj_t *s_row[ST_COUNT];
static lv_obj_t *s_icon_lbl[ST_COUNT];
static lv_obj_t *s_val_lbl[ST_COUNT];
static lv_timer_t *s_timer = NULL;

static int s_sel = 0;

static uint32_t s_boots = 0;
static bool s_boots_read = false;

static void read_boots_once(void) {
  if (s_boots_read)
    return;
  s_boots_read = true;
  nvs_handle_t h;
  if (nvs_open("octobit", NVS_READWRITE, &h) == ESP_OK) {
    uint32_t v = 0;
    nvs_get_u32(h, "boots", &v);
    v++;
    nvs_set_u32(h, "boots", v);
    nvs_commit(h);
    nvs_close(h);
    s_boots = v;
  } else {
    s_boots = 1;
  }
}

static void set_val(int i, const char *text) {
  if (s_val_lbl[i])
    lv_label_set_text(s_val_lbl[i], text);
}

static void refresh_values(void) {
  char b[64];

  uint32_t s = (uint32_t)(esp_timer_get_time() / 1000000LL);
  if (s >= 86400)
    snprintf(b,
             sizeof(b),
             "%lud %02luh",
             (unsigned long)(s / 86400),
             (unsigned long)((s % 86400) / 3600));
  else if (s >= 3600)
    snprintf(
        b, sizeof(b), "%luh %02lum", (unsigned long)(s / 3600), (unsigned long)((s % 3600) / 60));
  else
    snprintf(b, sizeof(b), "%lum %02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
  set_val(ST_UPTIME, b);

  bq25896_telem_t t;
  if (bq25896_read_telemetry(&t) == ESP_OK)
    snprintf(b, sizeof(b), "%d%%", t.soc);
  else
    snprintf(b, sizeof(b), "--");
  set_val(ST_BATTERY, b);

  snprintf(
      b, sizeof(b), "%lu KB", (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024));
  set_val(ST_HEAP, b);

  size_t total = 0, used = 0;
  if (esp_littlefs_info("assets", &total, &used) == ESP_OK) {
    uint32_t u10 = (uint32_t)((uint64_t)used * 10 / (1024 * 1024));
    uint32_t t10 = (uint32_t)((uint64_t)total * 10 / (1024 * 1024));
    snprintf(b,
             sizeof(b),
             "%lu.%lu/%lu.%lu MB",
             (unsigned long)(u10 / 10),
             (unsigned long)(u10 % 10),
             (unsigned long)(t10 / 10),
             (unsigned long)(t10 % 10));
  } else {
    snprintf(b, sizeof(b), "-- MB");
  }
  set_val(ST_STORAGE, b);
}

static void refresh_selection(void) {
  const lv_color_t accent = current_theme.border_accent;
  const lv_color_t dim = lv_color_hex(COL_DIM);
  for (int i = 0; i < ST_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(s_row[i], sel ? accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(s_row[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        s_row[i], sel ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
    lv_obj_set_style_shadow_width(s_row[i], sel ? 14 : 0, 0);
    lv_obj_set_style_shadow_color(s_row[i], accent, 0);
    lv_obj_set_style_shadow_spread(s_row[i], sel ? -3 : 0, 0);
    lv_obj_set_style_text_color(s_icon_lbl[i], sel ? accent : dim, 0);
    lv_obj_set_style_text_color(s_val_lbl[i], sel ? accent : dim, 0);
  }
  int top = s_sel - 1;
  if (top < 0)
    top = 0;
  if (top > ST_COUNT - VIS)
    top = ST_COUNT - VIS;
  lv_obj_set_style_translate_y(s_sellist, -top * ROW_STEP, 0);

  if (s_counter)
    lv_label_set_text_fmt(s_counter, "%d/%d", s_sel + 1, ST_COUNT);
}

static lv_obj_t *make_row(lv_obj_t *parent, int i) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, lv_pct(100), ROW_H);
  lv_obj_set_style_radius(row, 9, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_left(row, 10, 0);
  lv_obj_set_style_pad_right(row, 10, 0);
  lv_obj_set_style_pad_top(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ic = lv_label_create(row);
  lv_label_set_text(ic, STAT_DEFS[i].sym);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_16, 0);
  lv_obj_set_width(ic, 20);
  lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, STAT_DEFS[i].name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_set_style_pad_left(name, 9, 0);
  lv_obj_set_flex_grow(name, 1);

  lv_obj_t *val = lv_label_create(row);
  lv_label_set_text(val, "--");
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);

  s_icon_lbl[i] = ic;
  s_val_lbl[i] = val;
  return row;
}

static void build_top_card(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, CARD_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CARD_Y);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_interface, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_50, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_add_flag(card, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  lv_obj_t *ava = lv_obj_create(card);
  lv_obj_remove_flag(ava, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(ava, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(ava, AVA, AVA);
  lv_obj_align(ava, LV_ALIGN_LEFT_MID, 8, 0);
  lv_obj_set_style_radius(ava, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ava, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(ava, current_theme.screen_base, 0);
  lv_obj_set_style_bg_grad_dir(ava, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(ava, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ava, 2, 0);
  lv_obj_set_style_border_color(ava, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(ava, 14, 0);
  lv_obj_set_style_shadow_color(ava, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_spread(ava, -4, 0);
  lv_obj_set_style_pad_all(ava, 0, 0);
  lv_obj_set_style_clip_corner(ava, true, 0);

  lv_image_dsc_t *portrait = assets_get(AVATAR_ASSET);
  if (portrait != NULL) {
    lv_obj_t *img = lv_image_create(ava);
    lv_image_set_src(img, portrait);
    lv_obj_set_size(img, AVA - 8, AVA - 8);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_center(img);
  }

  lv_obj_t *badge = lv_label_create(card);
  lv_label_set_text_fmt(badge, "Lv %d", OCTO_LEVEL);
  lv_obj_set_style_text_font(badge, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(badge, current_theme.screen_base, 0);
  lv_obj_set_style_bg_color(badge, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(badge, 7, 0);
  lv_obj_set_style_pad_hor(badge, 5, 0);
  lv_obj_set_style_pad_ver(badge, 1, 0);
  lv_obj_update_layout(s_screen);
  lv_obj_align_to(badge, ava, LV_ALIGN_BOTTOM_RIGHT, 6, 5);

  const int col_x = 8 + AVA + 12;
  const int col_w = CONTENT_W - col_x - 12;

  lv_obj_t *xp_tag = lv_label_create(card);
  lv_label_set_text(xp_tag, "XP");
  lv_obj_set_style_text_font(xp_tag, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(xp_tag, lv_color_hex(COL_DIM), 0);
  lv_obj_align(xp_tag, LV_ALIGN_LEFT_MID, col_x, -12);

  lv_obj_t *xp_val = lv_label_create(card);
  lv_label_set_text_fmt(xp_val, "%d / %d", OCTO_XP, OCTO_XP_MAX);
  lv_obj_set_style_text_font(xp_val, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(xp_val, lv_color_hex(COL_DIM), 0);
  lv_obj_set_width(xp_val, col_w);
  lv_obj_set_style_text_align(xp_val, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(xp_val, LV_ALIGN_LEFT_MID, col_x, -12);

  lv_obj_t *bar = lv_obj_create(card);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(bar, col_w, 8);
  lv_obj_align(bar, LV_ALIGN_LEFT_MID, col_x, 6);
  lv_obj_set_style_radius(bar, 4, 0);
  lv_obj_set_style_bg_color(bar, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, current_theme.border_inactive, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_set_style_clip_corner(bar, true, 0);

  lv_obj_t *fill = lv_obj_create(bar);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
  int pct = OCTO_XP_MAX > 0 ? (OCTO_XP * 100 / OCTO_XP_MAX) : 0;
  lv_obj_set_size(fill, lv_pct(pct), lv_pct(100));
  lv_obj_align(fill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(fill, 4, 0);
  lv_obj_set_style_border_width(fill, 0, 0);
  lv_obj_set_style_bg_color(fill, current_theme.border_interface, 0);
  lv_obj_set_style_bg_grad_color(fill, current_theme.border_accent, 0);
  lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
}

static void build_selector(void) {
  lv_obj_t *tag = lv_label_create(s_screen);
  lv_label_set_text(tag, "STATISTICS");
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tag, lv_color_hex(COL_DIM), 0);
  lv_obj_align(tag, LV_ALIGN_TOP_LEFT, MX + 4, SELHD_Y);

  s_counter = lv_label_create(s_screen);
  lv_label_set_text(s_counter, "1/8");
  lv_obj_set_style_text_font(s_counter, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_counter, lv_color_hex(COL_DIM), 0);
  lv_obj_align(s_counter, LV_ALIGN_TOP_RIGHT, -(MX + 4), SELHD_Y);

  lv_obj_t *wrap = lv_obj_create(s_screen);
  lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(wrap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(wrap, CONTENT_W, SELWRAP_H);
  lv_obj_align(wrap, LV_ALIGN_TOP_MID, 0, SELWRAP_Y);
  lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wrap, 0, 0);
  lv_obj_set_style_pad_all(wrap, 0, 0);
  lv_obj_set_style_clip_corner(wrap, true, 0);

  s_sellist = lv_obj_create(wrap);
  lv_obj_remove_flag(s_sellist, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_sellist, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(s_sellist, lv_pct(100));
  lv_obj_set_height(s_sellist, LV_SIZE_CONTENT);
  lv_obj_align(s_sellist, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(s_sellist, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_sellist, 0, 0);
  lv_obj_set_style_pad_all(s_sellist, 0, 0);
  lv_obj_set_style_pad_row(s_sellist, ROW_GAP, 0);
  lv_obj_set_flex_flow(s_sellist, LV_FLEX_FLOW_COLUMN);

  for (int i = 0; i < ST_COUNT; i++)
    s_row[i] = make_row(s_sellist, i);
}

static void octobit_status_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_RIGHT:
      if (press)
        ui_switch_screen(SCREEN_HOME);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % ST_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel - 1 + ST_COUNT) % ST_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    default:
      break;
  }
}

static void refresh_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  refresh_values();
}

void ui_octobit_status_open(void) {
  bq25896_init();

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "Octobit", NULL);

  build_top_card();
  build_selector();

  ui_chrome_footer(s_screen, "UP/DOWN  navigate     BACK  home");

  set_val(ST_SCANS, "1,204");
  set_val(ST_CARDS, "37");
  set_val(ST_SIGNALS, "82");
  read_boots_once();
  {
    char b[16];
    snprintf(b, sizeof(b), "#%lu", (unsigned long)s_boots);
    set_val(ST_BOOTS, b);
  }
  refresh_values();
  refresh_selection();

  if (s_timer == NULL)
    s_timer = lv_timer_create(refresh_cb, REFRESH_MS, NULL);

  ui_input_set_screen_handler(octobit_status_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
