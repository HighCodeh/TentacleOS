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

#include "snake_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"
#include "nvs.h"

#include "buttons_gpio.h"
#include "game_fx.h"
#include "st7789.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TICK_MS          33
#define STEP_TICKS       4
#define CELL             16
#define TOPBAR           26
#define MAX_SEG          96
#define START_LEN        4
#define FOOD_PLACE_TRIES 200

#define COL_BG   0x0A0014
#define COL_BODY 0x9C27B0
#define COL_HEAD 0xE040FB
#define COL_FOOD 0x00E676

enum { ST_PLAY, ST_DEAD };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_seg[MAX_SEG];
static lv_obj_t *s_food_obj = NULL;
static lv_obj_t *s_score_lbl = NULL;
static lv_obj_t *s_msg_panel = NULL, *s_msg_lbl = NULL;
static lv_timer_t *s_timer = NULL;

static int s_state = ST_PLAY;
static int s_cols, s_rows, s_origin_x, s_origin_y;
static int s_bx[MAX_SEG], s_by[MAX_SEG];
static int s_len;
static int s_dx, s_dy;
static int s_ndx, s_ndy;
static int s_food_x, s_food_y;
static int s_score;
static uint32_t s_best;
static int s_tick_acc;

static bool s_up_last, s_down_last, s_left_last, s_right_last, s_ok_last, s_back_last;

