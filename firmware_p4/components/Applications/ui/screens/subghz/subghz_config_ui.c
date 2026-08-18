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

#include "subghz_config_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HDR_TITLE "RADIO CONFIG"
#define HDR_ICON  "/assets/icons/tune.bin"
#define FOOTER    "UP/DN pick   L/R adjust   OK set"

#define MX        8
#define CONTENT_W (LCD_H_RES - 2 * MX)

#define CARD1_Y     50
#define CARD1_H     84
#define CARD_RADIUS 12
#define CARD_GLOW_W 12

#define FREQ_CAP_Y 8
#define FREQ_VAL_Y 24
#define FREQ_GAP   3

#define BAND_Y      50
#define BAND_W      200
#define BAND_H      7
#define BAND_RADIUS 4
#define MARKER_PCT  21
#define MARKER_W    3
#define MARKER_H    13
#define MARKER_GLOW 8
#define SCALE_Y     62

#define GRID_Y   (CARD1_Y + CARD1_H + 8)
#define TILE_W   108
#define TILE_H   52
#define TILE_GAP 8
#define TILE_X_L MX
#define TILE_X_R (MX + TILE_W + TILE_GAP)
#define ROW2_Y   (GRID_Y + TILE_H + 8)

#define TILE_RADIUS 9
#define TILE_PAD_L  9
#define TILE_CAP_Y  7
#define TILE_VAL_Y  25
#define TILE_GLOW_W 12

#define COL_BAND_A 0x221F2E
#define COL_BAND_B 0x3A2F55

#define FREQ_NUM  "433.920"
#define FREQ_UNIT "MHz"

#define FIELD_COUNT 4
#define VAL_MAX     4

typedef struct {
  const char *name;
  const char *unit;
  const char *values[VAL_MAX];
  int count;
  int def;
} rf_field_t;

static const rf_field_t FIELDS[FIELD_COUNT] = {
    {"MODULATION", NULL, {"2FSK", "OOK", "GFSK", "4FSK"}, 4, 0},
    {"BANDWIDTH", "kHz", {"58", "135", "270", "812"}, 4, 1},
    {"DATA RATE", "kb/s", {"2.4", "4.8", "9.6", "38.4"}, 4, 1},
    {"PRESET", NULL, {"FM238", "FM476", "OOK270", "AM650"}, 4, 0},
};

static const int TILE_X[FIELD_COUNT] = {TILE_X_L, TILE_X_R, TILE_X_L, TILE_X_R};
static const int TILE_Y[FIELD_COUNT] = {GRID_Y, GRID_Y, ROW2_Y, ROW2_Y};

static const char *SCALE_LABELS[4] = {"300", "433", "700", "928"};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tile[FIELD_COUNT];
static lv_obj_t *s_tile_num[FIELD_COUNT];

static int s_sel = 0;
static int s_val_idx[FIELD_COUNT];

