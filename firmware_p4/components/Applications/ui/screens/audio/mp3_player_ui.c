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

#ifndef MP3_AUDIO_DECODE
#define MP3_AUDIO_DECODE 1
#endif

#include "mp3_player_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "esp_log.h"
#include "st7789.h"

#include "audio_i2s.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "id3_meta.h"
#include "media_thumb.h"
#include "wav_library_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#if MP3_AUDIO_DECODE
#include "mp3dec.h"
#endif

#define PATH_MAX_LEN     256
#define REFRESH_TIMER_MS 60
#define N_BARS           12
#define BAR_W            9
#define SPEC_H           58
#define COVER_BOX        118
#define READBUF_SZ       4096
#define MP3_MAXFRAME     2304
#define VOL_STEP         10
#define VOL_DEFAULT      80
#define ACC1             0x7A52D6
#define ACC2             0xB89AFF

#define PLAYER_TASK_STACK 12288
#define PLAYER_TASK_PRIO  SYS_PRIO_SERVICE_HI

#define MP3_DEFAULT_RATE_HZ 44100
#define MP3_SILENCE_SAMPLES 256
#define MP3_STOP_WAIT_ITERS 80
#define MP3_STOP_POLL_MS    10

static const char *TAG = "MP3_PLAYER";

static char s_path[PATH_MAX_LEN];
static screen_id_t s_return = SCREEN_FILES;
static int s_index = -1;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_bars[N_BARS];
static lv_obj_t *s_pfill = NULL;
static lv_obj_t *s_t_cur = NULL;
static lv_obj_t *s_t_tot = NULL;
static lv_obj_t *s_play_ic = NULL;
static lv_obj_t *s_fmt = NULL;
static lv_timer_t *s_refresh_timer = NULL;

static media_thumb_t s_thumb;
static bool s_thumb_valid = false;
static uint8_t *s_png_blob = NULL;
static lv_image_dsc_t s_png_dsc;
static bool s_png_valid = false;
static id3_meta_t s_meta;

static TaskHandle_t s_task = NULL;
static volatile bool s_task_run = false;
static volatile bool s_stop_req = false;
static volatile bool s_exit_req = false;
static volatile bool s_playing = false;
static volatile bool s_err = false;
static volatile bool s_finished = false;
static volatile int s_pos_sec = 0;
static volatile int s_total_sec = 0;
static volatile int s_level[N_BARS];
static int s_vol = VOL_DEFAULT;
static bool s_vol_dirty = false;

static void free_thumb(void) {
  if (s_thumb_valid) {
    media_thumb_free(&s_thumb);
    s_thumb_valid = false;
  }
  if (s_png_blob != NULL) {
    free(s_png_blob);
    s_png_blob = NULL;
  }
  s_png_valid = false;
}

static void load_cover(void) {
  free_thumb();
  ESP_LOGI(TAG,
           "ID3 cover fmt=%d size=%u (1=JPEG 2=PNG 0=none)",
           (int)s_meta.cover_fmt,
           (unsigned)s_meta.cover_size);
  if (s_meta.cover_fmt == ID3_COVER_NONE || s_meta.cover_size == 0)
    return;
  uint32_t blob_len = 0;
  uint8_t *blob = id3_meta_read_cover(s_path, &s_meta, &blob_len);
  if (blob == NULL)
    return;
  if (s_meta.cover_fmt == ID3_COVER_JPEG) {
    s_thumb_valid = media_thumb_decode_jpeg(blob, blob_len, &s_thumb);
    ESP_LOGI(TAG, "JPEG cover decode %s", s_thumb_valid ? "OK" : "FAILED");
    free(blob);
  } else {
    s_png_blob = blob;
    memset(&s_png_dsc, 0, sizeof(s_png_dsc));
    s_png_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_png_dsc.header.cf = LV_COLOR_FORMAT_RAW;
    s_png_dsc.data = s_png_blob;
    s_png_dsc.data_size = blob_len;
    s_png_valid = true;
    ESP_LOGI(TAG, "PNG cover queued (%u bytes)", (unsigned)blob_len);
  }
}

static void fmt_time(char *out, size_t n, int sec) {
  if (sec < 0)
    sec = 0;
  snprintf(out, n, "%d:%02d", sec / 60, sec % 60);
}

#if MP3_AUDIO_DECODE
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
#endif

static void exit_cb(void *p) {
  (void)p;
  ui_switch_screen(s_return);
}

