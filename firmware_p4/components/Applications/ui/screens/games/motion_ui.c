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

#include "motion_ui.h"

#include <math.h>

#include "esp_random.h"

#include "buttons_gpio.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define TICK_MS 50

#define BIG_D    156
#define BUBBLE_D 30
#define RING_D   44
#define TRAVEL   59

#define MAX_DEG   24.0f
#define LEVEL_TOL 3.0f
#define WALK_STEP 1.4f
#define DECAY     0.94f

#define COL_LEVEL 0x00E676
#define COL_DIM   0x8A8594

#define HDR_ICON  "/assets/icons/sensors.bin"
#define HDR_TITLE "BUBBLE LEVEL"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_bubble = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_read = NULL;
static lv_timer_t *s_timer = NULL;

static float s_pitch = 0.0f;
static float s_roll = 0.0f;
static bool s_back_last = false;
static bool s_left_last = false;

static float rand_walk_delta(void) {
  int r = (int)(esp_random() % 200) - 100;
  return (float)r / 100.0f * WALK_STEP;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static void build_level(void) {
  lv_obj_t *big = lv_obj_create(s_screen);
  lv_obj_remove_flag(big, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(big, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(big, BIG_D, BIG_D);
  lv_obj_align(big, LV_ALIGN_CENTER, 0, (UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H) / 2 - 24);
  lv_obj_set_style_radius(big, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(big, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(big, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(big, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(big, 2, 0);
  lv_obj_set_style_pad_all(big, 0, 0);
  lv_obj_set_style_shadow_width(big, 22, 0);
  lv_obj_set_style_shadow_color(big, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(big, LV_OPA_40, 0);

  lv_obj_t *ring = lv_obj_create(big);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(ring, RING_D, RING_D);
  lv_obj_center(ring);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(COL_DIM), 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_pad_all(ring, 0, 0);

  s_bubble = lv_obj_create(big);
  lv_obj_remove_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_bubble, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_bubble, BUBBLE_D, BUBBLE_D);
  lv_obj_align(s_bubble, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_bubble, 0, 0);
  lv_obj_set_style_bg_color(s_bubble, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(s_bubble, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(s_bubble, 12, 0);
  lv_obj_set_style_shadow_color(s_bubble, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(s_bubble, LV_OPA_70, 0);

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, "TILTED");
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_status, lv_color_hex(COL_DIM), 0);
  lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -52);

  s_read = lv_label_create(s_screen);
  lv_label_set_text(s_read, "Pitch 0 deg   Roll 0 deg");
  lv_obj_set_style_text_font(s_read, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_read, current_theme.text_main, 0);
  lv_obj_align(s_read, LV_ALIGN_BOTTOM_MID, 0, -30);
}

static void update_physics(void) {
  s_pitch = clampf((s_pitch + rand_walk_delta()) * DECAY, -MAX_DEG, MAX_DEG);
  s_roll = clampf((s_roll + rand_walk_delta()) * DECAY, -MAX_DEG, MAX_DEG);

  float fx = s_roll / MAX_DEG;
  float fy = -s_pitch / MAX_DEG;
  float mag = sqrtf(fx * fx + fy * fy);
  if (mag > 1.0f) {
    fx /= mag;
    fy /= mag;
  }
  int ox = (int)(fx * TRAVEL);
  int oy = (int)(fy * TRAVEL);
  if (s_bubble)
    lv_obj_align(s_bubble, LV_ALIGN_CENTER, ox, oy);

  bool level = (fabsf(s_pitch) <= LEVEL_TOL && fabsf(s_roll) <= LEVEL_TOL);
  lv_color_t c = level ? lv_color_hex(COL_LEVEL) : current_theme.border_accent;
  if (s_bubble) {
    lv_obj_set_style_bg_color(s_bubble, c, 0);
    lv_obj_set_style_shadow_color(s_bubble, c, 0);
  }
  if (s_status) {
    lv_label_set_text(s_status, level ? "LEVEL" : "TILTED");
    lv_obj_set_style_text_color(
        s_status, level ? lv_color_hex(COL_LEVEL) : lv_color_hex(COL_DIM), 0);
  }
  if (s_read)
    lv_label_set_text_fmt(s_read, "Pitch %d deg   Roll %d deg", (int)s_pitch, (int)s_roll);
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool back = back_button_is_down();
  bool left = ui_btn_left();
  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_GAMES_MENU);
    return;
  }

  update_physics();

  s_back_last = back;
  s_left_last = left;
}

void ui_motion_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_pitch = 0.0f;
  s_roll = 0.0f;
  s_back_last = false;
  s_left_last = false;
  s_bubble = NULL;
  s_status = NULL;
  s_read = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);
  build_level();
  ui_chrome_footer(s_screen, "BACK: EXIT");

  if (s_timer == NULL)
    s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