static void build_freq_card(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, CARD1_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CARD1_Y);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_shadow_width(card, CARD_GLOW_W, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);

  lv_obj_t *cap = lv_label_create(card);
  lv_label_set_text(cap, "TUNED FREQUENCY");
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, current_theme.text_secondary, 0);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, FREQ_CAP_Y);

  lv_obj_t *grp = lv_obj_create(card);
  lv_obj_remove_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grp, CONTENT_W, 24);
  lv_obj_align(grp, LV_ALIGN_TOP_MID, 0, FREQ_VAL_Y);
  lv_obj_set_style_bg_opa(grp, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grp, 0, 0);
  lv_obj_set_style_pad_all(grp, 0, 0);
  lv_obj_set_style_pad_column(grp, FREQ_GAP, 0);
  lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *num = lv_label_create(grp);
  lv_label_set_text(num, FREQ_NUM);
  lv_obj_set_style_text_font(num, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(num, current_theme.text_main, 0);

  lv_obj_t *unit = lv_label_create(grp);
  lv_label_set_text(unit, FREQ_UNIT);
  lv_obj_set_style_text_font(unit, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(unit, current_theme.border_accent, 0);

  lv_obj_t *band = lv_obj_create(card);
  lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(band, BAND_W, BAND_H);
  lv_obj_align(band, LV_ALIGN_TOP_MID, 0, BAND_Y);
  lv_obj_set_style_radius(band, BAND_RADIUS, 0);
  lv_obj_set_style_border_width(band, 0, 0);
  lv_obj_set_style_pad_all(band, 0, 0);
  lv_obj_set_style_bg_color(band, lv_color_hex(COL_BAND_A), 0);
  lv_obj_set_style_bg_grad_color(band, lv_color_hex(COL_BAND_B), 0);
  lv_obj_set_style_bg_grad_dir(band, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);

  lv_obj_t *marker = lv_obj_create(band);
  lv_obj_remove_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(marker, MARKER_W, MARKER_H);
  lv_obj_align(marker, LV_ALIGN_LEFT_MID, (BAND_W * MARKER_PCT) / 100, 0);
  lv_obj_set_style_radius(marker, 2, 0);
  lv_obj_set_style_border_width(marker, 0, 0);
  lv_obj_set_style_pad_all(marker, 0, 0);
  lv_obj_set_style_bg_color(marker, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(marker, MARKER_GLOW, 0);
  lv_obj_set_style_shadow_color(marker, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(marker, LV_OPA_COVER, 0);

  lv_obj_t *scale = lv_obj_create(card);
  lv_obj_remove_flag(scale, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(scale, BAND_W, 16);
  lv_obj_align(scale, LV_ALIGN_TOP_MID, 0, SCALE_Y);
  lv_obj_set_style_bg_opa(scale, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scale, 0, 0);
  lv_obj_set_style_pad_all(scale, 0, 0);
  lv_obj_set_flex_flow(scale, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      scale, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *lbl = lv_label_create(scale);
    lv_label_set_text(lbl, SCALE_LABELS[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, current_theme.text_secondary, 0);
  }
}

static lv_obj_t *build_tile(int i) {
  lv_obj_t *tile = lv_obj_create(s_screen);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(tile, TILE_W, TILE_H);
  lv_obj_set_pos(tile, TILE_X[i], TILE_Y[i]);
  lv_obj_set_style_radius(tile, TILE_RADIUS, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tile, 1, 0);

  lv_obj_t *cap = lv_label_create(tile);
  lv_label_set_text(cap, FIELDS[i].name);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, current_theme.text_secondary, 0);
  lv_obj_align(cap, LV_ALIGN_TOP_LEFT, TILE_PAD_L, TILE_CAP_Y);

  lv_obj_t *grp = lv_obj_create(tile);
  lv_obj_remove_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grp, TILE_W - 2 * TILE_PAD_L, 22);
  lv_obj_align(grp, LV_ALIGN_TOP_LEFT, TILE_PAD_L, TILE_VAL_Y);
  lv_obj_set_style_bg_opa(grp, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grp, 0, 0);
  lv_obj_set_style_pad_all(grp, 0, 0);
  lv_obj_set_style_pad_column(grp, 3, 0);
  lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *num = lv_label_create(grp);
  lv_label_set_text(num, FIELDS[i].values[s_val_idx[i]]);
  lv_obj_set_style_text_font(num, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(num, current_theme.text_main, 0);
  s_tile_num[i] = num;

  if (FIELDS[i].unit != NULL) {
    lv_obj_t *unit = lv_label_create(grp);
    lv_label_set_text(unit, FIELDS[i].unit);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(unit, current_theme.text_secondary, 0);
  }

  return tile;
}

static void refresh_selection(void) {
  for (int i = 0; i < FIELD_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_bg_color(
        s_tile[i], sel ? current_theme.bg_secondary : current_theme.bg_primary, 0);
    lv_obj_set_style_border_color(
        s_tile[i], sel ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_shadow_width(s_tile[i], sel ? TILE_GLOW_W : 0, 0);
    lv_obj_set_style_shadow_color(s_tile[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_opa(s_tile[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_spread(s_tile[i], sel ? -2 : 0, 0);
    lv_obj_set_style_text_color(
        s_tile_num[i], sel ? current_theme.border_accent : current_theme.text_main, 0);
  }
}

static void adjust_value(int dir) {
  int n = FIELDS[s_sel].count;
  s_val_idx[s_sel] = (s_val_idx[s_sel] + dir + n) % n;
  lv_label_set_text(s_tile_num[s_sel], FIELDS[s_sel].values[s_val_idx[s_sel]]);
}

static void subghz_config_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SUBGHZ_MENU);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % FIELD_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel - 1 + FIELD_COUNT) % FIELD_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_RIGHT:
      if (nav) {
        adjust_value(1);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_LEFT:
      if (nav) {
        adjust_value(-1);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        notify(NOTIFY_SAVED, "Radio config applied");
        ui_feedback(UI_FB_SELECT);
      }
      break;
    default:
      break;
  }
}

void ui_subghz_config_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;
  for (int i = 0; i < FIELD_COUNT; i++)
    s_val_idx[i] = FIELDS[i].def;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_freq_card();
  for (int i = 0; i < FIELD_COUNT; i++)
    s_tile[i] = build_tile(i);
  refresh_selection();

  ui_chrome_footer(s_screen, FOOTER);

  ui_input_set_screen_handler(subghz_config_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