static void player_task(void *arg) {
  (void)arg;
#if MP3_AUDIO_DECODE
  FILE *f = fopen(s_path, "rb");
  HMP3Decoder dec = NULL;
  uint8_t *readbuf = malloc(READBUF_SZ);
  int16_t *pcm = malloc(MP3_MAXFRAME * sizeof(int16_t));
  int16_t *mono = malloc((MP3_MAXFRAME / 2 + 4) * sizeof(int16_t));
  if (f != NULL)
    dec = MP3InitDecoder();
  if (f == NULL || dec == NULL || readbuf == NULL || pcm == NULL || mono == NULL) {
    if (dec)
      MP3FreeDecoder(dec);
    free(readbuf);
    free(pcm);
    free(mono);
    if (f)
      fclose(f);
    s_err = true;
    s_task_run = false;
    s_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  long audio_off = s_meta.audio_off > 0 ? s_meta.audio_off : 0;
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, audio_off, SEEK_SET);

  uint8_t *rp = readbuf;
  int left = 0;
  bool started = false;
  bool eof = false;
  uint32_t rate = MP3_DEFAULT_RATE_HZ;
  long samples_played = 0;

  while (!s_stop_req) {
    if (!s_playing) {
      if (started) {
        memset(mono, 0, MP3_SILENCE_SAMPLES * sizeof(int16_t));
        audio_i2s_stream_write(mono, MP3_SILENCE_SAMPLES);
      } else {
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      continue;
    }

    if (left < 2 * 512) {
      if (rp != readbuf && left > 0)
        memmove(readbuf, rp, left);
      rp = readbuf;
      size_t got = fread(readbuf + left, 1, READBUF_SZ - left, f);
      left += (int)got;
      if (got == 0)
        eof = true;
      if (eof && left < 4)
        break;
    }

    int off = MP3FindSyncWord(rp, left);
    if (off < 0) {
      if (eof)
        break;
      left = 0;
      continue;
    }
    rp += off;
    left -= off;

    int err = MP3Decode(dec, &rp, &left, pcm, 0);
    if (err != 0) {
      if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
        if (eof)
          break;
        if (left > 0) {
          memmove(readbuf, rp, left);
          rp = readbuf;
        }
        continue;
      }
      if (left > 0) {
        rp++;
        left--;
      }
      continue;
    }

    MP3FrameInfo fi;
    MP3GetLastFrameInfo(dec, &fi);
    if (fi.samprate <= 0 || fi.nChans <= 0 || fi.outputSamps <= 0)
      continue;

    if (!started) {
      rate = (uint32_t)fi.samprate;
      if (audio_i2s_stream_start(rate) != ESP_OK) {
        s_err = true;
        break;
      }
      started = true;
      if (fi.bitrate > 0)
        s_total_sec = (int)(((int64_t)(fsize - audio_off) * 8) / fi.bitrate);
    }

    int frames = fi.outputSamps / fi.nChans;
    if (frames > MP3_MAXFRAME / 2)
      frames = MP3_MAXFRAME / 2;
    if (fi.nChans == 2) {
      for (int i = 0; i < frames; i++) {
        int l = pcm[2 * i];
        int r = pcm[2 * i + 1];
        mono[i] = (int16_t)((l + r) / 2);
      }
    } else {
      for (int i = 0; i < frames; i++)
        mono[i] = pcm[i];
    }
    compute_bars(mono, frames);
    audio_i2s_stream_write(mono, frames);
    samples_played += frames;
    s_pos_sec = (int)(samples_played / (rate > 0 ? rate : MP3_DEFAULT_RATE_HZ));
  }

  if (started)
    audio_i2s_stream_stop();
  MP3FreeDecoder(dec);
  free(readbuf);
  free(pcm);
  free(mono);
  fclose(f);
#else
  s_err = true;
#endif

  for (int i = 0; i < N_BARS; i++)
    s_level[i] = 0;
  if (!s_stop_req)
    s_finished = true;
  s_playing = false;
  s_task_run = false;
  s_task = NULL;
  if (s_exit_req) {
    s_exit_req = false;
    ui_async_call(exit_cb, NULL);
  }
  vTaskDelete(NULL);
}

static void start_playback(void) {
  if (s_task_run)
    return;
  s_stop_req = false;
  s_finished = false;
  s_err = false;
  s_pos_sec = 0;
  s_playing = true;
  for (int i = 0; i < N_BARS; i++)
    s_level[i] = 0;
  s_task_run = true;
  if (xTaskCreatePinnedToCore(player_task,
                              "mp3_play",
                              PLAYER_TASK_STACK,
                              NULL,
                              PLAYER_TASK_PRIO,
                              &s_task,
                              SYS_CORE_UI) != pdPASS) {
    s_task_run = false;
    s_err = true;
  }
}

