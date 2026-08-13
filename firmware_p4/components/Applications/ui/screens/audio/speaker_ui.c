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

#include "speaker_ui.h"

#include <math.h>

#include "audio_i2s.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "menu_component_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "SPEAKER_UI";

#define NAV_TIMER_MS      50
#define VOLUME_ROW        0
#define CATEGORY_ROW      1
#define SONG_ROW_BASE     2
#define SND_AMP           0.75f
#define MAX_SEQ           64
#define N_EQ              10
#define SPK_TASK_STACK    4096
#define SPK_TASK_PRIORITY SYS_PRIO_SERVICE_LO
#define NP_TIMER_MS       33
#define MAX_SONG_ROWS     10
#define COUNT(a)          ((int)(sizeof(a) / sizeof((a)[0])))

typedef struct {
  const char *name;
  const audio_note_t *notes;
  uint16_t count;
  uint8_t tempo_pct;
} speaker_song_t;

typedef struct {
  const char *name;
  const speaker_song_t *songs;
  int count;
} speaker_cat_t;

static const audio_note_t M_MARIO[] = {
    {659, 120},
    {0, 60},
    {659, 120},
    {0, 120},
    {659, 120},
    {0, 120},
    {523, 120},
    {659, 120},
    {0, 60},
    {784, 160},
    {0, 320},
    {392, 160},
};
static const audio_note_t M_TETRIS[] = {
    {659, 300}, {494, 150}, {523, 150}, {587, 300}, {523, 150}, {494, 150}, {440, 300},
    {440, 150}, {523, 150}, {659, 300}, {587, 150}, {523, 150}, {494, 420}, {523, 150},
    {587, 300}, {659, 300}, {523, 300}, {440, 360}, {0, 120},
};
static const audio_note_t M_ZELDA[] = {
    {784, 130},
    {740, 130},
    {622, 130},
    {440, 130},
    {415, 130},
    {659, 130},
    {831, 130},
    {1047, 440},
};
static const audio_note_t M_COIN[] = {{988, 90}, {1319, 520}};
static const audio_note_t M_POWERUP[] = {
    {523, 60},
    {659, 60},
    {784, 60},
    {1047, 60},
    {1319, 60},
    {1047, 60},
    {1175, 140},
};
static const audio_note_t M_LASER[] = {
    {2600, 40},
    {2100, 40},
    {1600, 40},
    {1100, 40},
    {700, 60},
    {400, 80},
};

static const audio_note_t M_ODE[] = {
    {659, 200},
    {659, 200},
    {698, 200},
    {784, 200},
    {784, 200},
    {698, 200},
    {659, 200},
    {587, 200},
    {523, 200},
    {523, 200},
    {587, 200},
    {659, 200},
    {659, 280},
    {587, 360},
};
static const audio_note_t M_ELISE[] = {
    {659, 160},
    {622, 160},
    {659, 160},
    {622, 160},
    {659, 160},
    {494, 160},
    {587, 160},
    {523, 160},
    {440, 360},
};
static const audio_note_t M_TWINKLE[] = {
    {523, 300},
    {523, 300},
    {784, 300},
    {784, 300},
    {880, 300},
    {880, 300},
    {784, 500},
    {698, 300},
    {698, 300},
    {659, 300},
    {659, 300},
    {587, 300},
    {587, 300},
    {523, 500},
};
static const audio_note_t M_MINUET[] = {
    {587, 380},
    {392, 190},
    {440, 190},
    {494, 190},
    {523, 190},
    {587, 380},
    {392, 380},
    {392, 380},
};

static const audio_note_t M_BEEP1K[] = {{1000, 350}};
static const audio_note_t M_BEEP2K[] = {{2000, 350}};
static const audio_note_t M_CHIME[] = {{523, 140}, {659, 140}, {784, 240}};
static const audio_note_t M_SIREN[] = {
    {600, 220},
    {900, 220},
    {600, 220},
    {900, 220},
    {600, 220},
    {900, 220},
    {600, 220},
    {900, 220},
};
static const audio_note_t M_ALARM[] = {
    {2500, 80},
    {0, 60},
    {2500, 80},
    {0, 60},
    {2500, 80},
    {0, 60},
    {2500, 80},
    {0, 60},
    {2500, 80},
    {0, 60},
    {2500, 80},
    {0, 60},
};
static const audio_note_t M_SOS[] = {
    {800, 100},
    {0, 90},
    {800, 100},
    {0, 90},
    {800, 100},
    {0, 260},
    {800, 320},
    {0, 90},
    {800, 320},
    {0, 90},
    {800, 320},
    {0, 260},
    {800, 100},
    {0, 90},
    {800, 100},
    {0, 90},
    {800, 100},
    {0, 260},
};
static const audio_note_t M_NOTIFY[] = {{880, 120}, {1175, 320}};

