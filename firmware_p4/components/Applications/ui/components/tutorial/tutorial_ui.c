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

#include "tutorial_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "host_link_sec.h"
#include "storage_assets.h"
#include "storage_init.h"
#include "sys_time.h"
#include "tos_config.h"
#include "tos_storage_paths.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "TUTORIAL";

static const char *const WIZ_THEME_NAMES[] = {"default", "cyber_blue"};

#define TUT_NVS_NS  "tutorial"
#define TUT_NVS_KEY "done"

#define WIZ_ART_ASSET "/assets/img/image.bin"

#define WIZ_MARGIN    16
#define WIZ_STEP_Y    6
#define WIZ_PROG_Y    24
#define WIZ_PROG_W    (LCD_H_RES - 2 * WIZ_MARGIN)
#define WIZ_PROG_H    4
#define WIZ_CONTENT_Y 36
#define WIZ_CONTENT_H 252
#define WIZ_FOOT_Y    -8
#define WIZ_GAP       9
#define WIZ_TEXT_W    206
#define WIZ_ARM_MS    260
#define WIZ_DIM_OPA   150
#define WIZ_SUB_OPA   180
#define WIZ_SWATCH    24

// Animation timing (slow + cinematic; affordable now that PSRAM keeps frames resident).
#define FADE_OUT_MS  180
#define FADE_IN_MS   320
#define PROG_MS      460
#define STAGGER_MS   110  // gap between elements fading in one after another
#define MASCOT_FADE  420  // octobit fade-in
#define MASCOT_ENTER 640  // when the text starts fading in (after octobit is established)
#define BOB_MS       1800 // octobit float period (slow, gentle)
#define BOB_PX       6
#define WOBBLE_MS    560 // arrow idle horizontal swing period
#define WOBBLE_PX    5

// Vertical chooser geometry.
#define CH_ROW_H   30
#define CH_ARROW_W 22
#define CH_LIST_W  190
#define CH_MAX     6

extern lv_group_t *main_group;

static bool s_active = false;
static bool s_busy = false; // mid page-transition: swallow input
static int s_page = 0;
static int s_pending = 0;
static uint32_t s_open_tick = 0;

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_content = NULL;
static lv_obj_t *s_prog_fill = NULL;
static lv_obj_t *s_step = NULL;
static lv_obj_t *s_foot = NULL;
static lv_obj_t *s_mascot = NULL; // octobit on the current page (NULL if none)

// Active chooser (rebuilt per page; count==0 means the page is not a chooser).
static lv_obj_t *s_ch_rows[CH_MAX] = {NULL};
static lv_obj_t *s_ch_arrow = NULL;
static int s_ch_count = 0;
static int s_ch_sel = 0;
static int *s_ch_selp = NULL;

static int s_lang_sel = 0;
static int s_theme_sel = 0;

// ---- small animation helpers ------------------------------------------------

static void anim_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}
static void anim_ty_cb(void *obj, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}
static void anim_tx_cb(void *obj, int32_t v) {
  lv_obj_set_style_translate_x((lv_obj_t *)obj, v, 0);
}

