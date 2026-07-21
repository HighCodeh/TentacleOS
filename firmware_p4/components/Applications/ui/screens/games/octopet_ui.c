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

#include "octopet_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "game_fx.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "OCTOPET";

#define NAV_MS      50
#define LIFE_MS     1500
#define PET_ASSET   "/assets/img/octobit.bin"
#define FAINT_TICKS 10
#define COL_GOOD    0x00E676
#define COL_WARN    0xFFC400
#define COL_BAD     0xFF5252
#define COL_HEART   0xFF4081
#define COL_CRUMB   0xFFB74D
#define COL_SPARKLE 0x40C4FF
#define COL_POOP    0x8D6E63
#define COL_LEVEL   0xFFC400

#define LVL_BASE  6
#define INIT_STAT 75
#define STAT_MAX  100

enum { ACT_FEED = 0, ACT_PLAY, ACT_SLEEP, ACT_CLEAN, ACT_COUNT };
static const char *ACT_NAMES[ACT_COUNT] = {"FEED", "PLAY", "SLEEP", "CLEAN"};

static bool s_inited = false;
static int s_hunger, s_happy, s_energy, s_clean;
static bool s_sleeping = false;
static bool s_fainted = false;
static int s_neglect = 0;
static int s_age = 0;
static int s_level = 1;
static bool s_poop = false;

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_life_timer = NULL;
static lv_obj_t *s_pet = NULL;
static lv_obj_t *s_mood = NULL;
static lv_obj_t *s_zzz = NULL;
static lv_obj_t *s_poop_obj = NULL;
static lv_obj_t *s_faint_ov = NULL;
static lv_obj_t *s_bar[4];
static lv_obj_t *s_val[4];
static lv_obj_t *s_age_lbl = NULL;
static lv_obj_t *s_lvl_lbl = NULL;
static int s_sel = 0;
static lv_obj_t *s_cell[ACT_COUNT];
static int32_t s_pet_rest_y = 0;
static uint32_t s_rng = 0x1234abcd;

static bool s_l_last, s_r_last, s_ok_last, s_back_last;

static void nav_timer_cb(lv_timer_t *t);
static void life_tick_cb(lv_timer_t *t);
static void refresh_pet_look(void);

static uint32_t rng_next(void) {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

static int level_for_age(int age) {
  int lvl = 1, need = LVL_BASE, acc = 0;
  while (age >= acc + need) {
    acc += need;
    lvl++;
    need += LVL_BASE;
  }
  return lvl;
}

static void init_pet(void) {
  s_hunger = s_happy = s_energy = s_clean = INIT_STAT;
  s_sleeping = false;
  s_fainted = false;
  s_neglect = 0;
  s_age = 0;
  s_level = 1;
  s_poop = false;
  s_inited = true;
}

static int clampi(int v) {
  return v < 0 ? 0 : (v > STAT_MAX ? STAT_MAX : v);
}
static int stat_min(void) {
  int m = s_hunger;
  if (s_happy < m)
    m = s_happy;
  if (s_energy < m)
    m = s_energy;
  if (s_clean < m)
    m = s_clean;
  return m;
}
static uint32_t bar_color(int v) {
  return v < 25 ? COL_BAD : (v < 55 ? COL_WARN : COL_GOOD);
}

static void float_del_cb(lv_anim_t *a) {
  lv_obj_del((lv_obj_t *)a->var);
}
static void float_y_cb(void *var, int32_t v) {
  lv_obj_set_y((lv_obj_t *)var, v);
}
static void float_x_cb(void *var, int32_t v) {
  lv_obj_set_x((lv_obj_t *)var, v);
}
static void float_opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void spawn_particle(const char *text,
                           uint32_t color,
                           const lv_font_t *font,
                           int32_t x0,
                           int32_t y0,
                           int32_t dx,
                           int32_t dy,
                           uint32_t dur) {
  if (s_screen == NULL)
    return;
  lv_obj_t *l = lv_label_create(s_screen);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_pos(l, x0, y0);

  lv_anim_t ay;
  lv_anim_init(&ay);
  lv_anim_set_var(&ay, l);
  lv_anim_set_exec_cb(&ay, float_y_cb);
  lv_anim_set_values(&ay, y0, y0 + dy);
  lv_anim_set_duration(&ay, dur);
  lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&ay, float_del_cb);
  lv_anim_start(&ay);

  if (dx != 0) {
    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, l);
    lv_anim_set_exec_cb(&ax, float_x_cb);
    lv_anim_set_values(&ax, x0, x0 + dx);
    lv_anim_set_duration(&ax, dur);
    lv_anim_set_path_cb(&ax, lv_anim_path_ease_in_out);
    lv_anim_start(&ax);
  }

  lv_anim_t ao;
  lv_anim_init(&ao);
  lv_anim_set_var(&ao, l);
  lv_anim_set_exec_cb(&ao, float_opa_cb);
  lv_anim_set_values(&ao, 255, 0);
  lv_anim_set_duration(&ao, dur);
  lv_anim_start(&ao);
}

