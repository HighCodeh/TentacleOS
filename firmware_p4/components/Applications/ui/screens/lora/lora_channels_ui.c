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

#include "lora_channels_ui.h"

#include "lvgl.h"
#include "st7789.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define HDR_TITLE   "CHANNELS"
#define HDR_ICON    NULL
#define FOOTER_HINT "UP/DOWN   OK EDIT   BACK"

#define BODY_TOP UI_CHROME_HEADER_H
#define BODY_H   (LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H)

#define GRID_PAD 7
#define GRID_GAP 6

#define TILE_W   110
#define TILE_H   80
#define TILE_PAD 6
#define TILE_GAP 2
#define TILE_RAD 9

#define DOT_SZ  6
#define KEY_SZ  8
#define KEY_RAD 2

#define BAR_CNT   8
#define BAR_W     3
#define BAR_GAP   2
#define BAR_MAX_H 14

#define GLOW_W   12
#define GLOW_SPR -2

#define TILE_CNT   8
#define FILLED_CNT 5

#define COL_ACC2 0xB89AFF
#define COL_CYAN 0x37E0A8
#define COL_DIM  0x8A8594
#define COL_SLOT 0x4A4556

static const char *CH_NAME[FILLED_CNT] = {
    "LongFast",
    "GhostNet",
    "admin",
    "telemetry",
    "relay-ops",
};

static const char *CH_STUB[FILLED_CNT] = {
    "2B7E\xE2\x80\xA2\xE2\x80\xA2"
    "1C",
    "9F04\xE2\x80\xA2\xE2\x80\xA2"
    "7A",
    "C1D9\xE2\x80\xA2\xE2\x80\xA2"
    "02",
    "5A66\xE2\x80\xA2\xE2\x80\xA2"
    "E1",
    "08BC\xE2\x80\xA2\xE2\x80\xA2"
    "44",
};

