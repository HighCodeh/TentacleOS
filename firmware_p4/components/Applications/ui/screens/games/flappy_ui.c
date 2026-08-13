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

#include "flappy_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"
#include "nvs.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "game_fx.h"
#include "st7789.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TICK_MS    33
#define PIPE_COUNT 3
#define PIPE_W     38
#define CAP_H      12
#define CAP_OVER   5
#define GAP        100
#define BIRD_X     60
#define BIRD_W     34
#define BIRD_H     49
#define HB_MX      7
#define HB_MY      11
#define HB_W       (BIRD_W - 2 * HB_MX)
#define HB_H       (BIRD_H - 2 * HB_MY)
#define GROUND_H   20
#define GRAVITY    0.9f
#define FLAP_V     (-7.7f)
#define VEL_MAX    11.0f
#define VEL_MIN    (-10.0f)
#define SPEED      2.6f
#define GAP_MARGIN 28
#define STAR_COUNT 7

#define COL_BG     0x0A0014
#define COL_PIPE   0x9C27B0
#define COL_CAP    0xBA3FD0
#define COL_GROUND 0x3A0A4A
#define COL_STAR   0x46286A

enum { ST_READY, ST_PLAY, ST_DEAD };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_bird = NULL;
static lv_obj_t *s_pipe_top[PIPE_COUNT];
static lv_obj_t *s_pipe_bot[PIPE_COUNT];
static lv_obj_t *s_cap_top[PIPE_COUNT];
static lv_obj_t *s_cap_bot[PIPE_COUNT];
static lv_obj_t *s_star[STAR_COUNT];
static lv_obj_t *s_ground = NULL;
static lv_obj_t *s_score_lbl = NULL;
static lv_obj_t *s_msg_panel = NULL;
static lv_obj_t *s_msg_lbl = NULL;
static lv_timer_t *s_timer = NULL;

static int s_state = ST_READY;
static int s_w = 0, s_h = 0, s_playh = 0;
static float s_bird_y = 0, s_vel = 0;
static float s_pipe_x[PIPE_COUNT];
static int s_gap_y[PIPE_COUNT];
static bool s_scored[PIPE_COUNT];
static float s_star_x[STAR_COUNT];
static int s_star_y[STAR_COUNT];
static float s_spacing = 0;
static int s_score = 0;
static uint32_t s_best = 0;

static bool s_ok_last, s_up_last, s_back_last;

