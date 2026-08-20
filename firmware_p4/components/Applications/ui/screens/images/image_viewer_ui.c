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

#include "image_viewer_ui.h"

#include "esp_attr.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "lvgl.h"
#include "st7789.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"

static const char *TAG = "IMG_VIEWER";

#define VIEW_W               ui_screen_w()
#define VIEW_H               ui_screen_h()
#define HDR_H                26
#define FTR_H                24
#define IMG_AREA_H           (VIEW_H - HDR_H - FTR_H)
#define MAX_IMAGES           256
#define PATH_LEN             288
#define FOOTER_TXT_LEN       48
#define LVGL_PATH_PREFIX_MAX 4

typedef enum { IMG_NONE, IMG_JPEG, IMG_PNG, IMG_GIF } img_fmt_t;

static char s_path[PATH_LEN];
static screen_id_t s_return = SCREEN_FILES;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_area = NULL;
static lv_obj_t *s_hdr = NULL;
static lv_obj_t *s_ftr = NULL;

static uint8_t *s_blob = NULL;
static lv_image_dsc_t s_raw_dsc;

static lv_obj_t *s_loading = NULL;
static lv_timer_t *s_decode_timer = NULL;

EXT_RAM_BSS_ATTR static char s_list[MAX_IMAGES][PATH_LEN];
static int s_count = 0;
static int s_idx = 0;

static img_fmt_t fmt_of(const char *name) {
  const char *dot = strrchr(name, '.');
  if (dot == NULL)
    return IMG_NONE;
  if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
    return IMG_JPEG;
  if (strcasecmp(dot, ".png") == 0)
    return IMG_PNG;
  if (strcasecmp(dot, ".gif") == 0)
    return IMG_GIF;
  return IMG_NONE;
}

static bool is_image(const char *name) {
  return fmt_of(name) != IMG_NONE;
}

static void free_media(void) {
  if (s_blob != NULL) {
    free(s_blob);
    s_blob = NULL;
  }
  memset(&s_raw_dsc, 0, sizeof(s_raw_dsc));
}