static const uint8_t CH_BARS[FILLED_CNT][BAR_CNT] = {
    {60, 90, 40, 75, 55, 85, 50, 70},
    {70, 45, 85, 55, 65, 40, 80, 50},
    {55, 80, 45, 70, 90, 50, 65, 40},
    {65, 50, 85, 45, 75, 60, 40, 80},
    {75, 55, 40, 85, 50, 70, 60, 45},
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_tiles[TILE_CNT];

static int s_sel = 0;

static lv_obj_t *bare_box(lv_obj_t *parent, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

static void build_filled_tile(lv_obj_t *tile, int i) {
  lv_color_t sig = (i == 0) ? current_theme.border_accent : lv_color_hex(COL_CYAN);
  lv_color_t dot = (i == 0) ? lv_color_hex(COL_ACC2) : lv_color_hex(COL_CYAN);

  lv_obj_t *head = bare_box(tile, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(head, 5, 0);

  lv_obj_t *idx = lv_label_create(head);
  lv_label_set_text_fmt(idx, "%d", i);
  lv_obj_set_style_text_font(idx, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(
      idx, (i == 0) ? current_theme.border_accent : lv_color_hex(COL_DIM), 0);

  lv_obj_t *d = lv_obj_create(head);
  lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(d, DOT_SZ, DOT_SZ);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(d, 0, 0);
  lv_obj_set_style_bg_color(d, dot, 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  if (i == 0) {
    lv_obj_set_style_shadow_color(d, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(d, 5, 0);
    lv_obj_set_style_shadow_opa(d, LV_OPA_COVER, 0);
  }

  lv_obj_t *spacer = bare_box(head, 1, 1);
  lv_obj_set_flex_grow(spacer, 1);

  lv_obj_t *key = lv_obj_create(head);
  lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(key, KEY_SZ, KEY_SZ);
  lv_obj_set_style_radius(key, KEY_RAD, 0);
  lv_obj_set_style_border_width(key, 0, 0);
  lv_obj_set_style_bg_color(key, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(key, (i == 0) ? LV_OPA_COVER : LV_OPA_40, 0);

  lv_obj_t *name = lv_label_create(tile);
  lv_label_set_text(name, CH_NAME[i]);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);

  lv_obj_t *stub = lv_label_create(tile);
  lv_label_set_text(stub, CH_STUB[i]);
  lv_obj_set_style_text_font(stub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(stub, lv_color_hex(COL_DIM), 0);

  lv_obj_t *bars = bare_box(tile, lv_pct(100), BAR_MAX_H);
  lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(bars, BAR_GAP, 0);
  for (int b = 0; b < BAR_CNT; b++) {
    int h = CH_BARS[i][b] * BAR_MAX_H / 100;
    if (h < 3)
      h = 3;
    lv_obj_t *bar = lv_obj_create(bars);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, BAR_W, h);
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, sig, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  }
}

static void build_empty_tile(lv_obj_t *tile, int i) {
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *key = lv_obj_create(tile);
  lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(key, KEY_SZ + 4, KEY_SZ + 4);
  lv_obj_set_style_radius(key, KEY_RAD, 0);
  lv_obj_set_style_border_width(key, 1, 0);
  lv_obj_set_style_border_color(key, lv_color_hex(COL_SLOT), 0);
  lv_obj_set_style_bg_opa(key, LV_OPA_TRANSP, 0);

  lv_obj_t *lbl = lv_label_create(tile);
  lv_label_set_text_fmt(lbl, "slot %d free", i);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_SLOT), 0);
}

static lv_obj_t *make_tile(lv_obj_t *grid, int i) {
  bool filled = (i < FILLED_CNT);

  lv_obj_t *tile = lv_obj_create(grid);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(tile, TILE_W, TILE_H);
  lv_obj_set_style_radius(tile, TILE_RAD, 0);
  lv_obj_set_style_pad_all(tile, TILE_PAD, 0);
  lv_obj_set_style_pad_row(tile, TILE_GAP, 0);
  lv_obj_set_style_border_width(tile, 1, 0);
  lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  if (filled) {
    lv_obj_set_style_bg_color(tile, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    build_filled_tile(tile, i);
  } else {
    lv_obj_set_style_bg_color(tile, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    build_empty_tile(tile, i);
  }
  return tile;
}

static void refresh_selection(void) {
  for (int i = 0; i < TILE_CNT; i++) {
    if (s_tiles[i] == NULL)
      continue;
    bool sel = (i == s_sel);
    lv_color_t base = (i == 0) ? current_theme.border_accent : current_theme.border_inactive;
    lv_obj_set_style_border_color(s_tiles[i], sel ? current_theme.border_accent : base, 0);
    lv_obj_set_style_border_width(s_tiles[i], sel ? 2 : 1, 0);
    lv_obj_set_style_shadow_color(s_tiles[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(s_tiles[i], sel ? GLOW_W : 0, 0);
    lv_obj_set_style_shadow_opa(s_tiles[i], sel ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_spread(s_tiles[i], sel ? GLOW_SPR : 0, 0);
  }
}

static void lora_channels_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_LORA_CHAT);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % TILE_CNT;
        refresh_selection();
        lv_obj_scroll_to_view(s_tiles[s_sel], LV_ANIM_ON);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel - 1 + TILE_CNT) % TILE_CNT;
        refresh_selection();
        lv_obj_scroll_to_view(s_tiles[s_sel], LV_ANIM_ON);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press)
        ui_feedback(UI_FB_SELECT);
      break;
    default:
      break;
  }
}

void ui_lora_channels_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  lv_obj_t *grid = lv_obj_create(s_screen);
  lv_obj_set_size(grid, LCD_H_RES, BODY_H);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, BODY_TOP);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, GRID_PAD, 0);
  lv_obj_set_style_pad_row(grid, GRID_GAP, 0);
  lv_obj_set_style_pad_column(grid, GRID_GAP, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(grid, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(grid, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(grid, 2, LV_PART_SCROLLBAR);

  for (int i = 0; i < TILE_CNT; i++)
    s_tiles[i] = make_tile(grid, i);
  refresh_selection();

  ui_chrome_footer(s_screen, FOOTER_HINT);

  ui_input_set_screen_handler(lora_channels_input, NULL);

  ui_screen_load(s_screen);
}