static void mp3_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS && ev->action != INPUT_ACTION_REPEAT)
    return;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(s_return);
      break;
    case INPUT_BTN_RIGHT:
      if (press && s_index >= 0)
        ui_player_play_index(s_index + 1, s_return);
      break;
    case INPUT_BTN_LEFT:
      if (press && s_index >= 0)
        ui_player_play_index(s_index - 1, s_return);
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
    case INPUT_BTN_DOWN:
      s_vol = (s_vol > VOL_STEP) ? s_vol - VOL_STEP : 0;
      s_vol_dirty = true;
      audio_i2s_set_volume((uint8_t)s_vol);
      ui_feedback(UI_FB_NAV);
      break;
    case INPUT_BTN_UP:
      s_vol = (s_vol + VOL_STEP < 100) ? s_vol + VOL_STEP : 100;
      s_vol_dirty = true;
      audio_i2s_set_volume((uint8_t)s_vol);
      ui_feedback(UI_FB_NAV);
      break;
    default:
      break;
  }
}

static void refresh_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_refresh_timer = NULL;
    return;
  }
  if (s_finished) {
    s_finished = false;
    if (s_index >= 0)
      ui_player_play_index(s_index + 1, s_return);
    else
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
  if (s_fmt && s_err) {
    lv_label_set_text(s_fmt, MP3_AUDIO_DECODE ? "decode error" : "playback: enable esp-audio-libs");
    lv_obj_set_style_text_color(s_fmt, lv_color_hex(0xFF5252), 0);
  }
}

void ui_mp3_player_set_path(const char *path) {
  s_index = -1;
  if (path == NULL) {
    s_path[0] = '\0';
    return;
  }
  strncpy(s_path, path, sizeof(s_path) - 1);
  s_path[sizeof(s_path) - 1] = '\0';
}

void ui_mp3_player_set_return(int screen) {
  s_return = (screen_id_t)screen;
}

void ui_mp3_player_set_index(int index) {
  s_index = index;
}

void ui_mp3_player_stop(void) {
  if (s_task_run) {
    s_stop_req = true;
    for (int i = 0; i < MP3_STOP_WAIT_ITERS && s_task_run; i++)
      vTaskDelay(pdMS_TO_TICKS(MP3_STOP_POLL_MS));
  }
  if (s_vol_dirty) {
    g_config_system.volume = s_vol;
    tos_config_save(TOS_PATH_CONFIG_SYSTEM, "system");
    s_vol_dirty = false;
  }
}

static lv_obj_t *
mk_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t col, int width) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col, 0);
  if (width > 0) {
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  return l;
}

static lv_obj_t *transport_btn(lv_obj_t *parent, const char *sym, bool primary) {
  lv_obj_t *b = lv_obj_create(parent);
  int d = primary ? 46 : 32;
  lv_obj_set_size(b, d, d);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  if (primary) {
    lv_obj_set_style_bg_color(b, lv_color_hex(ACC2), 0);
    lv_obj_set_style_bg_grad_color(b, lv_color_hex(ACC1), 0);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(b, 14, 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(ACC1), 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);
  } else {
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
  }
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, sym);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(
      l, primary ? lv_color_hex(0x0A0220) : current_theme.text_secondary, 0);
  lv_obj_center(l);
  return l;
}