static void fade(lv_obj_t *o,
                 int32_t from,
                 int32_t to,
                 uint32_t ms,
                 uint32_t delay,
                 lv_anim_path_cb_t path,
                 lv_anim_completed_cb_t done) {
  lv_obj_set_style_opa(o, (lv_opa_t)from, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_path_cb(&a, path);
  if (done)
    lv_anim_set_completed_cb(&a, done);
  lv_anim_start(&a);
}

// ---- reusable page widgets --------------------------------------------------

static lv_obj_t *wiz_heading(lv_obj_t *p, const char *text) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(l, current_theme.text_main, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

static lv_obj_t *wiz_sub(lv_obj_t *p, const char *text) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, WIZ_TEXT_W);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(l, WIZ_SUB_OPA, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

static lv_obj_t *wiz_flow(lv_obj_t *p, lv_flex_flow_t flow, int gap) {
  lv_obj_t *box = lv_obj_create(p);
  lv_obj_remove_style_all(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(box, WIZ_TEXT_W);
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(box, flow);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(box, gap, 0);
  lv_obj_set_style_pad_column(box, gap, 0);
  return box;
}

static lv_obj_t *wiz_keycap(lv_obj_t *p, const char *text) {
  lv_obj_t *k = lv_label_create(p);
  lv_label_set_text(k, text);
  lv_obj_set_style_text_font(k, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(k, current_theme.text_main, 0);
  lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(k, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_width(k, 1, 0);
  lv_obj_set_style_border_color(k, current_theme.border_inactive, 0);
  lv_obj_set_style_radius(k, 3, 0);
  lv_obj_set_style_pad_hor(k, 5, 0);
  lv_obj_set_style_pad_ver(k, 3, 0);
  return k;
}

static void wiz_chip(lv_obj_t *p, const char *text, lv_color_t accent) {
  lv_obj_t *k = lv_label_create(p);
  lv_label_set_text(k, text);
  lv_obj_set_style_text_font(k, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(k, current_theme.text_main, 0);
  lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(k, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_width(k, 1, 0);
  lv_obj_set_style_border_color(k, accent, 0);
  lv_obj_set_style_radius(k, 6, 0);
  lv_obj_set_style_pad_hor(k, 8, 0);
  lv_obj_set_style_pad_ver(k, 4, 0);
}

// A small octobit that gently bobs, added to the top of a guide page's content.
static void wiz_mascot(lv_obj_t *p, int zoom) {
  lv_image_dsc_t *dsc = assets_get(WIZ_ART_ASSET);
  if (dsc == NULL)
    return;
  lv_obj_t *img = lv_image_create(p);
  lv_image_set_src(img, dsc);
  if (zoom != 256)
    lv_image_set_scale(img, zoom);
  s_mascot = img; // build_page_now choreographs its entrance (fade + glide in)
  // Gentle continuous float (translate-y; independent of the entrance translate-x).
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, img);
  lv_anim_set_exec_cb(&a, anim_ty_cb);
  lv_anim_set_values(&a, -BOB_PX, BOB_PX);
  lv_anim_set_duration(&a, BOB_MS);
  lv_anim_set_reverse_duration(&a, BOB_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

// ---- animated vertical chooser (the "setinha" selector) ---------------------

static void ch_restyle(void) {
  for (int i = 0; i < s_ch_count; i++) {
    lv_obj_t *row = s_ch_rows[i];
    if (!row)
      continue;
    bool on = (i == s_ch_sel);
    lv_obj_set_style_bg_opa(row, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(row, current_theme.border_accent, 0);
    lv_obj_t *lbl = lv_obj_get_child(row, 0);
    if (lbl) {
      lv_obj_set_style_text_color(lbl, on ? current_theme.screen_base : current_theme.text_main, 0);
      lv_obj_set_style_text_opa(lbl, on ? LV_OPA_COVER : WIZ_DIM_OPA, 0);
    }
  }
}

// Vertical alignment is INSTANT — the arrow snaps to the selected row; the only
// motion it keeps is the idle horizontal swing (started in build_chooser).
static void ch_arrow_to(int idx) {
  if (!s_ch_arrow)
    return;
  lv_obj_set_y(s_ch_arrow, idx * CH_ROW_H + (CH_ROW_H - 16) / 2);
}

static void
build_chooser(lv_obj_t *c, const char **items, const uint32_t *colors, int count, int *selp) {
  if (count > CH_MAX)
    count = CH_MAX;
  s_ch_count = count;
  s_ch_selp = selp;
  s_ch_sel = (selp && *selp < count) ? *selp : 0;

  lv_obj_t *list = lv_obj_create(c);
  lv_obj_remove_style_all(list);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(list, CH_LIST_W, count * CH_ROW_H);

  for (int i = 0; i < count; i++) {
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, CH_LIST_W - CH_ARROW_W, CH_ROW_H - 4);
    lv_obj_set_pos(row, CH_ARROW_W, i * CH_ROW_H);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 10, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, items[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);

    if (colors != NULL) {
      lv_obj_t *dot = lv_obj_create(row);
      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot, 16, 16);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(dot, lv_color_hex(colors[i]), 0);
    }
    s_ch_rows[i] = row;
  }

  s_ch_arrow = lv_label_create(list);
  lv_label_set_text(s_ch_arrow, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(s_ch_arrow, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_ch_arrow, current_theme.border_accent, 0);
  lv_obj_set_pos(s_ch_arrow, 2, 0);

  ch_restyle();
  ch_arrow_to(s_ch_sel);

  // Idle swing so it reads as "point here, use UP/DOWN".
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_ch_arrow);
  lv_anim_set_exec_cb(&a, anim_tx_cb);
  lv_anim_set_values(&a, 0, WOBBLE_PX);
  lv_anim_set_duration(&a, WOBBLE_MS);
  lv_anim_set_reverse_duration(&a, WOBBLE_MS);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static void chooser_move(int delta) {
  int n = s_ch_sel + delta;
  if (n < 0 || n >= s_ch_count)
    return;
  s_ch_sel = n;
  if (s_ch_selp)
    *s_ch_selp = n;
  ch_restyle();
  ch_arrow_to(n);
}

// ---- pages ------------------------------------------------------------------

static void page_language(lv_obj_t *c) {
  wiz_heading(c, "Language");
  wiz_sub(c, "Pick your language. UP / DOWN to choose.");
  static const char *langs[] = {"English", "Portugues", "Espanol", "Deutsch"};
  build_chooser(c, langs, NULL, 4, &s_lang_sel);
}

static void page_datetime(lv_obj_t *c) {
  wiz_heading(c, "Date & Time");
  lv_obj_t *clk = lv_label_create(c);
  char clkbuf[16];
  if (!sys_time_format(clkbuf, sizeof(clkbuf), "%H:%M"))
    snprintf(clkbuf, sizeof(clkbuf), "--:--");
  lv_label_set_text(clk, clkbuf);
  lv_obj_set_style_text_font(clk, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(clk, current_theme.border_accent, 0);
  wiz_sub(c, "High Boy timestamps every capture and log. Sync the clock from the companion app.");
  lv_obj_t *pill = lv_label_create(c);
  lv_label_set_text(pill, LV_SYMBOL_REFRESH "  Sync from companion");
  lv_obj_set_style_text_font(pill, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(pill, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pill, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_width(pill, 1, 0);
  lv_obj_set_style_border_color(pill, current_theme.border_accent, 0);
  lv_obj_set_style_radius(pill, 8, 0);
  lv_obj_set_style_pad_hor(pill, 9, 0);
  lv_obj_set_style_pad_ver(pill, 4, 0);
}

static void wiz_status_row(lv_obj_t *list, const char *name, const char *value) {
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, WIZ_TEXT_W);
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(row, 4, 0);
  lv_obj_t *n = lv_label_create(row);
  lv_label_set_text(n, name);
  lv_obj_set_style_text_font(n, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(n, current_theme.text_main, 0);
  lv_obj_t *v = lv_label_create(row);
  lv_label_set_text(v, value);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(v, current_theme.border_accent, 0);
}

static void page_storage(lv_obj_t *c) {
  wiz_heading(c, "Storage");
  lv_obj_t *list = wiz_flow(c, LV_FLEX_FLOW_COLUMN, 2);
  wiz_status_row(list,
                 LV_SYMBOL_SD_CARD "  microSD",
                 storage_is_mounted() ? "Ready " LV_SYMBOL_OK : "Missing " LV_SYMBOL_WARNING);
  wiz_status_row(list,
                 LV_SYMBOL_DRIVE "  Internal",
                 storage_assets_is_mounted() ? "OK " LV_SYMBOL_OK : "Fault " LV_SYMBOL_WARNING);
  wiz_sub(c, "microSD keeps your captures, scripts and firmware; internal flash runs the OS.");
}

static void page_companion(lv_obj_t *c) {
  wiz_heading(c, "Companion App");
  wiz_sub(c, "Pair the phone app for time sync, file transfer and remote control.");
  lv_obj_t *box = lv_label_create(c);
  char psk[HOST_LINK_PSK_HEX_SIZE];
  if (host_link_sec_get_psk_hex(psk, sizeof(psk)) == ESP_OK) {
    lv_label_set_text(box, psk);
    lv_label_set_long_mode(box, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(box, WIZ_TEXT_W);
    lv_obj_set_style_text_align(box, LV_TEXT_ALIGN_CENTER, 0);
  } else {
    lv_label_set_text(box, "Pair from Settings");
  }
  lv_obj_set_style_text_font(box, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(box, current_theme.text_main, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_set_style_pad_hor(box, 14, 0);
  lv_obj_set_style_pad_ver(box, 8, 0);
  wiz_sub(c, "Provision this key in the app, or press OK to skip.");
}

static void page_terms(lv_obj_t *c) {
  wiz_heading(c, "Responsible Use");
  wiz_sub(c,
          "High Boy is a security tool. Only test devices and networks you own or are explicitly "
          "authorized to. You are responsible for following local law.");
  lv_obj_t *agree = lv_label_create(c);
  lv_label_set_text(agree, LV_SYMBOL_OK "  I understand and agree");
  lv_obj_set_style_text_font(agree, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(agree, current_theme.border_accent, 0);
}

static void page_setupdone(lv_obj_t *c) {
  wiz_mascot(c, 200);
  wiz_heading(c, "Setup complete");
  wiz_sub(c, "Now let's meet your guide.");
}

static void page_welcome(lv_obj_t *c) {
  wiz_mascot(c, 256);
  wiz_heading(c, "Hi, I'm Octobit!");
  wiz_sub(c, "I'll ride along and point things out as you explore your High Boy.");
}

static void page_controls(lv_obj_t *c) {
  wiz_mascot(c, 150);
  wiz_heading(c, "Controls");
  lv_obj_t *r1 = wiz_flow(c, LV_FLEX_FLOW_ROW, 4);
  wiz_keycap(r1, LV_SYMBOL_UP);
  wiz_keycap(r1, LV_SYMBOL_DOWN);
  wiz_keycap(r1, LV_SYMBOL_LEFT);
  wiz_keycap(r1, LV_SYMBOL_RIGHT);
  lv_obj_t *m = lv_label_create(r1);
  lv_label_set_text(m, "Move");
  lv_obj_set_style_text_font(m, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(m, current_theme.text_main, 0);
  lv_obj_t *r2 = wiz_flow(c, LV_FLEX_FLOW_ROW, 4);
  wiz_keycap(r2, "OK");
  lv_obj_t *s = lv_label_create(r2);
  lv_label_set_text(s, "Select   -   hold UP anywhere for quick settings");
  lv_obj_set_width(s, WIZ_TEXT_W - 40);
  lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s, current_theme.text_main, 0);
}

static void page_toolkit(lv_obj_t *c) {
  wiz_mascot(c, 150);
  wiz_heading(c, "Every signal, one device");
  lv_obj_t *g = wiz_flow(c, LV_FLEX_FLOW_ROW_WRAP, 6);
  wiz_chip(g, "WI-FI", current_theme.protocol_wifi);
  wiz_chip(g, "BLE", current_theme.protocol_ble);
  wiz_chip(g, "NFC", current_theme.protocol_nfc);
  wiz_chip(g, "RFID", current_theme.protocol_rfid);
  wiz_chip(g, "SUB-GHZ", current_theme.protocol_subghz);
  wiz_chip(g, "IR", current_theme.protocol_ir);
  wiz_chip(g, "LORA", current_theme.protocol_lora);
  wiz_chip(g, "BADUSB", current_theme.border_accent);
}

static void page_ethics(lv_obj_t *c) {
  wiz_mascot(c, 150);
  lv_obj_t *w = lv_label_create(c);
  lv_label_set_text(w, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_font(w, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(w, lv_color_hex(0xE0954A), 0);
  wiz_heading(c, "Play fair");
  wiz_sub(c, "Only probe what's yours. Curiosity is great; consent comes first.");
}

static void page_theme(lv_obj_t *c) {
  wiz_heading(c, "Theme");
  wiz_sub(c, "UP / DOWN to choose. Applied when you finish.");
  static const char *names[] = {"Default", "Cyber Blue"};
  static const uint32_t sw[] = {0x834EC6, 0x00D9FF};
  s_theme_sel = (strcmp(g_config_screen.theme, WIZ_THEME_NAMES[1]) == 0) ? 1 : 0;
  build_chooser(c, names, sw, 2, &s_theme_sel);
}

static void page_ready(lv_obj_t *c) {
  wiz_mascot(c, 256);
  wiz_heading(c, "You're all set!");
  wiz_sub(c, "Tips will nudge you as you go. Let's dive in.");
}

typedef void (*wiz_build_fn)(lv_obj_t *);

typedef struct {
  wiz_build_fn build;
  const char *foot;
  bool mascot; // keep the persistent guide visible from here on
} wiz_page_t;

static const wiz_page_t PAGES[] = {
    {page_language, "UP/DOWN Pick    OK Next", false},
    {page_datetime, "OK Next    BACK Back", false},
    {page_storage, "OK Next    BACK Back", false},
    {page_companion, "OK Next    BACK Back", false},
    {page_terms, "OK Accept    BACK Back", false},
    {page_setupdone, "OK Continue    BACK Back", true},
    {page_welcome, "OK Next    BACK Back", true},
    {page_controls, "OK Next    BACK Back", true},
    {page_toolkit, "OK Next    BACK Back", true},
    {page_ethics, "OK Got it    BACK Back", true},
    {page_theme, "UP/DOWN Preview    OK Next", true},
    {page_ready, "OK Enter    BACK Back", true},
};

#define PAGE_COUNT ((int)(sizeof(PAGES) / sizeof(PAGES[0])))

static void arm_done(lv_anim_t *a) {
  (void)a;
  s_busy = false;
  s_open_tick = lv_tick_get();
}

static void build_page_now(int idx) {
  s_busy = true; // cleared by arm_done() when the fade-in finishes
  s_page = idx;
  s_ch_count = 0;
  s_ch_arrow = NULL;
  s_mascot = NULL; // set by wiz_mascot() during the build if this page has one
  lv_obj_clean(s_content);
  PAGES[idx].build(s_content);
  lv_label_set_text(s_foot, PAGES[idx].foot);
  lv_label_set_text_fmt(s_step, "%d / %d", idx + 1, PAGE_COUNT);

  // Progress bar eases to the new fraction.
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_prog_fill);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_width);
  lv_anim_set_values(&a, lv_obj_get_width(s_prog_fill), WIZ_PROG_W * (idx + 1) / PAGE_COUNT);
  lv_anim_set_duration(&a, PROG_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);

  // Compose the page in on the dark canvas: octobit fades in and glides from the
  // side first, THEN the text fades in below it, element by element.
  lv_obj_set_style_opa(s_content, LV_OPA_COVER, 0);

  uint32_t base = 0;
  if (s_mascot) {
    // Octobit simply fades in and floats in place (no slide); the text waits for it.
    fade(s_mascot, LV_OPA_TRANSP, LV_OPA_COVER, MASCOT_FADE, 0, lv_anim_path_ease_out, NULL);
    base = MASCOT_ENTER;
  }

  uint32_t n = lv_obj_get_child_count(s_content);
  int step = 0;
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *ch = lv_obj_get_child(s_content, i);
    if (ch == s_mascot)
      continue;
    fade(ch,
         LV_OPA_TRANSP,
         LV_OPA_COVER,
         FADE_IN_MS,
         base + (uint32_t)step * STAGGER_MS,
         lv_anim_path_ease_out,
         NULL);
    step++;
  }

  // Arm input once the whole sequence has settled (a no-op timing anim).
  uint32_t settle = base + (step > 0 ? (uint32_t)(step - 1) * STAGGER_MS : 0) + FADE_IN_MS;
  fade(s_content, LV_OPA_COVER, LV_OPA_COVER, settle, 0, lv_anim_path_linear, arm_done);
}

static void fade_out_done(lv_anim_t *a) {
  (void)a;
  build_page_now(s_pending);
}

static void go_page(int idx) {
  if (idx < 0 || idx >= PAGE_COUNT)
    return;
  s_pending = idx;
  s_busy = true;
  fade(s_content, LV_OPA_COVER, LV_OPA_TRANSP, FADE_OUT_MS, 0, lv_anim_path_ease_in, fade_out_done);
}

static void mark_done(void) {
  nvs_handle_t h;
  if (nvs_open(TUT_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, TUT_NVS_KEY, 1);
    nvs_commit(h);
    nvs_close(h);
  }
}

static void finish(void) {
  if (!s_active)
    return;
  s_active = false;
  mark_done();

  if (s_theme_sel >= 0 &&
      s_theme_sel < (int)(sizeof(WIZ_THEME_NAMES) / sizeof(WIZ_THEME_NAMES[0]))) {
    const char *name = WIZ_THEME_NAMES[s_theme_sel];
    if (strcmp(g_config_screen.theme, name) != 0) {
      strncpy(g_config_screen.theme, name, sizeof(g_config_screen.theme) - 1);
      g_config_screen.theme[sizeof(g_config_screen.theme) - 1] = '\0';
      tos_config_save(TOS_PATH_CONFIG_SCREEN, "screen");
    }
    ui_theme_load_from_name(name);
  }

  lv_obj_t *root = s_root;
  s_root = NULL;
  s_content = NULL;
  s_prog_fill = NULL;
  s_step = NULL;
  s_foot = NULL;
  s_ch_arrow = NULL;
  s_ch_count = 0;
  s_mascot = NULL;

  ui_switch_screen(SCREEN_HOME);
  if (root != NULL)
    lv_obj_del_async(root);
}

static void key_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  if (s_busy)
    return;
  if (lv_tick_get() - s_open_tick < WIZ_ARM_MS)
    return;
  uint32_t key = lv_event_get_key(e);

  // On a chooser page UP/DOWN move the selection (arrow slides to it).
  if (s_ch_count > 0) {
    if (key == LV_KEY_UP) {
      chooser_move(-1);
      return;
    }
    if (key == LV_KEY_DOWN) {
      chooser_move(1);
      return;
    }
  }

  if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
    if (s_page + 1 < PAGE_COUNT)
      go_page(s_page + 1);
    else
      finish();
  } else if (key == LV_KEY_LEFT || key == LV_KEY_ESC) {
    if (s_page > 0)
      go_page(s_page - 1);
  }
}

void tutorial_start(void) {
  if (s_active)
    return;

  ESP_LOGI(TAG, "onboarding wizard: starting (%d pages)", PAGE_COUNT);

  lv_obj_t *root = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(root, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, key_cb, LV_EVENT_KEY, NULL);
  s_root = root;

  s_step = lv_label_create(root);
  lv_obj_set_style_text_font(s_step, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_step, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_step, WIZ_DIM_OPA, 0);
  lv_obj_align(s_step, LV_ALIGN_TOP_RIGHT, -WIZ_MARGIN, WIZ_STEP_Y);

  lv_obj_t *track = lv_obj_create(root);
  lv_obj_remove_style_all(track);
  lv_obj_set_size(track, WIZ_PROG_W, WIZ_PROG_H);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(track, current_theme.border_inactive, 0);
  lv_obj_set_style_radius(track, WIZ_PROG_H / 2, 0);
  lv_obj_align(track, LV_ALIGN_TOP_LEFT, WIZ_MARGIN, WIZ_PROG_Y);

  s_prog_fill = lv_obj_create(track);
  lv_obj_remove_style_all(s_prog_fill);
  lv_obj_set_height(s_prog_fill, WIZ_PROG_H);
  lv_obj_set_width(s_prog_fill, 0);
  lv_obj_set_style_bg_opa(s_prog_fill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_prog_fill, current_theme.border_accent, 0);
  lv_obj_set_style_radius(s_prog_fill, WIZ_PROG_H / 2, 0);
  lv_obj_align(s_prog_fill, LV_ALIGN_LEFT_MID, 0, 0);

  s_content = lv_obj_create(root);
  lv_obj_remove_style_all(s_content);
  lv_obj_remove_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_content, LCD_H_RES, WIZ_CONTENT_H);
  lv_obj_align(s_content, LV_ALIGN_TOP_MID, 0, WIZ_CONTENT_Y);
  lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(
      s_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_content, WIZ_GAP, 0);
  lv_obj_set_style_pad_all(s_content, WIZ_MARGIN, 0);

  s_foot = lv_label_create(root);
  lv_obj_set_style_text_font(s_foot, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_foot, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(s_foot, WIZ_SUB_OPA, 0);
  lv_obj_align(s_foot, LV_ALIGN_BOTTOM_MID, 0, WIZ_FOOT_Y);

  s_active = true;
  s_busy = false;

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, root);
    lv_group_focus_obj(root);
    lv_group_set_editing(main_group, false);
  }

  build_page_now(0);
  lv_screen_load(root);
}

bool tutorial_should_run(void) {
  uint8_t done = 0;
  nvs_handle_t h;
  esp_err_t err = nvs_open(TUT_NVS_NS, NVS_READONLY, &h);
  if (err == ESP_OK) {
    nvs_get_u8(h, TUT_NVS_KEY, &done);
    nvs_close(h);
  }
  bool run = (done == 0);
  ESP_LOGI(TAG,
           "should_run: nvs_open=%s done=%u -> %s",
           esp_err_to_name(err),
           done,
           run ? "START" : "skip");
  return run;
}

bool tutorial_is_active(void) {
  return s_active;
}

void tutorial_reset(void) {
  nvs_handle_t h;
  if (nvs_open(TUT_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_set_u8(h, TUT_NVS_KEY, 0);
    nvs_commit(h);
    nvs_close(h);
  }
}