static uint32_t load_best(void) {
  nvs_handle_t h;
  uint32_t v = 0;
  if (nvs_open("snake", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u32(h, "best", &v);
    nvs_close(h);
  }
  return v;
}
static void save_best(uint32_t v) {
  nvs_handle_t h;
  if (nvs_open("snake", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u32(h, "best", v);
    nvs_commit(h);
    nvs_close(h);
  }
}

static void cell_pos(lv_obj_t *o, int cx, int cy) {
  lv_obj_set_pos(o, s_origin_x + cx * CELL, s_origin_y + cy * CELL);
}

static bool on_snake(int cx, int cy) {
  for (int i = 0; i < s_len; i++)
    if (s_bx[i] == cx && s_by[i] == cy)
      return true;
  return false;
}

static void place_food(void) {
  for (int tries = 0; tries < FOOD_PLACE_TRIES; tries++) {
    int cx = esp_random() % s_cols;
    int cy = esp_random() % s_rows;
    if (!on_snake(cx, cy)) {
      s_food_x = cx;
      s_food_y = cy;
      break;
    }
  }
  cell_pos(s_food_obj, s_food_x, s_food_y);
}

static void render_body(void) {
  for (int i = 0; i < MAX_SEG; i++) {
    if (i < s_len) {
      lv_obj_remove_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
      cell_pos(s_seg[i], s_bx[i], s_by[i]);
      lv_obj_set_style_bg_color(s_seg[i], lv_color_hex(i == 0 ? COL_HEAD : COL_BODY), 0);
    } else {
      lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void set_score_text(void) {
  lv_label_set_text_fmt(s_score_lbl, "Score %d", s_score);
}

static void reset_game(void) {
  s_state = ST_PLAY;
  s_score = 0;
  s_len = START_LEN;
  int sx = s_cols / 2, sy = s_rows / 2;
  for (int i = 0; i < s_len; i++) {
    s_bx[i] = sx - i;
    s_by[i] = sy;
  }
  s_dx = 1;
  s_dy = 0;
  s_ndx = 1;
  s_ndy = 0;
  s_tick_acc = 0;
  place_food();
  render_body();
  set_score_text();
  lv_obj_add_flag(s_msg_panel, LV_OBJ_FLAG_HIDDEN);
}

static void die(void) {
  s_state = ST_DEAD;
  game_fx(GFX_CRASH);
  if ((uint32_t)s_score > s_best) {
    s_best = (uint32_t)s_score;
    save_best(s_best);
  }
  lv_label_set_text_fmt(s_msg_lbl,
                        "GAME OVER\n\nScore  %d\nBest  %u\n\nOK = retry\nBACK = exit",
                        s_score,
                        (unsigned)s_best);
  lv_obj_remove_flag(s_msg_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_msg_panel);
}

static void step(void) {
  if (!(s_ndx == -s_dx && s_ndy == -s_dy)) {
    s_dx = s_ndx;
    s_dy = s_ndy;
  }

  int nhx = s_bx[0] + s_dx;
  int nhy = s_by[0] + s_dy;

  if (nhx < 0 || nhx >= s_cols || nhy < 0 || nhy >= s_rows) {
    die();
    return;
  }

  bool grow = (nhx == s_food_x && nhy == s_food_y);

  int last = s_len - 1;
  for (int i = 0; i < s_len; i++) {
    if (i == last && !grow)
      continue;
    if (s_bx[i] == nhx && s_by[i] == nhy) {
      die();
      return;
    }
  }

  if (grow && s_len < MAX_SEG)
    s_len++;
  for (int i = s_len - 1; i > 0; i--) {
    s_bx[i] = s_bx[i - 1];
    s_by[i] = s_by[i - 1];
  }
  s_bx[0] = nhx;
  s_by[0] = nhy;

  if (grow) {
    s_score++;
    set_score_text();
    game_fx(GFX_EAT);
    place_food();
  }
  render_body();
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  bool up = ui_btn_up(), down = ui_btn_down(), left = ui_btn_left();
  bool right = ui_btn_right(), ok = ok_button_is_down(), back = back_button_is_down();

  if (!ui_input_is_locked()) {
    if (back && !s_back_last) {
      s_back_last = back;
      ui_switch_screen(SCREEN_GAMES_MENU);
      return;
    }

    if (s_state == ST_PLAY) {
      if (up && !s_up_last) {
        s_ndx = 0;
        s_ndy = -1;
      } else if (down && !s_down_last) {
        s_ndx = 0;
        s_ndy = 1;
      } else if (left && !s_left_last) {
        s_ndx = -1;
        s_ndy = 0;
      } else if (right && !s_right_last) {
        s_ndx = 1;
        s_ndy = 0;
      }
    } else if (ok && !s_ok_last) {
      reset_game();
    }
  }

  if (s_state == ST_PLAY && ++s_tick_acc >= STEP_TICKS) {
    s_tick_acc = 0;
    step();
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_snake_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  s_best = load_best();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);

  int w = LCD_H_RES, h = LCD_V_RES;
  s_cols = w / CELL;
  s_rows = (h - TOPBAR) / CELL;
  s_origin_x = (w - s_cols * CELL) / 2;
  s_origin_y = TOPBAR + (h - TOPBAR - s_rows * CELL) / 2;

  s_food_obj = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_food_obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_food_obj, CELL - 2, CELL - 2);
  lv_obj_set_style_radius(s_food_obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_food_obj, 0, 0);
  lv_obj_set_style_bg_opa(s_food_obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_food_obj, lv_color_hex(COL_FOOD), 0);

  for (int i = 0; i < MAX_SEG; i++) {
    s_seg[i] = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_seg[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_seg[i], CELL - 1, CELL - 1);
    lv_obj_set_style_radius(s_seg[i], 3, 0);
    lv_obj_set_style_border_width(s_seg[i], 0, 0);
    lv_obj_set_style_bg_opa(s_seg[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_seg[i], lv_color_hex(COL_BODY), 0);
    lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
  }

  s_score_lbl = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 5);

  s_msg_panel = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_msg_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_msg_panel, w - 60, LV_SIZE_CONTENT);
  lv_obj_align(s_msg_panel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(s_msg_panel, 14, 0);
  lv_obj_set_style_bg_color(s_msg_panel, lv_color_hex(0x1A0426), 0);
  lv_obj_set_style_bg_opa(s_msg_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_msg_panel, 2, 0);
  lv_obj_set_style_border_color(s_msg_panel, ui_theme_get_accent(), 0);
  lv_obj_set_style_pad_all(s_msg_panel, 14, 0);
  s_msg_lbl = lv_label_create(s_msg_panel);
  lv_obj_set_style_text_color(s_msg_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_msg_lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(s_msg_lbl);

  reset_game();

  if (s_timer == NULL)
    s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);

  ui_screen_load(s_screen);
}