static void feedback(const char *text, uint32_t color) {
  if (s_screen == NULL)
    return;
  lv_obj_t *l = lv_label_create(s_screen);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, -24);
  int32_t y0 = lv_obj_get_y(l);

  lv_anim_t ay;
  lv_anim_init(&ay);
  lv_anim_set_var(&ay, l);
  lv_anim_set_exec_cb(&ay, float_y_cb);
  lv_anim_set_values(&ay, y0, y0 - 38);
  lv_anim_set_duration(&ay, 700);
  lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&ay, float_del_cb);
  lv_anim_start(&ay);

  lv_anim_t ao;
  lv_anim_init(&ao);
  lv_anim_set_var(&ao, l);
  lv_anim_set_exec_cb(&ao, float_opa_cb);
  lv_anim_set_values(&ao, 255, 0);
  lv_anim_set_duration(&ao, 700);
  lv_anim_start(&ao);
}

static void burst(int kind) {
  if (s_screen == NULL)
    return;

  int32_t sw = lv_obj_get_width(s_screen);
  int32_t sh = lv_obj_get_height(s_screen);
  int32_t bx = sw / 2;
  int32_t by = sh / 2;

  switch (kind) {
    case ACT_FEED: {
      for (int i = 0; i < 5; i++) {
        int32_t ox = (int32_t)(rng_next() % 44) - 22;
        spawn_particle(".",
                       COL_CRUMB,
                       &lv_font_montserrat_14,
                       bx + ox,
                       by + 6,
                       (int32_t)(rng_next() % 16) - 8,
                       22 + (int32_t)(rng_next() % 14),
                       640);
      }
      break;
    }
    case ACT_PLAY: {
      for (int i = 0; i < 4; i++) {
        int32_t ox = (int32_t)(rng_next() % 50) - 25;
        const char *g = (i & 1) ? "<3" : "*";
        spawn_particle(g,
                       COL_HEART,
                       (i & 1) ? &lv_font_montserrat_14 : &lv_font_montserrat_12,
                       bx + ox,
                       by - 6,
                       (int32_t)(rng_next() % 18) - 9,
                       -(34 + (int32_t)(rng_next() % 20)),
                       820);
      }
      break;
    }
    case ACT_CLEAN: {
      for (int i = 0; i < 6; i++) {
        int32_t ox = (int32_t)(rng_next() % 60) - 30;
        spawn_particle("+",
                       COL_SPARKLE,
                       &lv_font_montserrat_12,
                       bx + ox,
                       by - 2,
                       (int32_t)(rng_next() % 24) - 12,
                       -(20 + (int32_t)(rng_next() % 22)),
                       700);
      }
      break;
    }
    default:
      break;
  }
}

static void poop_gone_cb(lv_anim_t *a) {
  lv_obj_del((lv_obj_t *)a->var);
  if ((lv_obj_t *)a->var == s_poop_obj)
    s_poop_obj = NULL;
}

static void make_poop_obj(void) {
  if (s_poop_obj || s_screen == NULL)
    return;
  s_poop_obj = lv_label_create(s_screen);
  lv_label_set_text(s_poop_obj, "~");
  lv_obj_set_style_text_color(s_poop_obj, lv_color_hex(COL_POOP), 0);
  lv_obj_set_style_text_font(s_poop_obj, &lv_font_montserrat_16, 0);
  lv_obj_align(s_poop_obj, LV_ALIGN_CENTER, 38, 44);

  lv_obj_set_style_opa(s_poop_obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_poop_obj);
  lv_anim_set_exec_cb(&a, float_opa_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, 280);
  lv_anim_start(&a);
}

