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

#include "wav_player_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "st7789.h"

#include "audio_i2s.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "wav_library_ui.h"

#define REFRESH_TIMER_MS  60
#define N_BARS            12
#define BAR_W             9
#define SPEC_H            76
#define READ_FRAMES       512
#define SEEK_SEC          5
#define VOL_STEP          10
#define VOL_DEFAULT       80
#define PATH_MAX_LEN      256
#define PLAYER_TASK_STACK 8192
#define PLAYER_TASK_PRIO  SYS_PRIO_SERVICE_HI
#define DBLCLICK_MS       350
#define G1                0x7A52D6
#define G2                0xB89AFF
#define OK_GREEN          0x00E676

typedef struct {
  uint16_t fmt;
  uint16_t channels;
  uint32_t rate;
  uint16_t bits;
  long data_off;
  long data_size;
} wav_info_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_bars[N_BARS];
static lv_obj_t *s_pfill = NULL;
static lv_obj_t *s_t_cur = NULL;
static lv_obj_t *s_t_tot = NULL;
static lv_obj_t *s_play_ic = NULL;
static lv_obj_t *s_fname = NULL;
static lv_obj_t *s_fmt = NULL;
static lv_obj_t *s_idx_lbl = NULL;
static lv_obj_t *s_prev_lbl = NULL;
static lv_obj_t *s_next_lbl = NULL;
static lv_timer_t *s_refresh_timer = NULL;

static char s_path[PATH_MAX_LEN];
static int s_index = -1;
static uint32_t s_left_ms = 0;
static uint32_t s_right_ms = 0;
static screen_id_t s_return = SCREEN_FILES;
static TaskHandle_t s_task = NULL;
static volatile bool s_task_run = false;
static volatile bool s_stop_req = false;
static volatile bool s_exit_req = false;
static volatile bool s_playing = false;
static volatile bool s_err = false;
static volatile bool s_finished = false;
static volatile bool s_pending_play = false;
static volatile int s_seek_req = 0;
static volatile int s_pos_sec = 0;
static volatile int s_total_sec = 0;
static volatile int s_level[N_BARS];
static volatile int s_i_rate = 0;
static volatile int s_i_ch = 0;
static volatile int s_i_bits = 0;
static int s_vol = VOL_DEFAULT;
static bool s_vol_dirty = false;

static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool parse_wav(FILE *f, wav_info_t *w) {
  uint8_t hdr[12];
  if (fread(hdr, 1, 12, f) != 12)
    return false;
  if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
    return false;

  bool have_fmt = false, have_data = false;
  while (!(have_fmt && have_data)) {
    uint8_t c[8];
    if (fread(c, 1, 8, f) != 8)
      break;
    uint32_t sz = rd_u32(c + 4);
    if (memcmp(c, "fmt ", 4) == 0) {
      uint8_t fb[16];
      uint32_t rd = sz < 16 ? sz : 16;
      if (fread(fb, 1, rd, f) != rd)
        break;
      w->fmt = rd_u16(fb);
      w->channels = rd_u16(fb + 2);
      w->rate = rd_u32(fb + 4);
      w->bits = rd_u16(fb + 14);
      if (sz > rd)
        fseek(f, sz - rd, SEEK_CUR);
      if (sz & 1)
        fseek(f, 1, SEEK_CUR);
      have_fmt = true;
    } else if (memcmp(c, "data", 4) == 0) {
      w->data_off = ftell(f);
      w->data_size = (long)sz;
      have_data = true;
      break;
    } else {
      fseek(f, (long)sz + (sz & 1), SEEK_CUR);
    }
  }
  return have_fmt && have_data;
}

