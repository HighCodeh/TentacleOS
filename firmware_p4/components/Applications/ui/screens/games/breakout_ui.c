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

#include "breakout_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"
#include "nvs.h"

#include "buttons_gpio.h"
#include "game_fx.h"
#include "st7789.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TICK_MS         33
#define TOPBAR          26
#define BRICK_COLS      7
#define BRICK_ROWS      4
#define BRICK_N         (BRICK_COLS * BRICK_ROWS)
#define BRICK_H         14
#define MARGIN          6
#define BGAP            4
#define PADDLE_W        48
#define PADDLE_H        9
#define PADDLE_SPEED    6
#define BALL_SZ         9
#define BALL_SPEED      3.4f
#define PADDLE_MAXVX    4.2f
#define START_LIVES     3
#define SCORE_PER_BRICK 10

#define COL_BG     0x0A0014
#define COL_PADDLE 0xE040FB
#define COL_BALL   0xFFFFFF

enum { ST_READY, ST_PLAY, ST_DEAD };

static const uint32_t ROW_COLORS[BRICK_ROWS] = {0xE040FB, 0xBA3FD0, 0x9C27B0, 0x7B1FA2};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_paddle = NULL;
static lv_obj_t *s_ball = NULL;
static lv_obj_t *s_brick[BRICK_N];
static bool s_brick_on[BRICK_N];
static lv_obj_t *s_score_lbl = NULL;
static lv_obj_t *s_msg_panel = NULL, *s_msg_lbl = NULL;
static lv_timer_t *s_timer = NULL;

static int s_state = ST_READY;
static int s_w, s_h;
static float s_px;
static float s_bx, s_by, s_bvx, s_bvy;
static int s_brick_x[BRICK_N], s_brick_y[BRICK_N], s_brick_w;
static int s_alive;
static int s_score, s_lives;
static uint32_t s_best;

static bool s_ok_last, s_back_last;