static void add_poop(void) {
  if (s_poop)
    return;
  s_poop = true;
  make_poop_obj();
}

static void sweep_poop_away(void) {
  if (!s_poop)
    return;
  s_poop = false;
  if (s_poop_obj) {
    int32_t x0 = lv_obj_get_x(s_poop_obj);
    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, s_poop_obj);
    lv_anim_set_exec_cb(&ax, float_x_cb);
    lv_anim_set_values(&ax, x0, x0 + 60);
    lv_anim_set_duration(&ax, 380);
    lv_anim_set_path_cb(&ax, lv_anim_path_ease_in);
    lv_anim_start(&ax);
    lv_anim_t ao;
    lv_anim_init(&ao);
    lv_anim_set_var(&ao, s_poop_obj);
    lv_anim_set_exec_cb(&ao, float_opa_cb);
    lv_anim_set_values(&ao, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&ao, 380);
    lv_anim_set_completed_cb(&ao, poop_gone_cb);
    lv_anim_start(&ao);
  }
}

static const char *mood_face(uint32_t *col) {
  if (s_fainted) {
    *col = COL_BAD;
    return "x_x";
  }
  if (s_sleeping) {
    *col = 0x82B1FF;
    return "-_-";
  }
  if (s_poop && s_clean < 40) {
    *col = COL_POOP;
    return ">_<";
  }
  if (s_hunger < 25) {
    *col = COL_BAD;
    return ":<";
  }
  if (s_clean < 25) {
    *col = COL_POOP;
    return ":S";
  }
  if (s_energy < 25) {
    *col = COL_WARN;
    return "u_u";
  }
  if (s_happy < 25) {
    *col = COL_WARN;
    return ":(";
  }
  if (stat_min() >= 70) {
    *col = COL_GOOD;
    return ":D";
  }
  *col = COL_GOOD;
  return ":)";
}

static const char *mood_word(void) {
  if (s_fainted)
    return "Fainted...";
  if (s_sleeping)
    return "Sleeping";
  if (s_poop && s_clean < 40)
    return "Eww, poop!";
  if (s_hunger < 25)
    return "Hungry!";
  if (s_energy < 25)
    return "Sleepy...";
  if (s_clean < 25)
    return "Dirty!";
  if (s_happy < 25)
    return "Sad";
  if (stat_min() >= 70)
    return "Happy!";
  return "OK";
}

static void refresh_pet_look(void) {
  if (s_pet) {
    lv_obj_set_style_opa(s_pet, s_sleeping ? LV_OPA_60 : LV_OPA_COVER, 0);
    bool sick = (!s_sleeping && stat_min() < 20);
    lv_obj_set_style_image_recolor_opa(s_pet, sick ? LV_OPA_40 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_image_recolor(s_pet, lv_color_hex(COL_BAD), 0);
  }
  if (s_mood) {
    uint32_t col;
    const char *face = mood_face(&col);
    lv_label_set_text(s_mood, face);
    lv_obj_set_style_text_color(s_mood, lv_color_hex(col), 0);
  }

  if (s_sleeping && s_zzz == NULL && s_screen) {
    s_zzz = lv_label_create(s_screen);
    lv_label_set_text(s_zzz, "Zzz");
    lv_obj_set_style_text_color(s_zzz, current_theme.border_accent, 0);
    lv_obj_set_style_text_font(s_zzz, &lv_font_montserrat_16, 0);
    lv_obj_align(s_zzz, LV_ALIGN_CENTER, 46, -54);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_zzz);
    lv_anim_set_exec_cb(&a, float_opa_cb);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_playback_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
  } else if (!s_sleeping && s_zzz != NULL) {
    lv_obj_del(s_zzz);
    s_zzz = NULL;
  }

  if (s_poop && s_poop_obj == NULL && !s_fainted)
    make_poop_obj();
}