void ui_mp3_player_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  if (s_index >= 0) {
    const char *p = ui_wav_library_path(s_index);
    if (p != NULL) {
      strncpy(s_path, p, sizeof(s_path) - 1);
      s_path[sizeof(s_path) - 1] = '\0';
      ui_wav_library_set_selected(s_index);
    } else {
      s_index = -1;
    }
  }

  memset(&s_meta, 0, sizeof(s_meta));
  id3_meta_parse(s_path, &s_meta);
  load_cover();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "PLAYING", "/assets/icons/music_note.bin");
  ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " vol   OK play   BACK exit");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, 224, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_hor(body, 6, 0);
  lv_obj_set_style_pad_ver(body, 6, 0);

  lv_obj_t *cover = lv_obj_create(body);
  lv_obj_remove_style_all(cover);
  lv_obj_set_size(cover, COVER_BOX, COVER_BOX);
  lv_obj_remove_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(cover, 14, 0);
  lv_obj_set_style_clip_corner(cover, true, 0);
  lv_obj_set_style_bg_color(cover, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(cover, 1, 0);
  lv_obj_set_style_border_color(cover, current_theme.border_inactive, 0);
  lv_obj_set_style_shadow_width(cover, 26, 0);
  lv_obj_set_style_shadow_color(cover, lv_color_hex(ACC1), 0);
  lv_obj_set_style_shadow_opa(cover, LV_OPA_40, 0);
  const lv_image_dsc_t *cover_src = NULL;
  int cw = 0, ch = 0;
  if (s_thumb_valid && s_thumb.w > 0 && s_thumb.h > 0) {
    cover_src = &s_thumb.dsc;
    cw = s_thumb.w;
    ch = s_thumb.h;
  } else if (s_png_valid) {
    cover_src = &s_png_dsc;
    lv_image_header_t hdr = {0};
    if (lv_image_decoder_get_info(&s_png_dsc, &hdr) == LV_RESULT_OK) {
      cw = hdr.w;
      ch = hdr.h;
    }
  }
  if (cover_src != NULL) {
    lv_obj_t *img = lv_image_create(cover);
    lv_image_set_src(img, cover_src);
    lv_obj_center(img);
    int longest = cw > ch ? cw : ch;
    if (longest > 0) {
      int zoom = (COVER_BOX * 256) / longest;
      if (zoom < 1)
        zoom = 1;
      lv_image_set_scale(img, (uint16_t)zoom);
    }
  } else {
    lv_obj_t *ph = lv_label_create(cover);
    lv_label_set_text(ph, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(ph, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ph, current_theme.text_secondary, 0);
    lv_obj_center(ph);
  }

  const char *slash = strrchr(s_path, '/');
  const char *fallback = s_path[0] ? (slash ? slash + 1 : s_path) : "no file";
  lv_obj_t *fname = lv_label_create(body);
  lv_label_set_text(fname, s_meta.title[0] ? s_meta.title : fallback);
  lv_label_set_long_mode(fname, LV_LABEL_LONG_DOT);
  lv_obj_set_width(fname, 204);
  lv_obj_set_style_text_align(fname, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(fname, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(fname, current_theme.text_main, 0);

  char info[128] = "";
  if (s_meta.artist[0])
    snprintf(info, sizeof(info), "%s", s_meta.artist);
  if (s_meta.album[0]) {
    size_t k = strlen(info);
    snprintf(info + k, sizeof(info) - k, "%s%s", info[0] ? "  |  " : "", s_meta.album);
  }
  if (s_meta.year[0]) {
    size_t k = strlen(info);
    snprintf(info + k, sizeof(info) - k, "%s%s", info[0] ? "  |  " : "", s_meta.year);
  }
  s_fmt = lv_label_create(body);
  lv_label_set_text(s_fmt, info[0] ? info : "MP3");
  lv_label_set_long_mode(s_fmt, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_fmt, 204);
  lv_obj_set_style_text_align(s_fmt, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_fmt, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_fmt, lv_color_hex(0x00E5D0), 0);

  lv_obj_t *prog = lv_obj_create(body);
  lv_obj_remove_style_all(prog);
  lv_obj_set_size(prog, 200, LV_SIZE_CONTENT);
  lv_obj_remove_flag(prog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(prog, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(prog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(prog, 3, 0);
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
  lv_obj_set_style_bg_color(s_pfill, lv_color_hex(ACC1), 0);
  lv_obj_set_style_bg_grad_color(s_pfill, lv_color_hex(ACC2), 0);
  lv_obj_set_style_bg_grad_dir(s_pfill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(s_pfill, LV_OPA_COVER, 0);
  lv_obj_t *trow = lv_obj_create(prog);
  lv_obj_remove_style_all(trow);
  lv_obj_set_size(trow, 200, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(trow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      trow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  s_t_cur = mk_label(trow, "0:00", &lv_font_montserrat_12, current_theme.text_main, 0);
  s_t_tot = mk_label(trow, "0:00", &lv_font_montserrat_12, current_theme.text_main, 0);

  lv_obj_t *ctrls = lv_obj_create(body);
  lv_obj_remove_style_all(ctrls);
  lv_obj_set_size(ctrls, 180, 50);
  lv_obj_remove_flag(ctrls, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(ctrls, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ctrls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ctrls, 20, 0);
  transport_btn(ctrls, LV_SYMBOL_PREV, false);
  s_play_ic = transport_btn(ctrls, LV_SYMBOL_PAUSE, true);
  transport_btn(ctrls, LV_SYMBOL_NEXT, false);

  s_vol = g_config_system.volume;
  s_vol_dirty = false;
  audio_i2s_set_volume((uint8_t)s_vol);
  ui_input_set_screen_handler(mp3_input, NULL);
  if (s_refresh_timer == NULL)
    s_refresh_timer = lv_timer_create(refresh_cb, REFRESH_TIMER_MS, NULL);

  start_playback();
  ui_screen_load_owned(&s_screen, s_screen);
}
