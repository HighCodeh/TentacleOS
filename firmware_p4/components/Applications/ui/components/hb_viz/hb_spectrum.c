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

#include "hb_spectrum.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

#include "hb_signal.h"
#include "ui_theme.h"

// --- tunables -------------------------------------------------------------

#define HB_SPEC_MAX_APS   64
#define HB_SPEC_LABEL_LEN 18

#define PAD_TOP    4
#define PAD_BOTTOM 16 // room for the channel numbers under the baseline
#define PAD_SIDE   6

#define TENT_FILL_OPA LV_OPA_40
#define RSSI_FLOOR    (-100)
#define RSSI_CEIL     (-30)

// --- internal state -------------------------------------------------------

typedef struct {
  uint16_t channel;
  int16_t rssi;
  uint8_t width_mhz;
  char label[HB_SPEC_LABEL_LEN];
} hb_spec_ap_t;

typedef struct {
  lv_obj_t *canvas;
  void *buf;
  int32_t w;
  int32_t h;
  hb_band_t band;
  int count;
  hb_spec_ap_t aps[HB_SPEC_MAX_APS];
} hb_spec_ctx_t;

// Channels that carry a printed number on the 2.4 GHz axis (the non-overlapping
// trio operators actually care about); every channel still gets a faint tick.
static const uint8_t CH24_LABELS[] = {1, 6, 11};
static const char *const CH24_TEXT[] = {"1", "6", "11"};
// A readable subset of the 5 GHz channels - one per ~4 to avoid crowding.
static const uint8_t CH5_LABELS[] = {36, 44, 52, 60, 100, 116, 132, 149, 165};
static const char *const CH5_TEXT[] = {"36", "44", "52", "60", "100", "116", "132", "149", "165"};

// --- helpers --------------------------------------------------------------

static hb_spec_ctx_t *ctx_of(lv_obj_t *spectrum) {
  return spectrum ? (hb_spec_ctx_t *)lv_obj_get_user_data(spectrum) : NULL;
}

static void band_range(hb_band_t band, int *fmin, int *fmax) {
  if (band == HB_BAND_5) {
    *fmin = 5140;
    *fmax = 5900;
  } else {
    *fmin = 2401;
    *fmax = 2495;
  }
}

static int chan_freq(hb_band_t band, int ch) {
  if (band == HB_BAND_5)
    return 5000 + ch * 5;
  if (ch == 14)
    return 2484;
  return 2407 + ch * 5;
}

static bool ap_in_band(hb_band_t band, uint16_t channel) {
  bool is_5g = channel >= 15;
  return (band == HB_BAND_5) == is_5g;
}

static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

// --- drawing --------------------------------------------------------------

static void draw_gridline(lv_layer_t *layer, int x, int top_y, int base_y, bool major) {
  lv_draw_line_dsc_t l;
  lv_draw_line_dsc_init(&l);
  l.color = current_theme.border_inactive;
  l.width = 1;
  l.opa = major ? LV_OPA_40 : LV_OPA_20;
  l.p1.x = x;
  l.p1.y = top_y;
  l.p2.x = x;
  l.p2.y = base_y;
  lv_draw_line(layer, &l);
}

static void draw_axis_label(lv_layer_t *layer, int x, int base_y, const char *text) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = text;
  d.font = &lv_font_montserrat_12;
  d.color = current_theme.text_secondary;
  d.align = LV_TEXT_ALIGN_CENTER;
  d.opa = LV_OPA_COVER;
  lv_area_t a = {x - 14, base_y + 2, x + 14, base_y + 16};
  lv_draw_label(layer, &d, &a);
}