#define SONG(n, arr, t) {n, arr, (uint16_t)COUNT(arr), t}
static const speaker_song_t CHIPTUNE[] = {
    SONG("Mario", M_MARIO, 100),
    SONG("Tetris", M_TETRIS, 100),
    SONG("Zelda Secret", M_ZELDA, 100),
    SONG("Coin", M_COIN, 100),
    SONG("Power Up", M_POWERUP, 100),
    SONG("Laser", M_LASER, 100),
};
static const speaker_song_t CLASSICAL[] = {
    SONG("Ode to Joy", M_ODE, 100),
    SONG("Fur Elise", M_ELISE, 100),
    SONG("Twinkle", M_TWINKLE, 100),
    SONG("Minuet", M_MINUET, 100),
};
static const speaker_song_t ALERTS[] = {
    SONG("Beep 1kHz", M_BEEP1K, 100),
    SONG("Beep 2kHz", M_BEEP2K, 100),
    SONG("Chime", M_CHIME, 100),
    SONG("Siren", M_SIREN, 100),
    SONG("Alarm", M_ALARM, 100),
    SONG("SOS", M_SOS, 100),
    SONG("Notify", M_NOTIFY, 100),
};
static const speaker_cat_t CATS[] = {
    {"Chiptunes", CHIPTUNE, COUNT(CHIPTUNE)},
    {"Classical", CLASSICAL, COUNT(CLASSICAL)},
    {"Alerts", ALERTS, COUNT(ALERTS)},
};
#define NUM_CATS COUNT(CATS)

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_timer_t *s_nav_timer = NULL;
static int s_category = 0;

static lv_obj_t *s_nowplaying = NULL;
static lv_obj_t *s_eq[N_EQ];
static lv_obj_t *s_progress = NULL;
static lv_obj_t *s_np_title = NULL;
static lv_obj_t *s_np_count = NULL;
static lv_timer_t *s_np_timer = NULL;
static float s_eq_disp[N_EQ];

static const speaker_song_t *s_active = NULL;
static volatile bool s_busy = false;
static volatile bool s_playing = false;
static volatile bool s_cancel = false;
static volatile int s_cur_idx = 0;
static volatile int s_note_count = 1;
static volatile uint16_t s_cur_freq = 0;

static bool s_btn_up_last, s_btn_down_last, s_btn_left_last, s_btn_right_last;
static bool s_btn_ok_last, s_btn_back_last;
static bool s_np_ok_last, s_np_back_last;

static void apply_volume(int level) {
  if (level < 0)
    level = 0;
  if (level > INTENSITY_BAR_STEPS)
    level = INTENSITY_BAR_STEPS;
  audio_i2s_set_volume((uint8_t)(level * 100 / INTENSITY_BAR_STEPS));
}

static int freq_to_band(uint16_t f) {
  const float lo = 200.0f, hi = 3000.0f;
  float ff = (float)f;
  if (ff < lo)
    ff = lo;
  if (ff > hi)
    ff = hi;
  int b = (int)(logf(ff / lo) / logf(hi / lo) * (float)(N_EQ - 1) + 0.5f);
  if (b < 0)
    b = 0;
  if (b >= N_EQ)
    b = N_EQ - 1;
  return b;
}

static bool progress_cb(int i, int n, uint16_t freq, void *ctx) {
  (void)ctx;
  s_cur_idx = i;
  s_note_count = n;
  s_cur_freq = freq;
  return !s_cancel;
}

static void speaker_task(void *arg) {
  (void)arg;
  const speaker_song_t *s = s_active;
  if (s != NULL && s->notes != NULL) {
    int n = s->count;
    if (n > MAX_SEQ)
      n = MAX_SEQ;
    audio_note_t seq[MAX_SEQ];
    uint8_t t = s->tempo_pct ? s->tempo_pct : 100;
    for (int i = 0; i < n; i++) {
      seq[i].freq_hz = s->notes[i].freq_hz;
      uint32_t d = (uint32_t)s->notes[i].dur_ms * t / 100;
      seq[i].dur_ms = (uint16_t)(d > 0 ? d : 1);
    }
    s_note_count = n;
    audio_i2s_play_song_cb(seq, n, SND_AMP, progress_cb, NULL);
  }
  s_cur_freq = 0;
  s_playing = false;
  s_busy = false;
  vTaskDelete(NULL);
}