static uint8_t *read_file(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
  if (buf == NULL) {
    fclose(f);
    ESP_LOGE(TAG, "OOM reading %s (%ld bytes)", path, sz);
    return NULL;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (rd != (size_t)sz) {
    free(buf);
    return NULL;
  }
  *out_len = (size_t)sz;
  return buf;
}

static void scan_folder(void) {
  s_count = 0;
  s_idx = 0;

  char dir[PATH_LEN];
  strncpy(dir, s_path, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = '\0';
  const char *base = s_path;
  char *slash = strrchr(dir, '/');
  if (slash != NULL) {
    *slash = '\0';
    base = slash + 1;
  } else {
    strcpy(dir, ".");
  }

  DIR *d = opendir(dir);
  if (d != NULL) {
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_count < MAX_IMAGES) {
      if (ent->d_name[0] == '.')
        continue;
      if (ent->d_type == DT_DIR)
        continue;
      if (!is_image(ent->d_name))
        continue;
      if (strlen(dir) + 1 + strlen(ent->d_name) >= PATH_LEN)
        continue;
      strlcpy(s_list[s_count], dir, PATH_LEN);
      strlcat(s_list[s_count], "/", PATH_LEN);
      strlcat(s_list[s_count], ent->d_name, PATH_LEN);
      if (strcmp(ent->d_name, base) == 0)
        s_idx = s_count;
      s_count++;
    }
    closedir(d);
  }

  if (s_count == 0) {
    strncpy(s_list[0], s_path, PATH_LEN - 1);
    s_list[0][PATH_LEN - 1] = '\0';
    s_count = 1;
    s_idx = 0;
  }
}

static void fit_image(lv_obj_t *w, int32_t iw, int32_t ih) {
  lv_obj_set_size(w, VIEW_W, IMG_AREA_H);
  lv_image_set_inner_align(w, LV_IMAGE_ALIGN_CENTER);

  bool oversize = (iw > 0 && ih > 0 && (iw > VIEW_W || ih > IMG_AREA_H));
  if (oversize) {
    int32_t sw = (int32_t)((int64_t)VIEW_W * LV_SCALE_NONE / iw);
    int32_t sh = (int32_t)((int64_t)IMG_AREA_H * LV_SCALE_NONE / ih);
    int32_t s = sw < sh ? sw : sh;
    lv_image_set_scale(w, (uint32_t)(s < 1 ? 1 : s));
  } else {
    lv_image_set_scale(w, LV_SCALE_NONE);
  }
  lv_image_set_antialias(w, oversize);
  lv_obj_center(w);
}

static void decode_now(lv_timer_t *t) {
  (void)t;
  s_decode_timer = NULL;
  if (s_area == NULL || s_count <= 0)
    return;

  const char *path = s_list[s_idx];
  const char *name = strrchr(path, '/');
  name = (name != NULL) ? name + 1 : path;

  img_fmt_t fmt = fmt_of(name);
  size_t len = 0;
  lv_obj_t *w = NULL;
  int32_t iw = 0, ih = 0;

  if (fmt == IMG_JPEG) {
    char lp[PATH_LEN + LVGL_PATH_PREFIX_MAX];
    strlcpy(lp, "A:", sizeof(lp));
    strlcat(lp, (path[0] == '/') ? path + 1 : path, sizeof(lp));
    lv_image_header_t h;
    memset(&h, 0, sizeof(h));
    lv_result_t r = lv_image_decoder_get_info(lp, &h);
    ESP_LOGI(TAG,
             "JPEG '%s': src='%s' get_info=%s %dx%d cf=%d",
             name,
             lp,
             r == LV_RESULT_OK ? "OK" : "FAIL",
             (int)h.w,
             (int)h.h,
             (int)h.cf);
    if (r == LV_RESULT_OK) {
      iw = h.w;
      ih = h.h;
    }
    w = lv_image_create(s_area);
    lv_image_set_src(w, lp);
  } else if (fmt == IMG_PNG) {
    s_blob = read_file(path, &len);
    ESP_LOGI(TAG, "PNG '%s': read=%u bytes", name, (unsigned)len);
    if (s_blob != NULL) {
      memset(&s_raw_dsc, 0, sizeof(s_raw_dsc));
      s_raw_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      s_raw_dsc.header.cf = LV_COLOR_FORMAT_RAW;
      s_raw_dsc.data = s_blob;
      s_raw_dsc.data_size = (uint32_t)len;
      lv_image_header_t h;
      memset(&h, 0, sizeof(h));
      lv_result_t r = lv_image_decoder_get_info(&s_raw_dsc, &h);
      ESP_LOGI(TAG,
               "PNG '%s': get_info=%s %dx%d cf=%d",
               name,
               r == LV_RESULT_OK ? "OK" : "FAIL",
               (int)h.w,
               (int)h.h,
               (int)h.cf);
      if (r == LV_RESULT_OK) {
        iw = h.w;
        ih = h.h;
      }
      w = lv_image_create(s_area);
      lv_image_set_src(w, &s_raw_dsc);
    }
  } else if (fmt == IMG_GIF) {
#if LV_USE_GIF
    s_blob = read_file(path, &len);
    if (s_blob != NULL) {
      if (len >= 10) {
        iw = s_blob[6] | (s_blob[7] << 8);
        ih = s_blob[8] | (s_blob[9] << 8);
      }
      ESP_LOGI(TAG, "GIF '%s': %u bytes, %dx%d", name, (unsigned)len, (int)iw, (int)ih);
      memset(&s_raw_dsc, 0, sizeof(s_raw_dsc));
      s_raw_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      s_raw_dsc.header.cf = LV_COLOR_FORMAT_RAW;
      s_raw_dsc.data = s_blob;
      s_raw_dsc.data_size = (uint32_t)len;
      w = lv_gif_create(s_area);
      lv_gif_set_color_format(w, LV_COLOR_FORMAT_RGB565);
      lv_gif_set_src(w, &s_raw_dsc);
    }
#endif
  }

  if (s_loading != NULL) {
    lv_obj_del(s_loading);
    s_loading = NULL;
  }

  if (w == NULL) {
    lv_obj_t *ph = lv_label_create(s_area);
    lv_label_set_text(ph, LV_SYMBOL_WARNING "  cannot display");
    lv_obj_set_style_text_color(ph, lv_color_hex(0xFF5252), 0);
    lv_obj_set_style_text_font(ph, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(ph, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(ph);
    return;
  }

  fit_image(w, iw, ih);
}

static void load_current(void) {
  if (s_decode_timer != NULL) {
    lv_timer_del(s_decode_timer);
    s_decode_timer = NULL;
  }
  if (s_area == NULL)
    return;
  lv_obj_clean(s_area);
  s_loading = NULL;
  free_media();

  if (s_count <= 0)
    return;

  const char *path = s_list[s_idx];
  const char *name = strrchr(path, '/');
  name = (name != NULL) ? name + 1 : path;

  if (s_hdr != NULL)
    lv_label_set_text(s_hdr, name);
  if (s_ftr != NULL) {
    char f[FOOTER_TXT_LEN];
    snprintf(f, sizeof(f), "%d/%d   " LV_SYMBOL_LEFT " " LV_SYMBOL_RIGHT, s_idx + 1, s_count);
    lv_label_set_text(s_ftr, f);
  }

  s_loading = lv_label_create(s_area);
  lv_label_set_text(s_loading, LV_SYMBOL_REFRESH "  Loading...");
  lv_obj_set_style_text_color(s_loading, lv_color_hex(0x8A8594), 0);
  lv_obj_set_style_text_font(s_loading, &lv_font_montserrat_14, 0);
  lv_obj_center(s_loading);

  s_decode_timer = lv_timer_create(decode_now, 40, NULL);
  lv_timer_set_repeat_count(s_decode_timer, 1);
}

static void go_relative(int dir) {
  if (s_count <= 1)
    return;
  s_idx = (s_idx + dir + s_count) % s_count;
  load_current();
  ui_feedback(UI_FB_NAV);
}

static void image_viewer_input(const input_event_t *ev, void *ctx) {
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
    case INPUT_BTN_DOWN:
      go_relative(+1);
      break;
    case INPUT_BTN_LEFT:
    case INPUT_BTN_UP:
      go_relative(-1);
      break;
    default:
      break;
  }
}

void ui_image_viewer_set_path(const char *path) {
  if (path == NULL) {
    s_path[0] = '\0';
    return;
  }
  strncpy(s_path, path, sizeof(s_path) - 1);
  s_path[sizeof(s_path) - 1] = '\0';
}

void ui_image_viewer_set_return(int screen) {
  s_return = (screen_id_t)screen;
}

void ui_image_viewer_stop(void) {
  if (s_decode_timer != NULL) {
    lv_timer_del(s_decode_timer);
    s_decode_timer = NULL;
  }
  if (s_area != NULL)
    lv_obj_clean(s_area);
  free_media();
  s_count = 0;
  s_loading = NULL;
  s_area = NULL;
  s_hdr = NULL;
  s_ftr = NULL;
}

static void on_screen_delete(lv_event_t *e) {
  (void)e;
  if (s_decode_timer != NULL) {
    lv_timer_del(s_decode_timer);
    s_decode_timer = NULL;
  }
  s_loading = NULL;
  s_area = NULL;
  s_hdr = NULL;
  s_ftr = NULL;
  free_media();
}

void ui_image_viewer_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_add_event_cb(s_screen, on_screen_delete, LV_EVENT_DELETE, NULL);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);

  s_hdr = lv_label_create(s_screen);
  lv_obj_set_width(s_hdr, VIEW_W - 16);
  lv_label_set_long_mode(s_hdr, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_hdr, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_hdr, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_hdr, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(s_hdr, LV_ALIGN_TOP_MID, 0, 5);

  s_area = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_area, VIEW_W, IMG_AREA_H);
  lv_obj_align(s_area, LV_ALIGN_TOP_MID, 0, HDR_H);
  lv_obj_set_style_bg_color(s_area, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_area, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_area, 0, 0);
  lv_obj_set_style_border_width(s_area, 0, 0);
  lv_obj_set_style_radius(s_area, 0, 0);

  s_ftr = lv_label_create(s_screen);
  lv_obj_set_style_text_font(s_ftr, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_ftr, lv_color_hex(0x9AA0A6), 0);
  lv_obj_align(s_ftr, LV_ALIGN_BOTTOM_MID, 0, -5);

  ui_input_set_screen_handler(image_viewer_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  scan_folder();
  load_current();
}