static void draw_tent(lv_layer_t *layer,
                      const hb_spec_ctx_t *ctx,
                      const hb_spec_ap_t *ap,
                      int x0,
                      int x1,
                      int top_y,
                      int base_y,
                      int fmin,
                      int fmax) {
  int span = fmax - fmin;
  if (span <= 0)
    return;
  int plot_w = x1 - x0;
  int plot_h = base_y - top_y;

  int freq = chan_freq(ctx->band, ap->channel);
  int cx = x0 + (freq - fmin) * plot_w / span;

  int wmhz = ap->width_mhz ? ap->width_mhz : 20;
  int halfw = (wmhz / 2) * plot_w / span;
  if (halfw < 3)
    halfw = 3;

  int lx = clampi(cx - halfw, x0, x1);
  int rx = clampi(cx + halfw, x0, x1);

  int r = clampi(ap->rssi, RSSI_FLOOR, RSSI_CEIL);
  int height = (r - RSSI_FLOOR) * plot_h / (RSSI_CEIL - RSSI_FLOOR);
  int apex_y = base_y - height;

  lv_color_t color = hb_signal_color(ap->rssi);

  // Translucent body so overlapping networks read as layered congestion.
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.color = color;
  tri.opa = TENT_FILL_OPA;
  tri.p[0].x = lx;
  tri.p[0].y = base_y;
  tri.p[1].x = cx;
  tri.p[1].y = apex_y;
  tri.p[2].x = rx;
  tri.p[2].y = base_y;
  lv_draw_triangle(layer, &tri);

  // Crisp silhouette on top of the fill.
  lv_draw_line_dsc_t edge;
  lv_draw_line_dsc_init(&edge);
  edge.color = color;
  edge.width = 2;
  edge.opa = LV_OPA_COVER;
  edge.round_start = 1;
  edge.round_end = 1;
  edge.p1.x = lx;
  edge.p1.y = base_y;
  edge.p2.x = cx;
  edge.p2.y = apex_y;
  lv_draw_line(layer, &edge);
  edge.p1.x = cx;
  edge.p1.y = apex_y;
  edge.p2.x = rx;
  edge.p2.y = base_y;
  lv_draw_line(layer, &edge);

  if (ap->label[0] != '\0') {
    int ly = (apex_y - 14 >= top_y) ? apex_y - 14 : apex_y + 2;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = ap->label;
    d.font = &lv_font_montserrat_12;
    d.color = current_theme.text_main;
    d.align = LV_TEXT_ALIGN_CENTER;
    d.opa = LV_OPA_COVER;
    lv_area_t a = {cx - 44, ly, cx + 44, ly + 13};
    lv_draw_label(layer, &d, &a);
  }
}

static void redraw(hb_spec_ctx_t *ctx) {
  if (ctx == NULL || ctx->canvas == NULL)
    return;
  if (ctx->w <= (PAD_SIDE * 2) || ctx->h <= (PAD_TOP + PAD_BOTTOM))
    return;

  lv_canvas_fill_bg(ctx->canvas, current_theme.screen_base, LV_OPA_COVER);

  lv_layer_t layer;
  lv_canvas_init_layer(ctx->canvas, &layer);

  int x0 = PAD_SIDE;
  int x1 = ctx->w - PAD_SIDE;
  int top_y = PAD_TOP;
  int base_y = ctx->h - PAD_BOTTOM;
  int fmin, fmax;
  band_range(ctx->band, &fmin, &fmax);
  int span = fmax - fmin;
  int plot_w = x1 - x0;

  // Baseline.
  lv_draw_line_dsc_t base;
  lv_draw_line_dsc_init(&base);
  base.color = current_theme.border_inactive;
  base.width = 1;
  base.opa = LV_OPA_COVER;
  base.p1.x = x0;
  base.p1.y = base_y;
  base.p2.x = x1;
  base.p2.y = base_y;
  lv_draw_line(&layer, &base);

  // Channel grid + axis numbers. Labels are static strings on purpose: a canvas
  // defers the actual draw to finish_layer, so any text pointer must outlive
  // this function - a stack buffer would dangle by the time it is rendered.
  if (ctx->band == HB_BAND_5) {
    for (int i = 0; i < (int)(sizeof(CH5_LABELS) / sizeof(CH5_LABELS[0])); i++) {
      int x = x0 + (chan_freq(HB_BAND_5, CH5_LABELS[i]) - fmin) * plot_w / span;
      draw_gridline(&layer, x, top_y, base_y, true);
      draw_axis_label(&layer, x, base_y, CH5_TEXT[i]);
    }
  } else {
    for (int ch = 1; ch <= 13; ch++) {
      int major_idx = -1;
      for (int m = 0; m < (int)(sizeof(CH24_LABELS) / sizeof(CH24_LABELS[0])); m++)
        if (CH24_LABELS[m] == ch) {
          major_idx = m;
          break;
        }
      int x = x0 + (chan_freq(HB_BAND_24, ch) - fmin) * plot_w / span;
      draw_gridline(&layer, x, top_y, base_y, major_idx >= 0);
      if (major_idx >= 0)
        draw_axis_label(&layer, x, base_y, CH24_TEXT[major_idx]);
    }
  }

  // Networks: weakest first so the strongest tents paint on top (the caller
  // hands us the array strongest-first, so walk it in reverse).
  for (int i = ctx->count - 1; i >= 0; i--) {
    if (!ap_in_band(ctx->band, ctx->aps[i].channel))
      continue;
    draw_tent(&layer, ctx, &ctx->aps[i], x0, x1, top_y, base_y, fmin, fmax);
  }

  lv_canvas_finish_layer(ctx->canvas, &layer);
  lv_obj_invalidate(ctx->canvas);
}