static void compute_bars(const int16_t *mono, int n) {
  if (n <= 0)
    return;
  for (int b = 0; b < N_BARS; b++) {
    int a = (b * n) / N_BARS;
    int z = ((b + 1) * n) / N_BARS;
    if (z <= a) {
      s_level[b] = 0;
      continue;
    }
    uint64_t sumsq = 0;
    for (int i = a; i < z; i++) {
      int32_t v = mono[i];
      sumsq += (uint64_t)(v * v);
    }
    int rms = (int)sqrt((double)(sumsq / (z - a)));
    int pct = (rms * 130) / 32768;
    if (pct > 100)
      pct = 100;
    s_level[b] = pct;
  }
}

static void exit_to_files_cb(void *p) {
  (void)p;
  ui_switch_screen(s_return);
}

static void player_task(void *arg) {
  (void)arg;
  FILE *f = fopen(s_path, "rb");
  wav_info_t w = {0};
  if (f == NULL || !parse_wav(f, &w) || w.fmt != 1 || w.bits != 16 || w.channels < 1 ||
      w.channels > 2) {
    if (f)
      fclose(f);
    s_err = true;
    s_task_run = false;
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  int ch = w.channels;
  uint32_t rate = w.rate ? w.rate : 44100;
  long bpf = (long)ch * 2;
  long total_frames = w.data_size / bpf;
  s_total_sec = (int)(total_frames / rate);
  s_i_rate = (int)rate;
  s_i_ch = ch;
  s_i_bits = w.bits;

  int16_t *raw = malloc((size_t)READ_FRAMES * bpf);
  int16_t *mono = malloc((size_t)READ_FRAMES * sizeof(int16_t));
  if (raw == NULL || mono == NULL || audio_i2s_stream_start(rate) != ESP_OK) {
    free(raw);
    free(mono);
    fclose(f);
    s_err = true;
    s_task_run = false;
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  fseek(f, w.data_off, SEEK_SET);
  long frame_pos = 0;

  while (!s_stop_req && frame_pos < total_frames) {
    int sk = s_seek_req;
    if (sk != 0) {
      s_seek_req = 0;
      frame_pos += (long)sk * (long)rate;
      if (frame_pos < 0)
        frame_pos = 0;
      if (frame_pos > total_frames)
        frame_pos = total_frames;
      fseek(f, w.data_off + frame_pos * bpf, SEEK_SET);
      s_pos_sec = (int)(frame_pos / rate);
      if (frame_pos >= total_frames)
        break;
    }

    if (!s_playing) {
      memset(mono, 0, 256 * sizeof(int16_t));
      audio_i2s_stream_write(mono, 256);
      continue;
    }

    long want = total_frames - frame_pos;
    if (want > READ_FRAMES)
      want = READ_FRAMES;
    size_t got = fread(raw, (size_t)bpf, (size_t)want, f);
    if (got == 0)
      break;

    for (size_t i = 0; i < got; i++) {
      if (ch == 2) {
        int l = raw[2 * i];
        int r = raw[2 * i + 1];
        mono[i] = (int16_t)((l + r) / 2);
      } else {
        mono[i] = raw[i];
      }
    }
    compute_bars(mono, (int)got);
    audio_i2s_stream_write(mono, (int)got);
    frame_pos += (long)got;
    s_pos_sec = (int)(frame_pos / rate);
  }

  audio_i2s_stream_stop();
  free(raw);
  free(mono);
  fclose(f);

  for (int i = 0; i < N_BARS; i++)
    s_level[i] = 0;
  if (!s_stop_req)
    s_finished = true;
  s_playing = false;
  s_task_run = false;
  s_task = NULL;
  if (s_exit_req) {
    s_exit_req = false;
    ui_async_call(exit_to_files_cb, NULL);
  }
  vTaskDelete(NULL);
}

static void start_playback(void) {
  if (s_task_run) {
    s_stop_req = true;
    s_pending_play = true;
    return;
  }
  s_stop_req = false;
  s_pending_play = false;
  s_finished = false;
  s_err = false;
  s_seek_req = 0;
  s_pos_sec = 0;
  s_playing = true;
  for (int i = 0; i < N_BARS; i++)
    s_level[i] = 0;
  s_task_run = true;
  if (xTaskCreatePinnedToCore(player_task,
                              "wav_play",
                              PLAYER_TASK_STACK,
                              NULL,
                              PLAYER_TASK_PRIO,
                              &s_task,
                              SYS_CORE_UI) != pdPASS) {
    s_task_run = false;
    s_err = true;
  }
}

static int pl_count(void) {
  return (s_index >= 0) ? ui_wav_library_count() : 1;
}

static const char *pl_name(int i) {
  if (s_index >= 0) {
    const char *nm = ui_wav_library_name(i);
    return nm ? nm : "";
  }
  const char *slash = strrchr(s_path, '/');
  return s_path[0] ? (slash ? slash + 1 : s_path) : "";
}

static void refresh_track_labels(void) {
  int n = pl_count();
  int cur = (s_index >= 0) ? s_index : 0;

  if (s_fname) {
    const char *nm = pl_name(cur);
    lv_label_set_text(s_fname, (nm && nm[0]) ? nm : "no file");
  }
  if (s_idx_lbl) {
    if (n > 1)
      lv_label_set_text_fmt(s_idx_lbl, "%d / %d", cur + 1, n);
    else
      lv_label_set_text(s_idx_lbl, "");
  }
  if (s_prev_lbl) {
    if (n > 1)
      lv_label_set_text_fmt(s_prev_lbl, LV_SYMBOL_PREV " %s", pl_name((cur - 1 + n) % n));
    else
      lv_label_set_text(s_prev_lbl, "");
  }
  if (s_next_lbl) {
    if (n > 1)
      lv_label_set_text_fmt(s_next_lbl, LV_SYMBOL_NEXT " %s", pl_name((cur + 1) % n));
    else
      lv_label_set_text(s_next_lbl, "");
  }
}

static void go_relative(int dir) {
  int n = pl_count();
  if (n <= 1 || s_index < 0) {
    refresh_track_labels();
    start_playback();
    return;
  }
  ui_player_play_index(s_index + dir, s_return);
}

static void fmt_time(char *out, size_t n, int sec) {
  if (sec < 0)
    sec = 0;
  snprintf(out, n, "%d:%02d", sec / 60, sec % 60);
}

static void refresh_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_refresh_timer = NULL;
    return;
  }

  if (s_finished) {
    s_finished = false;
    go_relative(+1);
    return;
  }

  if (s_pending_play && !s_task_run) {
    s_pending_play = false;
    start_playback();
    return;
  }

  for (int i = 0; i < N_BARS; i++) {
    if (s_bars[i])
      lv_obj_set_height(s_bars[i], lv_pct(s_level[i] < 3 ? 3 : s_level[i]));
  }
  int tot = s_total_sec > 0 ? s_total_sec : 1;
  int pct = (s_pos_sec * 100) / tot;
  if (pct > 100)
    pct = 100;
  if (s_pfill)
    lv_obj_set_width(s_pfill, lv_pct(pct));
  char b[12];
  fmt_time(b, sizeof(b), s_pos_sec);
  lv_label_set_text(s_t_cur, b);
  fmt_time(b, sizeof(b), s_total_sec);
  lv_label_set_text(s_t_tot, b);
  if (s_play_ic)
    lv_label_set_text(s_play_ic, (s_playing && !s_finished) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  if (s_fmt) {
    if (s_err) {
      lv_label_set_text(s_fmt, "unsupported wav");
    } else if (s_i_rate > 0) {
      lv_label_set_text_fmt(s_fmt,
                            "%d.%dkHz - %d-bit - %s",
                            s_i_rate / 1000,
                            (s_i_rate % 1000) / 100,
                            s_i_bits,
                            s_i_ch == 2 ? "stereo" : "mono");
    }
  }
}

static void wav_player_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(s_return);
      break;
    case INPUT_BTN_OK:
      if (press) {
        if (!s_task_run || s_finished)
          start_playback();
        else
          s_playing = !s_playing;
        ui_feedback(UI_FB_SELECT);
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press) {
        uint32_t now = lv_tick_get();
        if (now - s_right_ms < DBLCLICK_MS) {
          s_seek_req = 0;
          go_relative(+1);
        } else {
          s_seek_req += SEEK_SEC;
        }
        s_right_ms = now;
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_LEFT:
      if (press) {
        uint32_t now = lv_tick_get();
        if (now - s_left_ms < DBLCLICK_MS) {
          s_seek_req = 0;
          go_relative(-1);
        } else {
          s_seek_req -= SEEK_SEC;
        }
        s_left_ms = now;
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_vol = (s_vol > VOL_STEP) ? s_vol - VOL_STEP : 0;
        s_vol_dirty = true;
        audio_i2s_set_volume((uint8_t)s_vol);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_vol = (s_vol + VOL_STEP < 100) ? s_vol + VOL_STEP : 100;
        s_vol_dirty = true;
        audio_i2s_set_volume((uint8_t)s_vol);
        ui_feedback(UI_FB_NAV);
      }
      break;
    default:
      break;
  }
}

