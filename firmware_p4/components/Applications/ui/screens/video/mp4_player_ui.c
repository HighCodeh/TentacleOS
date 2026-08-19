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

#include "mp4_player_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "st7789.h"

#include "media_thumb.h"
#include "mp4_meta.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

static const char *TAG = "MP4_PLAYER";

#define PATH_MAX_LEN     256
#define REFRESH_TIMER_MS 120
#define COVER_BOX        118
#define META_LINE_LEN    96
#define DUR_BUF_LEN      16
#define ACC1             0x7A52D6
#define ACC2             0xB89AFF

static char s_path[PATH_MAX_LEN];
static screen_id_t s_return = SCREEN_FILES;

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static lv_obj_t *s_status = NULL;
static media_thumb_t s_thumb;
static bool s_thumb_valid = false;
static uint8_t *s_png_blob = NULL;
static lv_image_dsc_t s_png_dsc;
static bool s_png_valid = false;
static mp4_meta_t s_meta;

void ui_mp4_player_set_path(const char *path) {
  if (path == NULL) {
    s_path[0] = '\0';
    return;
  }
  strncpy(s_path, path, sizeof(s_path) - 1);
  s_path[sizeof(s_path) - 1] = '\0';
}

void ui_mp4_player_set_return(int screen) {
  s_return = (screen_id_t)screen;
}

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
  if (s_meta.cover_fmt == MP4_COVER_NONE || s_meta.cover_size == 0)
    return;
  uint32_t blob_len = 0;
  uint8_t *blob = mp4_meta_read_cover(s_path, &s_meta, &blob_len);
  if (blob == NULL)
    return;
  if (s_meta.cover_fmt == MP4_COVER_JPEG) {
    s_thumb_valid = media_thumb_decode_jpeg(blob, blob_len, &s_thumb);
    free(blob);
  } else {
    s_png_blob = blob;
    memset(&s_png_dsc, 0, sizeof(s_png_dsc));
    s_png_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_png_dsc.header.cf = LV_COLOR_FORMAT_RAW;
    s_png_dsc.data = s_png_blob;
    s_png_dsc.data_size = blob_len;
    s_png_valid = true;
  }
}

static void fmt_duration(char *out, size_t n, uint32_t sec) {
  if (sec >= 3600)
    snprintf(out, n, "%u:%02u:%02u", (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60),
             (unsigned)(sec % 60));
  else
    snprintf(out, n, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}

static void mp4_player_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS)
    return;
  switch (ev->button) {
    case INPUT_BTN_BACK:
      ui_switch_screen(s_return);
      break;
    case INPUT_BTN_OK:
      ui_feedback(UI_FB_SELECT);
      if (s_status != NULL) {
        if (s_meta.vcodec == MP4_VCODEC_H264)
          lv_label_set_text(s_status, "H.264 video can't play on\nthis chip - transcode to MJPEG");
        else if (s_meta.vcodec == MP4_VCODEC_MJPEG)
          lv_label_set_text(s_status, "MJPEG playback pipeline\nis the next build stage");
        else
          lv_label_set_text(s_status, "Audio decode is the next\nbuild stage");
      }
      break;
    default:
      ui_feedback(UI_FB_NAV);
      break;
  }
}

static void refresh_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_refresh_timer = NULL;
  }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font, lv_color_t col,
                            int width) {
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

void ui_mp4_player_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  bool parsed = mp4_meta_parse(s_path, &s_meta);
  if (!parsed)
    memset(&s_meta, 0, sizeof(s_meta));
  load_cover();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "VIDEO", "/assets/icons/description.bin");
  ui_chrome_footer(s_screen, "OK info   BACK exit");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_style_all(body);
  lv_obj_set_size(body, 224, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(body, 4, 0);

  lv_obj_t *cover = lv_obj_create(body);
  lv_obj_remove_style_all(cover);
  lv_obj_set_size(cover, COVER_BOX, COVER_BOX);
  lv_obj_remove_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(cover, 10, 0);
  lv_obj_set_style_clip_corner(cover, true, 0);
  lv_obj_set_style_bg_color(cover, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(cover, 1, 0);
  lv_obj_set_style_border_color(cover, current_theme.border_inactive, 0);
  lv_obj_set_style_shadow_width(cover, 12, 0);
  lv_obj_set_style_shadow_color(cover, lv_color_hex(ACC1), 0);
  lv_obj_set_style_shadow_opa(cover, LV_OPA_30, 0);

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
    lv_label_set_text(ph, LV_SYMBOL_VIDEO);
    lv_obj_set_style_text_font(ph, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ph, current_theme.text_secondary, 0);
    lv_obj_center(ph);
  }

  const char *slash = strrchr(s_path, '/');
  const char *fallback = s_path[0] ? (slash ? slash + 1 : s_path) : "no file";
  const char *title = s_meta.title[0] ? s_meta.title : fallback;
  make_label(body, title, &lv_font_montserrat_14, current_theme.text_main, 210);

  if (s_meta.artist[0])
    make_label(body, s_meta.artist, &lv_font_montserrat_12, current_theme.border_accent, 210);

  if (s_meta.album[0] || s_meta.year[0]) {
    char line[META_LINE_LEN];
    if (s_meta.album[0] && s_meta.year[0])
      snprintf(line, sizeof(line), "%s  |  %s", s_meta.album, s_meta.year);
    else
      snprintf(line, sizeof(line), "%s", s_meta.album[0] ? s_meta.album : s_meta.year);
    make_label(body, line, &lv_font_montserrat_12, current_theme.text_secondary, 210);
  }

  char meta_line[META_LINE_LEN];
  char dur[DUR_BUF_LEN] = "";
  if (s_meta.duration_sec > 0)
    fmt_duration(dur, sizeof(dur), s_meta.duration_sec);
  if (s_meta.width > 0 && s_meta.height > 0)
    snprintf(meta_line, sizeof(meta_line), "%s  |  %ux%u%s%s", mp4_vcodec_name(s_meta.vcodec),
             (unsigned)s_meta.width, (unsigned)s_meta.height, dur[0] ? "  |  " : "", dur);
  else
    snprintf(meta_line, sizeof(meta_line), "%s%s%s", mp4_vcodec_name(s_meta.vcodec),
             dur[0] ? "  |  " : "", dur);
  make_label(body, meta_line, &lv_font_montserrat_12, lv_color_hex(0x00E5D0), 210);

  s_status = lv_label_create(body);
  lv_obj_set_width(s_status, 210);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
  if (!parsed)
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFF5252), 0);
  else
    lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);

  if (!parsed)
    lv_label_set_text(s_status, "Not a readable MP4");
  else if (s_meta.vcodec == MP4_VCODEC_H264)
    lv_label_set_text(s_status, "H.264 - transcode to MJPEG to play");
  else if (s_meta.vcodec == MP4_VCODEC_MJPEG)
    lv_label_set_text(s_status, "MJPEG - playback: next build stage");
  else if (s_meta.has_audio)
    lv_label_set_text(s_status, "Audio - playback: next build stage");
  else
    lv_label_set_text(s_status, "Metadata only");

  ui_input_set_screen_handler(mp4_player_input, NULL);
  if (s_refresh_timer == NULL)
    s_refresh_timer = lv_timer_create(refresh_timer_cb, REFRESH_TIMER_MS, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