// --- lifecycle ------------------------------------------------------------

static void on_delete(lv_event_t *e) {
  hb_spec_ctx_t *ctx = (hb_spec_ctx_t *)lv_event_get_user_data(e);
  if (ctx == NULL)
    return;
  if (ctx->buf != NULL)
    heap_caps_free(ctx->buf);
  free(ctx);
}

lv_obj_t *hb_spectrum_create(lv_obj_t *parent, int32_t w, int32_t h) {
  if (parent == NULL || w <= 0 || h <= 0)
    return NULL;

  hb_spec_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx == NULL)
    return NULL;

  size_t buf_sz = LV_CANVAS_BUF_SIZE(w, h, 16, LV_DRAW_BUF_STRIDE_ALIGN);
  // Prefer PSRAM: a ~300x110 RGB565 buffer is ~66 KB and must not starve the
  // scarce internal DMA RAM the LCD and SPI bridge depend on.
  ctx->buf = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ctx->buf == NULL)
    ctx->buf = heap_caps_malloc(buf_sz, MALLOC_CAP_DEFAULT);
  if (ctx->buf == NULL) {
    free(ctx);
    return NULL;
  }

  ctx->canvas = lv_canvas_create(parent);
  if (ctx->canvas == NULL) {
    heap_caps_free(ctx->buf);
    free(ctx);
    return NULL;
  }

  ctx->w = w;
  ctx->h = h;
  ctx->band = HB_BAND_24;
  ctx->count = 0;

  lv_canvas_set_buffer(ctx->canvas, ctx->buf, w, h, LV_COLOR_FORMAT_RGB565);
  lv_obj_remove_flag(ctx->canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_user_data(ctx->canvas, ctx);
  lv_obj_add_event_cb(ctx->canvas, on_delete, LV_EVENT_DELETE, ctx);

  redraw(ctx);
  return ctx->canvas;
}

void hb_spectrum_set_band(lv_obj_t *spectrum, hb_band_t band) {
  hb_spec_ctx_t *ctx = ctx_of(spectrum);
  if (ctx == NULL || ctx->band == band)
    return;
  ctx->band = band;
  redraw(ctx);
}

hb_band_t hb_spectrum_get_band(lv_obj_t *spectrum) {
  hb_spec_ctx_t *ctx = ctx_of(spectrum);
  return ctx ? ctx->band : HB_BAND_24;
}

void hb_spectrum_set_aps(lv_obj_t *spectrum, const hb_spectrum_ap_t *aps, int count) {
  hb_spec_ctx_t *ctx = ctx_of(spectrum);
  if (ctx == NULL)
    return;
  if (aps == NULL)
    count = 0;
  if (count > HB_SPEC_MAX_APS)
    count = HB_SPEC_MAX_APS;
  if (count < 0)
    count = 0;

  ctx->count = count;
  for (int i = 0; i < count; i++) {
    ctx->aps[i].channel = aps[i].channel;
    ctx->aps[i].rssi = aps[i].rssi;
    ctx->aps[i].width_mhz = aps[i].width_mhz;
    if (aps[i].label != NULL) {
      strncpy(ctx->aps[i].label, aps[i].label, HB_SPEC_LABEL_LEN - 1);
      ctx->aps[i].label[HB_SPEC_LABEL_LEN - 1] = '\0';
    } else {
      ctx->aps[i].label[0] = '\0';
    }
  }
  redraw(ctx);
}

void hb_spectrum_redraw(lv_obj_t *spectrum) {
  redraw(ctx_of(spectrum));
}