static void refresh_bars(bool anim) {
  int v[4] = {s_hunger, s_happy, s_energy, s_clean};
  for (int i = 0; i < 4; i++) {
    if (s_bar[i]) {
      lv_bar_set_value(s_bar[i], v[i], anim ? LV_ANIM_ON : LV_ANIM_OFF);
      lv_obj_set_style_bg_color(s_bar[i], lv_color_hex(bar_color(v[i])), LV_PART_INDICATOR);
    }
    if (s_val[i]) {
      lv_label_set_text_fmt(s_val[i], "%d", v[i]);
      lv_obj_set_style_text_color(s_val[i], lv_color_hex(bar_color(v[i])), 0);
    }
  }
}

static void refresh_age(void) {
  if (s_age_lbl)
    lv_label_set_text_fmt(s_age_lbl, "AGE %d", s_age);
  if (s_lvl_lbl)
    lv_label_set_text_fmt(s_lvl_lbl, "Lv %d", s_level);
}

static void pet_scale_cb(void *var, int32_t v) {
  lv_image_set_scale((lv_obj_t *)var, (uint32_t)v);
}
static void pet_pop(void) {
  if (s_pet == NULL)
    return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_pet);
  lv_anim_set_exec_cb(&a, pet_scale_cb);
  lv_anim_set_values(&a, 256, 296);
  lv_anim_set_duration(&a, 120);
  lv_anim_set_playback_duration(&a, 140);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void pet_wiggle(void) {
  if (s_pet == NULL)
    return;
  int32_t x0 = lv_obj_get_x(s_pet);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_pet);
  lv_anim_set_exec_cb(&a, float_x_cb);
  lv_anim_set_values(&a, x0, x0 + 4);
  lv_anim_set_duration(&a, 90);
  lv_anim_set_playback_duration(&a, 90);
  lv_anim_set_repeat_count(&a, 2);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static void pet_blink(void) {
  if (s_mood == NULL)
    return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_mood);
  lv_anim_set_exec_cb(&a, float_opa_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
  lv_anim_set_duration(&a, 110);
  lv_anim_set_playback_duration(&a, 110);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static void update_action_focus(void) {
  for (int i = 0; i < ACT_COUNT; i++) {
    if (!s_cell[i])
      continue;
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(
        s_cell[i], sel ? current_theme.border_accent : current_theme.border_interface, 0);
    lv_obj_set_style_border_width(s_cell[i], sel ? 3 : 1, 0);
    lv_obj_set_style_bg_opa(s_cell[i], sel ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(s_cell[i], sel ? 12 : 0, 0);
    lv_obj_set_style_shadow_color(s_cell[i], current_theme.border_accent, 0);
    lv_obj_set_style_shadow_spread(s_cell[i], sel ? 1 : 0, 0);
  }
}

static void do_action(int a) {
  switch (a) {
    case ACT_FEED:
      s_hunger = clampi(s_hunger + 28);
      s_clean = clampi(s_clean - 6);
      game_fx(GFX_EAT);
      feedback("+ Food", COL_GOOD);
      burst(ACT_FEED);

      if (!s_poop && (rng_next() % 100) < 35)
        add_poop();
      break;
    case ACT_PLAY:
      if (s_sleeping) {
        feedback("zzz...", COL_WARN);
        break;
      }
      s_happy = clampi(s_happy + 28);
      s_energy = clampi(s_energy - 12);
      game_fx(GFX_SCORE);
      feedback("Fun!", COL_HEART);
      burst(ACT_PLAY);
      break;
    case ACT_SLEEP:
      s_sleeping = !s_sleeping;
      game_fx(GFX_START);
      feedback(s_sleeping ? "Zzz" : "Wake!", 0x82B1FF);
      break;
    case ACT_CLEAN:
      s_clean = clampi(s_clean + 40);
      game_fx(GFX_BOUNCE);
      feedback(s_poop ? "Sparkly!" : "Clean!", COL_SPARKLE);
      burst(ACT_CLEAN);
      sweep_poop_away();
      break;
    default:
      break;
  }
  pet_pop();
  refresh_bars(true);
  refresh_pet_look();
}

static void show_faint(void) {
  s_fainted = true;
  s_sleeping = false;
  if (s_faint_ov || s_screen == NULL)
    return;
  s_faint_ov = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_faint_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_faint_ov, lv_pct(80), 96);
  lv_obj_align(s_faint_ov, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(s_faint_ov, 12, 0);
  lv_obj_set_style_bg_color(s_faint_ov, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(s_faint_ov, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_faint_ov, 2, 0);
  lv_obj_set_style_border_color(s_faint_ov, lv_color_hex(COL_BAD), 0);
  lv_obj_set_flex_flow(s_faint_ov, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      s_faint_ov, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *t = lv_label_create(s_faint_ov);
  lv_label_set_text(t, "Octo fainted!");
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
  lv_obj_t *h = lv_label_create(s_faint_ov);
  lv_label_set_text(h, "OK to revive");
  lv_obj_set_style_text_color(h, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(h, &lv_font_montserrat_12, 0);

  game_fx(GFX_CRASH);
}

static void revive(void) {
  if (s_faint_ov) {
    lv_obj_del(s_faint_ov);
    s_faint_ov = NULL;
  }
  if (s_poop_obj) {
    lv_obj_del(s_poop_obj);
    s_poop_obj = NULL;
  }
  init_pet();
  refresh_bars(true);
  refresh_age();
  refresh_pet_look();
  feedback("Revived!", COL_GOOD);
}

static void life_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_life_timer = NULL;
    return;
  }
  if (s_fainted)
    return;

  s_age++;
  int new_level = level_for_age(s_age);
  if (new_level > s_level) {
    s_level = new_level;
    game_fx(GFX_SCORE);
    feedback("Level up!", COL_LEVEL);
  }
  refresh_age();

  if (s_sleeping) {
    s_energy = clampi(s_energy + 8);
    s_hunger = clampi(s_hunger - 1);
    s_clean = clampi(s_clean - 1);
    if (s_energy >= STAT_MAX)
      s_sleeping = false;
  } else {
    s_hunger = clampi(s_hunger - 3);
    s_happy = clampi(s_happy - 2);
    s_energy = clampi(s_energy - 2);
    s_clean = clampi(s_clean - 2);
  }

  if (s_poop) {
    s_clean = clampi(s_clean - 3);
    if ((s_age & 1) == 0)
      s_happy = clampi(s_happy - 1);
  } else if (!s_sleeping && (rng_next() % 100) < 6) {
    add_poop();
  }

  if (!s_sleeping) {
    uint32_t r = rng_next() % 100;
    if (r < 12)
      pet_blink();
    else if (r < 18)
      pet_wiggle();
  }

  if (s_hunger == 0 && s_energy == 0)
    s_neglect++;
  else
    s_neglect = 0;

  refresh_bars(true);
  refresh_pet_look();

  if (s_neglect >= FAINT_TICKS)
    show_faint();
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_GAMES_MENU);
    goto edges;
  }

  if (s_fainted) {
    if (ok && !s_ok_last)
      revive();
    goto edges;
  }

  if (left && !s_l_last) {
    s_sel = (s_sel - 1 + ACT_COUNT) % ACT_COUNT;
    update_action_focus();
  }
  if (right && !s_r_last) {
    s_sel = (s_sel + 1) % ACT_COUNT;
    update_action_focus();
  }
  if (ok && !s_ok_last)
    do_action(s_sel);

edges:
  s_l_last = left;
  s_r_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_octopet_open(void) {
  if (!s_inited)
    init_pet();

  s_level = level_for_age(s_age);

  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_zzz = NULL;
  s_poop_obj = NULL;
  s_faint_ov = NULL;
  s_l_last = s_r_last = s_ok_last = s_back_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, "OCTO-PET");
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

  s_lvl_lbl = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_lvl_lbl, "Lv %d", s_level);
  lv_obj_set_style_text_color(s_lvl_lbl, lv_color_hex(COL_LEVEL), 0);
  lv_obj_set_style_text_font(s_lvl_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_lvl_lbl, LV_ALIGN_TOP_LEFT, 8, 8);

  s_age_lbl = lv_label_create(s_screen);
  lv_label_set_text_fmt(s_age_lbl, "AGE %d", s_age);
  lv_obj_set_style_text_color(s_age_lbl, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(s_age_lbl, &lv_font_montserrat_12, 0);
  lv_obj_align(s_age_lbl, LV_ALIGN_TOP_RIGHT, -8, 8);

  static const char *cap[4] = {"HUN", "HAP", "ENE", "CLN"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *c = lv_label_create(s_screen);
    lv_label_set_text(c, cap[i]);
    lv_obj_set_style_text_color(c, current_theme.border_inactive, 0);
    lv_obj_set_style_text_font(c, &lv_font_montserrat_12, 0);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 8, 30 + i * 18);

    lv_obj_t *b = lv_bar_create(s_screen);
    lv_obj_set_size(b, 88, 10);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, 44, 32 + i * 18);
    lv_bar_set_range(b, 0, STAT_MAX);
    lv_obj_set_style_bg_color(b, current_theme.bg_secondary, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 5, LV_PART_INDICATOR);
    s_bar[i] = b;

    lv_obj_t *vl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(vl, &lv_font_montserrat_12, 0);
    lv_obj_align(vl, LV_ALIGN_TOP_LEFT, 138, 30 + i * 18);
    s_val[i] = vl;
  }

  lv_obj_t *glow = lv_obj_create(s_screen);
  lv_obj_remove_flag(glow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(glow, 108, 108);
  lv_obj_align(glow, LV_ALIGN_CENTER, 0, 2);
  lv_obj_set_style_radius(glow, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(glow, 0, 0);
  lv_obj_set_style_bg_color(glow, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(glow, LV_OPA_20, 0);

  lv_obj_t *plat = lv_obj_create(s_screen);
  lv_obj_remove_flag(plat, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(plat, 118, 14);
  lv_obj_align(plat, LV_ALIGN_CENTER, 0, 58);
  lv_obj_set_style_radius(plat, 7, 0);
  lv_obj_set_style_border_width(plat, 0, 0);
  lv_obj_set_style_bg_color(plat, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(plat, LV_OPA_70, 0);

  lv_image_dsc_t *dsc = assets_get(PET_ASSET);
  if (dsc != NULL) {
    s_pet = lv_image_create(s_screen);
    lv_image_set_src(s_pet, dsc);
    lv_image_set_pivot(s_pet, dsc->header.w / 2, dsc->header.h / 2);
    lv_obj_align(s_pet, LV_ALIGN_CENTER, 0, 6);
    s_pet_rest_y = lv_obj_get_y(s_pet);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_pet);
    lv_anim_set_exec_cb(&a, float_y_cb);
    lv_anim_set_values(&a, s_pet_rest_y, s_pet_rest_y - 6);
    lv_anim_set_duration(&a, 1100);
    lv_anim_set_playback_duration(&a, 1100);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }

  s_mood = lv_label_create(s_screen);
  lv_obj_set_style_text_color(s_mood, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_mood, &lv_font_montserrat_16, 0);
  lv_obj_align(s_mood, LV_ALIGN_CENTER, -46, -34);

  lv_obj_t *moodw = lv_label_create(s_screen);
  lv_obj_set_style_text_color(moodw, current_theme.text_main, 0);
  lv_obj_set_style_text_font(moodw, &lv_font_montserrat_14, 0);
  lv_obj_align(moodw, LV_ALIGN_BOTTOM_MID, 0, -64);
  lv_label_set_text(moodw, mood_word());

  lv_obj_t *bar = lv_obj_create(s_screen);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bar, lv_pct(100), 48);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 2, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (int i = 0; i < ACT_COUNT; i++) {
    lv_obj_t *cell = lv_obj_create(bar);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cell, 70, 38);
    lv_obj_set_style_radius(cell, 8, 0);
    lv_obj_set_style_bg_color(cell, current_theme.bg_primary, 0);
    lv_obj_set_style_bg_grad_color(cell, current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_grad_dir(cell, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(cell, current_theme.border_interface, 0);
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_t *l = lv_label_create(cell);
    lv_label_set_text(l, ACT_NAMES[i]);
    lv_obj_set_style_text_color(l, current_theme.text_main, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);
    s_cell[i] = cell;
  }

  refresh_bars(false);
  refresh_age();
  update_action_focus();
  refresh_pet_look();
  if (s_fainted)
    show_faint();

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_MS, NULL);
  if (s_life_timer == NULL)
    s_life_timer = lv_timer_create(life_tick_cb, LIFE_MS, NULL);

  ui_screen_load(s_screen);
  ESP_LOGI(
      TAG, "octo-pet opened (H%d P%d E%d C%d Lv%d)", s_hunger, s_happy, s_energy, s_clean, s_level);
}