static lv_obj_t *make_eq_bar(lv_obj_t *parent) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(bar, 1);
  lv_obj_set_height(bar, lv_pct(3));
  lv_obj_set_style_radius(bar, 2, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0xFF5252), 0);
  lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
  return bar;
}

static void np_timer_cb(lv_timer_t *t);
static void nav_timer_cb(lv_timer_t *t);

static void nowplaying_open(const speaker_song_t *song) {
  if (s_nowplaying != NULL) {
    lv_obj_del(s_nowplaying);
    s_nowplaying = NULL;
  }
  for (int i = 0; i < N_EQ; i++)
    s_eq_disp[i] = 0.0f;
  s_np_ok_last = false;
  s_np_back_last = false;

  s_nowplaying = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_nowplaying, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_nowplaying, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_nowplaying, LV_OBJ_FLAG_SCROLLABLE);

  // Transient playback screen: snapshot header (no rebind) so freeing it never
  // dangles the live speaker menu's dynamic header underneath.
  ui_chrome_header_overlay(s_nowplaying, "NOW PLAYING", "/assets/icons/music_note.bin");
  ui_chrome_footer(s_nowplaying, "OK / BACK = stop");

  s_np_title = lv_label_create(s_nowplaying);
  lv_obj_set_width(s_np_title, lv_pct(86));
  lv_label_set_long_mode(s_np_title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
  lv_label_set_text(s_np_title, song->name);
  lv_obj_set_style_text_color(s_np_title, ui_theme_get_accent(), 0);
  lv_obj_set_style_text_font(s_np_title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_np_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_np_title, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H + 8);

  lv_obj_t *eqc = lv_obj_create(s_nowplaying);
  lv_obj_remove_flag(eqc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(eqc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(eqc, 0, 0);
  lv_obj_set_style_pad_all(eqc, 4, 0);
  lv_obj_set_style_pad_column(eqc, 4, 0);
  lv_obj_set_size(eqc, lv_pct(86), lv_pct(40));
  lv_obj_align(eqc, LV_ALIGN_CENTER, 0, -6);
  lv_obj_set_flex_flow(eqc, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(eqc, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  for (int i = 0; i < N_EQ; i++)
    s_eq[i] = make_eq_bar(eqc);

  s_progress = lv_bar_create(s_nowplaying);
  lv_obj_set_size(s_progress, lv_pct(86), 8);
  lv_obj_align(s_progress, LV_ALIGN_CENTER, 0, 60);
  lv_bar_set_range(s_progress, 0, song->count > 0 ? song->count : 1);
  lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);

  s_np_count = lv_label_create(s_nowplaying);
  lv_label_set_text(s_np_count, "0 / 0");
  lv_obj_set_style_text_color(s_np_count, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_np_count, LV_OPA_70, 0);
  lv_obj_set_style_text_font(s_np_count, &lv_font_montserrat_12, 0);
  lv_obj_align(s_np_count, LV_ALIGN_CENTER, 0, 80);

  s_np_timer = lv_timer_create(np_timer_cb, NP_TIMER_MS, NULL);
  ui_screen_load(s_nowplaying);
}

static void np_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_nowplaying) {
    lv_timer_delete(t);
    s_np_timer = NULL;
    if (s_nowplaying != NULL) {
      lv_obj_del(s_nowplaying);
      s_nowplaying = NULL;
    }
    return;
  }
  if (!ui_input_is_locked()) {
    bool ok = ok_button_is_down();
    bool back = back_button_is_down();
    if ((ok && !s_np_ok_last) || (back && !s_np_back_last))
      s_cancel = true;
    s_np_ok_last = ok;
    s_np_back_last = back;
  }

  if (!s_playing) {
    lv_timer_delete(t);
    s_np_timer = NULL;
    if (s_nowplaying != NULL) {
      lv_obj_del(s_nowplaying);
      s_nowplaying = NULL;
    }
    ui_screen_load(s_screen);
    if (s_nav_timer == NULL)
      s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
    return;
  }

  uint16_t f = s_cur_freq;
  int band = (f > 0) ? freq_to_band(f) : -1;
  for (int i = 0; i < N_EQ; i++) {
    float target = 0.0f;
    if (band >= 0) {
      int d = i - band;
      if (d < 0)
        d = -d;
      target = (d == 0) ? 1.0f : (d == 1) ? 0.55f : (d == 2) ? 0.25f : 0.0f;
    }
    if (target > s_eq_disp[i])
      s_eq_disp[i] = target;
    else
      s_eq_disp[i] *= 0.82f;
    int h = (int)(3.0f + s_eq_disp[i] * 94.0f);
    lv_obj_set_height(s_eq[i], lv_pct(h));
  }

  int idx = s_cur_idx, cnt = s_note_count;
  lv_bar_set_value(s_progress, idx + 1, LV_ANIM_OFF);
  lv_label_set_text_fmt(s_np_count, "%d / %d", idx + 1, cnt);
}

static void play_song_now(const speaker_song_t *song) {
  if (song == NULL || s_busy)
    return;
  s_active = song;
  s_busy = true;
  s_cancel = false;
  s_playing = true;
  s_cur_idx = 0;
  s_cur_freq = 0;
  s_note_count = song->count > 0 ? song->count : 1;
  nowplaying_open(song);
  if (xTaskCreatePinnedToCore(speaker_task, "spk_play", SPK_TASK_STACK, NULL, SPK_TASK_PRIORITY, NULL, SYS_CORE_UI) !=
      pdPASS) {
    s_busy = false;
    s_playing = false;
  }
}

static void rebuild_async(void *p) {
  (void)p;
  ui_speaker_open();
  menu_component_select(&s_menu, CATEGORY_ROW);
}

static void cycle_category(int dir) {
  s_category = (s_category + dir + NUM_CATS) % NUM_CATS;
  lv_async_call(rebuild_async, NULL);
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  bool up = ui_btn_up(), down = ui_btn_down();
  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  int sel = menu_component_get_selected(&s_menu);

  if (down && !s_btn_down_last)
    menu_component_next(&s_menu);
  if (up && !s_btn_up_last)
    menu_component_prev(&s_menu);

  if (right && !s_btn_right_last) {
    if (sel == VOLUME_ROW) {
      menu_component_intensity_inc(&s_menu, VOLUME_ROW);
      apply_volume(menu_component_get_intensity(&s_menu, VOLUME_ROW));
    } else if (sel == CATEGORY_ROW) {
      cycle_category(+1);
    } else {
      const speaker_cat_t *c = &CATS[s_category];
      int si = sel - SONG_ROW_BASE;
      if (si >= 0 && si < c->count)
        play_song_now(&c->songs[si]);
    }
  }
  if (left && !s_btn_left_last) {
    if (sel == VOLUME_ROW) {
      menu_component_intensity_dec(&s_menu, VOLUME_ROW);
      apply_volume(menu_component_get_intensity(&s_menu, VOLUME_ROW));
    } else if (sel == CATEGORY_ROW) {
      cycle_category(-1);
    }
  }
  if (ok && !s_btn_ok_last) {
    if (sel == CATEGORY_ROW) {
      cycle_category(+1);
    } else if (sel >= SONG_ROW_BASE) {
      const speaker_cat_t *c = &CATS[s_category];
      int si = sel - SONG_ROW_BASE;
      if (si >= 0 && si < c->count)
        play_song_now(&c->songs[si]);
    }
  }
  if (back && !s_btn_back_last)
    ui_switch_screen(SCREEN_SETTINGS);

  s_btn_up_last = up;
  s_btn_down_last = down;
  s_btn_left_last = left;
  s_btn_right_last = right;
  s_btn_ok_last = ok;
  s_btn_back_last = back;
}

void ui_speaker_open(void) {
  if (s_nav_timer != NULL) {
    lv_timer_delete(s_nav_timer);
    s_nav_timer = NULL;
  }
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_menu = menu_component_create(s_screen, "Speaker", "/assets/icons/speaker.bin");
  menu_component_add_intensity(&s_menu, "/assets/icons/volume_up.bin", "Volume", 3);
  apply_volume(3);
  menu_component_add_selector(
      &s_menu, "/assets/icons/category.bin", "Category", CATS[s_category].name);
  const speaker_cat_t *c = &CATS[s_category];
  int n = c->count > MAX_SONG_ROWS ? MAX_SONG_ROWS : c->count;
  for (int i = 0; i < n; i++)
    menu_component_add_item(&s_menu, "/assets/icons/music_note.bin", c->songs[i].name);

  s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);
  ui_screen_load(s_screen);
  ESP_LOGI(TAG, "speaker menu opened (category=%s)", c->name);
}