static uint32_t load_best(void) {
  nvs_handle_t h;
  uint32_t v = 0;
  if (nvs_open("flappy", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u32(h, "best", &v);
    nvs_close(h);
  }
  return v;
}
static void save_best(uint32_t v) {
  nvs_handle_t h;
  if (nvs_open("flappy", NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u32(h, "best", v);
    nvs_commit(h);
    nvs_close(h);
  }
}

static int rand_gap_y(void) {
  int lo = GAP / 2 + GAP_MARGIN;
  int hi = s_playh - GAP / 2 - GAP_MARGIN;
  if (hi <= lo)
    return s_playh / 2;
  return lo + (int)(esp_random() % (uint32_t)(hi - lo));
}

static void layout_pipe(int i) {
  int x = (int)s_pipe_x[i];
  int gap_top = s_gap_y[i] - GAP / 2;
  int gap_bot = s_gap_y[i] + GAP / 2;
  lv_obj_set_pos(s_pipe_top[i], x, 0);
  lv_obj_set_size(s_pipe_top[i], PIPE_W, gap_top > 0 ? gap_top : 1);
  lv_obj_set_pos(s_pipe_bot[i], x, gap_bot);
  lv_obj_set_size(s_pipe_bot[i], PIPE_W, (s_playh - gap_bot) > 0 ? (s_playh - gap_bot) : 1);
  lv_obj_set_pos(s_cap_top[i], x - CAP_OVER, gap_top - CAP_H);
  lv_obj_set_pos(s_cap_bot[i], x - CAP_OVER, gap_bot);
}

static void set_score_text(void) {
  lv_label_set_text_fmt(s_score_lbl, "%d", s_score);
}

static void reset_game(void) {
  s_state = ST_READY;
  s_score = 0;
  s_vel = 0;
  s_bird_y = s_playh / 2.0f - BIRD_H / 2.0f;
  for (int i = 0; i < PIPE_COUNT; i++) {
    s_pipe_x[i] = s_w + 40 + i * s_spacing;
    s_gap_y[i] = rand_gap_y();
    s_scored[i] = false;
    layout_pipe(i);
  }
  lv_obj_set_y(s_bird, (int)s_bird_y);
  lv_image_set_rotation(s_bird, 0);
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

static void update_bird_tilt(void) {
  float deg = s_vel * 4.0f;
  if (deg < -28)
    deg = -28;
  if (deg > 72)
    deg = 72;
  lv_image_set_rotation(s_bird, (int16_t)(deg * 10));
}

static void step_physics(void) {
  s_vel += GRAVITY;
  if (s_vel > VEL_MAX)
    s_vel = VEL_MAX;
  if (s_vel < VEL_MIN)
    s_vel = VEL_MIN;
  s_bird_y += s_vel;
  if (s_bird_y < 0) {
    s_bird_y = 0;
    s_vel = 0;
  }
  lv_obj_set_y(s_bird, (int)s_bird_y);
  update_bird_tilt();

  for (int i = 0; i < STAR_COUNT; i++) {
    s_star_x[i] -= SPEED * 0.35f;
    if (s_star_x[i] < -4) {
      s_star_x[i] += s_w + 8;
      s_star_y[i] = 10 + (int)(esp_random() % (uint32_t)(s_playh - 20));
      lv_obj_set_y(s_star[i], s_star_y[i]);
    }
    lv_obj_set_x(s_star[i], (int)s_star_x[i]);
  }

  for (int i = 0; i < PIPE_COUNT; i++) {
    s_pipe_x[i] -= SPEED;
    if (s_pipe_x[i] + PIPE_W < 0) {
      s_pipe_x[i] += s_spacing * PIPE_COUNT;
      s_gap_y[i] = rand_gap_y();
      s_scored[i] = false;
    }
    layout_pipe(i);
    if (!s_scored[i] && s_pipe_x[i] + PIPE_W < BIRD_X) {
      s_scored[i] = true;
      s_score++;
      set_score_text();
      game_fx(GFX_SCORE);
    }
  }

  int hb_l = BIRD_X + HB_MX, hb_r = hb_l + HB_W;
  int hb_t = (int)s_bird_y + HB_MY, hb_b = hb_t + HB_H;
  if (hb_b >= s_playh) {
    s_bird_y = s_playh - HB_MY - HB_H;
    lv_obj_set_y(s_bird, (int)s_bird_y);
    die();
    return;
  }
  for (int i = 0; i < PIPE_COUNT; i++) {
    int px = (int)s_pipe_x[i];
    if (hb_r > px && hb_l < px + PIPE_W) {
      int gap_top = s_gap_y[i] - GAP / 2;
      int gap_bot = s_gap_y[i] + GAP / 2;
      if (hb_t < gap_top || hb_b > gap_bot) {
        die();
        return;
      }
    }
  }
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  bool ok = ok_button_is_down();
  bool up = ui_btn_up();
  bool back = back_button_is_down();

  if (!ui_input_is_locked()) {
    if (back && !s_back_last) {
      s_back_last = back;
      ui_switch_screen(SCREEN_GAMES_MENU);
      return;
    }
    bool flap_edge = (ok && !s_ok_last) || (up && !s_up_last);
    if (flap_edge) {
      if (s_state == ST_READY) {
        s_state = ST_PLAY;
        set_score_text();
        s_vel = FLAP_V;
        game_fx(GFX_START);
      } else if (s_state == ST_PLAY) {
        s_vel = FLAP_V;
        game_fx(GFX_FLAP);
      } else {
        reset_game();
      }
    }
  }

  if (s_state == ST_PLAY)
    step_physics();

  s_ok_last = ok;
  s_up_last = up;
  s_back_last = back;
}

static lv_obj_t *make_rect(uint32_t color, uint32_t border) {
  lv_obj_t *o = lv_obj_create(s_screen);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_radius(o, 3, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(o, border ? 2 : 0, 0);
  if (border)
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  return o;
}

void ui_flappy_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ok_last = s_up_last = s_back_last = false;
  s_best = load_best();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);

  s_w = LCD_H_RES;
  s_h = LCD_V_RES;
  s_playh = s_h - GROUND_H;
  s_spacing = (s_w + PIPE_W) / 2.0f;

  for (int i = 0; i < STAR_COUNT; i++) {
    s_star[i] = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_star[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_star[i], 3, 3);
    lv_obj_set_style_radius(s_star[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_star[i], 0, 0);
    lv_obj_set_style_bg_opa(s_star[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_star[i], lv_color_hex(COL_STAR), 0);
    s_star_x[i] = (float)(esp_random() % (uint32_t)s_w);
    s_star_y[i] = 10 + (int)(esp_random() % (uint32_t)(s_playh - 20));
    lv_obj_set_pos(s_star[i], (int)s_star_x[i], s_star_y[i]);
  }

  for (int i = 0; i < PIPE_COUNT; i++) {
    s_pipe_top[i] = make_rect(COL_PIPE, 0xCC00FF);
    s_pipe_bot[i] = make_rect(COL_PIPE, 0xCC00FF);
    s_cap_top[i] = make_rect(COL_CAP, 0xCC00FF);
    s_cap_bot[i] = make_rect(COL_CAP, 0xCC00FF);
    lv_obj_set_size(s_cap_top[i], PIPE_W + 2 * CAP_OVER, CAP_H);
    lv_obj_set_size(s_cap_bot[i], PIPE_W + 2 * CAP_OVER, CAP_H);
  }

  s_ground = make_rect(COL_GROUND, 0xCC00FF);
  lv_obj_set_size(s_ground, s_w, GROUND_H);
  lv_obj_set_pos(s_ground, 0, s_playh);

  s_bird = lv_image_create(s_screen);
  lv_image_dsc_t *dsc = assets_get("/assets/img/octobit_bird.bin");
  if (dsc != NULL)
    lv_image_set_src(s_bird, dsc);
  lv_image_set_pivot(s_bird, BIRD_W / 2, BIRD_H / 2);
  lv_obj_set_x(s_bird, BIRD_X);

  s_score_lbl = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_16, 0);
  lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 8);

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
  lv_label_set_text(s_score_lbl, "TAP OK");

  if (s_timer == NULL)
    s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);

  ui_screen_load(s_screen);
}