static lv_obj_t *transport_btn(lv_obj_t *parent, const char *sym, bool primary) {
  lv_obj_t *b = lv_obj_create(parent);
  int d = primary ? 42 : 30;
  lv_obj_set_size(b, d, d);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  if (primary) {
    lv_obj_set_style_bg_color(b, lv_color_hex(G2), 0);
    lv_obj_set_style_bg_grad_color(b, lv_color_hex(G1), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  } else {
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
  }
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, sym);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, primary ? lv_color_hex(0x0A0220) : current_theme.text_main, 0);
  lv_obj_center(l);
  return l;
}

void ui_wav_player_set_return(int screen) {
  s_return = (screen_id_t)screen;
}

void ui_wav_player_set_path(const char *path) {
  s_index = -1;
  if (path == NULL) {
    s_path[0] = '\0';
    return;
  }
  strncpy(s_path, path, sizeof(s_path) - 1);
  s_path[sizeof(s_path) - 1] = '\0';
}

void ui_wav_player_set_index(int index) {
  s_index = index;
}

void ui_wav_player_stop(void) {
  s_pending_play = false;
  if (s_task_run) {
    s_stop_req = true;
    for (int i = 0; i < 80 && s_task_run; i++)
      vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (s_vol_dirty) {
    g_config_system.volume = s_vol;
    tos_config_save(TOS_PATH_CONFIG_SYSTEM, "system");
    s_vol_dirty = false;
  }
}

void ui_wav_player_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_left_ms = s_right_ms = 0;
  s_pending_play = false;

  if (s_index >= 0) {
    const char *p = ui_wav_library_path(s_index);
    if (p != NULL) {
      strncpy(s_path, p, sizeof(s_path) - 1);
      s_path[sizeof(s_path) - 1] = '\0';
    } else {
      s_index = -1;
    }
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "PLAYING", "/assets/icons/music_note.bin");
  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " seek  x2 skip   OK play");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, 216, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(body, 4, 0);

  lv_obj_t *spec = lv_obj_create(body);
  lv_obj_remove_style_all(spec);
  lv_obj_set_size(spec, 200, SPEC_H);
  lv_obj_remove_flag(spec, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(spec, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(spec, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  for (int i = 0; i < N_BARS; i++) {
    lv_obj_t *bar = lv_obj_create(spec);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(bar, BAR_W);
    lv_obj_set_height(bar, lv_pct(3));
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(G1), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(G2), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    s_bars[i] = bar;
  }

  s_idx_lbl = lv_label_create(body);
  lv_label_set_text(s_idx_lbl, "");
  lv_obj_set_style_text_font(s_idx_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_idx_lbl, current_theme.border_accent, 0);

  s_fname = lv_label_create(body);
  const char *slash = strrchr(s_path, '/');
  lv_label_set_text(s_fname, s_path[0] ? (slash ? slash + 1 : s_path) : "no file");
  lv_label_set_long_mode(s_fname, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_fname, 200);
  lv_obj_set_style_text_align(s_fname, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_fname, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_fname, current_theme.text_main, 0);

  s_fmt = lv_label_create(body);
  lv_label_set_text(s_fmt, "WAV - PCM");
  lv_obj_set_style_text_font(s_fmt, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_fmt, lv_color_hex(0x00E5D0), 0);

  lv_obj_t *prog = lv_obj_create(body);
  lv_obj_remove_style_all(prog);
  lv_obj_set_size(prog, 200, LV_SIZE_CONTENT);
  lv_obj_remove_flag(prog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(prog, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(prog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(prog, 4, 0);

  lv_obj_t *track = lv_obj_create(prog);
  lv_obj_remove_style_all(track);
  lv_obj_set_size(track, 200, 5);
  lv_obj_set_style_radius(track, 3, 0);
  lv_obj_set_style_bg_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

  s_pfill = lv_obj_create(track);
  lv_obj_remove_style_all(s_pfill);
  lv_obj_set_height(s_pfill, lv_pct(100));
  lv_obj_set_width(s_pfill, lv_pct(0));
  lv_obj_align(s_pfill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_radius(s_pfill, 3, 0);
  lv_obj_set_style_bg_color(s_pfill, lv_color_hex(G1), 0);
  lv_obj_set_style_bg_grad_color(s_pfill, lv_color_hex(G2), 0);
  lv_obj_set_style_bg_grad_dir(s_pfill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(s_pfill, LV_OPA_COVER, 0);

  lv_obj_t *trow = lv_obj_create(prog);
  lv_obj_remove_style_all(trow);
  lv_obj_set_size(trow, 200, LV_SIZE_CONTENT);
  lv_obj_remove_flag(trow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(trow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      trow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  s_t_cur = lv_label_create(trow);
  lv_label_set_text(s_t_cur, "0:00");
  lv_obj_set_style_text_font(s_t_cur, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_t_cur, current_theme.text_main, 0);

  s_t_tot = lv_label_create(trow);
  lv_label_set_text(s_t_tot, "0:00");
  lv_obj_set_style_text_font(s_t_tot, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_t_tot, current_theme.text_main, 0);

  lv_obj_t *ctrls = lv_obj_create(body);
  lv_obj_remove_style_all(ctrls);
  lv_obj_set_size(ctrls, 180, 46);
  lv_obj_remove_flag(ctrls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(ctrls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ctrls, 18, 0);

  transport_btn(ctrls, LV_SYMBOL_PREV, false);
  s_play_ic = transport_btn(ctrls, LV_SYMBOL_PAUSE, true);
  transport_btn(ctrls, LV_SYMBOL_NEXT, false);

  lv_obj_t *queue = lv_obj_create(body);
  lv_obj_remove_style_all(queue);
  lv_obj_set_size(queue, 210, LV_SIZE_CONTENT);
  lv_obj_remove_flag(queue, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(queue, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      queue, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(queue, 8, 0);

  s_prev_lbl = lv_label_create(queue);
  lv_label_set_text(s_prev_lbl, "");
  lv_label_set_long_mode(s_prev_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(s_prev_lbl, 98);
  lv_obj_set_style_text_font(s_prev_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_prev_lbl, current_theme.border_inactive, 0);

  s_next_lbl = lv_label_create(queue);
  lv_label_set_text(s_next_lbl, "");
  lv_label_set_long_mode(s_next_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_next_lbl, 98);
  lv_obj_set_style_text_align(s_next_lbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(s_next_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_next_lbl, current_theme.border_inactive, 0);

  refresh_track_labels();

  s_vol = g_config_system.volume;
  s_vol_dirty = false;
  audio_i2s_set_volume((uint8_t)s_vol);
  start_playback();

  if (s_refresh_timer == NULL)
    s_refresh_timer = lv_timer_create(refresh_timer_cb, REFRESH_TIMER_MS, NULL);
  ui_input_set_screen_handler(wav_player_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