static uint32_t load_best(void) {
  nvs_handle_t h;
  uint32_t v = 0;
  if (nvs_open("breakout", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u32(h, "best", &v);
    nvs_close(h);
  }
  return v;
}
static void save_best(uint32_t v) {
  nvs_handle_t h;
  if (nvs_open("breakout", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u32(h, "best", v);
    nvs_commit(h);
    nvs_close(h);
  }
}

static void set_score_text(void) {
  lv_label_set_text_fmt(s_score_lbl, "Score %d   Lives %d", s_score, s_lives);
}

static int paddle_y(void) {
  return s_h - 22;
}

static void build_bricks(void) {
  s_brick_w = (s_w - 2 * MARGIN - (BRICK_COLS - 1) * BGAP) / BRICK_COLS;
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      int i = r * BRICK_COLS + c;
      s_brick_x[i] = MARGIN + c * (s_brick_w + BGAP);
      s_brick_y[i] = TOPBAR + 8 + r * (BRICK_H + BGAP);
      s_brick_on[i] = true;
      lv_obj_set_pos(s_brick[i], s_brick_x[i], s_brick_y[i]);
      lv_obj_set_size(s_brick[i], s_brick_w, BRICK_H);
      lv_obj_set_style_bg_color(s_brick[i], lv_color_hex(ROW_COLORS[r]), 0);
      lv_obj_remove_flag(s_brick[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  s_alive = BRICK_N;
}

static void park_ball_on_paddle(void) {
  s_bx = s_px + PADDLE_W / 2.0f - BALL_SZ / 2.0f;
  s_by = paddle_y() - BALL_SZ - 1;
  s_bvx = 0;
  s_bvy = 0;
  lv_obj_set_pos(s_ball, (int)s_bx, (int)s_by);
}

static void reset_game(void) {
  s_state = ST_READY;
  s_score = 0;
  s_lives = START_LIVES;
  s_px = s_w / 2.0f - PADDLE_W / 2.0f;
  lv_obj_set_pos(s_paddle, (int)s_px, paddle_y());
  build_bricks();
  park_ball_on_paddle();
  set_score_text();
  lv_obj_add_flag(s_msg_panel, LV_OBJ_FLAG_HIDDEN);
}

static void launch_ball(void) {
  s_state = ST_PLAY;
  s_bvy = -BALL_SPEED;
  s_bvx = ((esp_random() & 1) ? 1.0f : -1.0f) * (BALL_SPEED * 0.5f);
  game_fx(GFX_START);
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

static bool hit_bricks(void) {
  int bl = (int)s_bx, br = bl + BALL_SZ, bt = (int)s_by, bb = bt + BALL_SZ;
  for (int i = 0; i < BRICK_N; i++) {
    if (!s_brick_on[i])
      continue;
    int xl = s_brick_x[i], xr = xl + s_brick_w, yt = s_brick_y[i], yb = yt + BRICK_H;
    if (br > xl && bl < xr && bb > yt && bt < yb) {
      s_brick_on[i] = false;
      s_alive--;
      lv_obj_add_flag(s_brick[i], LV_OBJ_FLAG_HIDDEN);

      int pen_x = (s_bvx > 0) ? (br - xl) : (xr - bl);
      int pen_y = (s_bvy > 0) ? (bb - yt) : (yb - bt);
      if (pen_x < pen_y)
        s_bvx = -s_bvx;
      else
        s_bvy = -s_bvy;
      s_score += SCORE_PER_BRICK;
      set_score_text();
      game_fx(GFX_SCORE);
      return true;
    }
  }
  return false;
}

static void step_ball(void) {
  s_bx += s_bvx;
  s_by += s_bvy;

  if (s_bx < 0) {
    s_bx = 0;
    s_bvx = -s_bvx;
    game_fx(GFX_BOUNCE);
  }
  if (s_bx + BALL_SZ > s_w) {
    s_bx = s_w - BALL_SZ;
    s_bvx = -s_bvx;
    game_fx(GFX_BOUNCE);
  }
  if (s_by < TOPBAR) {
    s_by = TOPBAR;
    s_bvy = -s_bvy;
    game_fx(GFX_BOUNCE);
  }

  int py = paddle_y();
  if (s_bvy > 0 && s_by + BALL_SZ >= py && s_by + BALL_SZ <= py + PADDLE_H + 4 &&
      s_bx + BALL_SZ > s_px && s_bx < s_px + PADDLE_W) {
    s_by = py - BALL_SZ;
    float hit = ((s_bx + BALL_SZ / 2.0f) - (s_px + PADDLE_W / 2.0f)) / (PADDLE_W / 2.0f);
    if (hit < -1)
      hit = -1;
    if (hit > 1)
      hit = 1;
    s_bvx = hit * PADDLE_MAXVX;
    s_bvy = -BALL_SPEED;
    game_fx(GFX_BOUNCE);
  }

  hit_bricks();

  if (s_by > s_h) {
    s_lives--;
    set_score_text();
    if (s_lives <= 0) {
      die();
      return;
    }
    s_state = ST_READY;
    park_ball_on_paddle();
    return;
  }

  if (s_alive <= 0) {
    build_bricks();
    s_state = ST_READY;
    park_ball_on_paddle();
    return;
  }

  lv_obj_set_pos(s_ball, (int)s_bx, (int)s_by);
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }

  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (!ui_input_is_locked()) {
    if (back && !s_back_last) {
      s_back_last = back;
      ui_switch_screen(SCREEN_GAMES_MENU);
      return;
    }

    if (left)
      s_px -= PADDLE_SPEED;
    if (right)
      s_px += PADDLE_SPEED;
    if (s_px < 0)
      s_px = 0;
    if (s_px + PADDLE_W > s_w)
      s_px = s_w - PADDLE_W;
    lv_obj_set_x(s_paddle, (int)s_px);

    if (ok && !s_ok_last) {
      if (s_state == ST_READY)
        launch_ball();
      else if (s_state == ST_DEAD)
        reset_game();
    }
  }

  if (s_state == ST_READY) {
    park_ball_on_paddle();
  } else if (s_state == ST_PLAY) {
    step_ball();
  }

  s_ok_last = ok;
  s_back_last = back;
}

static lv_obj_t *make_rect(uint32_t color) {
  lv_obj_t *o = lv_obj_create(s_screen);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(o, 3, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

void ui_breakout_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ok_last = s_back_last = false;
  s_best = load_best();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);

  s_w = LCD_H_RES;
  s_h = LCD_V_RES;

  for (int i = 0; i < BRICK_N; i++) {
    s_brick[i] = make_rect(ROW_COLORS[0]);
    lv_obj_add_flag(s_brick[i], LV_OBJ_FLAG_HIDDEN);
  }

  s_paddle = make_rect(COL_PADDLE);
  lv_obj_set_size(s_paddle, PADDLE_W, PADDLE_H);
  lv_obj_set_style_radius(s_paddle, 4, 0);

  s_ball = make_rect(COL_BALL);
  lv_obj_set_size(s_ball, BALL_SZ, BALL_SZ);
  lv_obj_set_style_radius(s_ball, LV_RADIUS_CIRCLE, 0);

  s_score_lbl = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 5);

  s_msg_panel = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_msg_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_msg_panel, s_w - 60, LV_SIZE_CONTENT);
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
