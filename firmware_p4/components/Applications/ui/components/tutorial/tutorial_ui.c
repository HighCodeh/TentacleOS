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

#include "esp_log.h"
#include "nvs.h"

#include "lvgl.h"
#include "st7789.h"

#include "assets_manager.h"
#include "ui_manager.h"
#include "ui_theme.h"

static const char *TAG = "TUTORIAL";

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
#define WIZ_TEXT_W    200
#define WIZ_ARM_MS    300
#define WIZ_DIM_OPA   150
#define WIZ_SUB_OPA   175
#define WIZ_SWATCH    24

extern lv_group_t *main_group;

static bool s_active = false;
static int s_page = 0;
static uint32_t s_open_tick = 0;
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_content = NULL;
static lv_obj_t *s_prog_fill = NULL;
static lv_obj_t *s_step = NULL;
static lv_obj_t *s_foot = NULL;

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

static void wiz_mascot(lv_obj_t *p) {
  lv_obj_t *img = lv_image_create(p);
  lv_image_dsc_t *dsc = assets_get(WIZ_ART_ASSET);
  if (dsc != NULL)
    lv_image_set_src(img, dsc);
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

static void page_language(lv_obj_t *c) {
  wiz_heading(c, "Language");
  static const char *langs[] = {"English", "Portugues", "Espanol", "Deutsch"};
  lv_obj_t *list = wiz_flow(c, LV_FLEX_FLOW_COLUMN, 4);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *row = lv_label_create(list);
    lv_label_set_text(row, langs[i]);
    lv_obj_set_width(row, WIZ_TEXT_W - 8);
    lv_obj_set_style_text_font(row, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(row, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_radius(row, 5, 0);
    lv_obj_set_style_pad_ver(row, 5, 0);
    if (i == 0) {
      lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(row, current_theme.border_accent, 0);
      lv_obj_set_style_text_color(row, current_theme.screen_base, 0);
    } else {
      lv_obj_set_style_text_color(row, current_theme.text_main, 0);
      lv_obj_set_style_text_opa(row, WIZ_DIM_OPA, 0);
    }
  }
}

static void page_datetime(lv_obj_t *c) {
  wiz_heading(c, "Date & Time");
  lv_obj_t *clk = lv_label_create(c);
  lv_label_set_text(clk, "14:32");
  lv_obj_set_style_text_font(clk, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(clk, current_theme.border_accent, 0);
  wiz_sub(c, "July 31, 2026");
  lv_obj_t *pill = lv_label_create(c);
  lv_label_set_text(pill, LV_SYMBOL_REFRESH "  Sync from companion app");
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
  wiz_status_row(list, LV_SYMBOL_SD_CARD "  SD Card", "Ready " LV_SYMBOL_OK);
  wiz_status_row(list, LV_SYMBOL_DRIVE "  Internal", "OK " LV_SYMBOL_OK);
  wiz_sub(c, "Storage mounted and ready.");
}

static void page_companion(lv_obj_t *c) {
  wiz_heading(c, "Companion App");
  wiz_sub(c, "Enter this code in the phone app:");
  lv_obj_t *box = lv_label_create(c);
  lv_label_set_text(box, "8 8 4 2");
  lv_obj_set_style_text_font(box, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(box, current_theme.text_main, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_set_style_pad_hor(box, 14, 0);
  lv_obj_set_style_pad_ver(box, 8, 0);
  wiz_sub(c, "...or press BACK to skip for now.");
}

static void page_terms(lv_obj_t *c) {
  wiz_heading(c, "Responsible Use");
  wiz_sub(
      c,
      "Use High Boy only where you are authorized. You are responsible for following local law.");
  lv_obj_t *agree = lv_label_create(c);
  lv_label_set_text(agree, LV_SYMBOL_OK "  I understand and agree");
  lv_obj_set_style_text_font(agree, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(agree, current_theme.border_accent, 0);
}

static void page_setupdone(lv_obj_t *c) {
  lv_obj_t *chk = lv_label_create(c);
  lv_label_set_text(chk, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(chk, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(chk, current_theme.border_accent, 0);
  wiz_heading(c, "Setup complete");
  wiz_sub(c, "Now let's meet your guide.");
}

static void page_welcome(lv_obj_t *c) {
  wiz_mascot(c);
  wiz_heading(c, "Hi, I'm Octobit!");
  wiz_sub(c, "Welcome to your High Boy.");
}

static void page_controls(lv_obj_t *c) {
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
  lv_label_set_text(s, "Select");
  lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s, current_theme.text_main, 0);
  lv_obj_t *r3 = wiz_flow(c, LV_FLEX_FLOW_ROW, 4);
  wiz_keycap(r3, "BACK");
  lv_obj_t *b = lv_label_create(r3);
  lv_label_set_text(b, "Return");
  lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(b, current_theme.text_main, 0);
}

static void page_toolkit(lv_obj_t *c) {
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
  lv_obj_t *w = lv_label_create(c);
  lv_label_set_text(w, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_font(w, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(w, lv_color_hex(0xE0954A), 0);
  wiz_heading(c, "Play fair");
  wiz_sub(c, "Only test devices you own or are allowed to.");
}

static void page_theme(lv_obj_t *c) {
  wiz_heading(c, "Pick a vibe");
  static const uint32_t sw[] = {0x7A52D6, 0x2F9E6F, 0xC9761B, 0xD33F6F, 0x2E77C9};
  lv_obj_t *row = wiz_flow(c, LV_FLEX_FLOW_ROW, 8);
  for (int i = 0; i < 5; i++) {
    lv_obj_t *s = lv_obj_create(row);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, WIZ_SWATCH, WIZ_SWATCH);
    lv_obj_set_style_radius(s, 6, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s, lv_color_hex(sw[i]), 0);
    if (i == 0) {
      lv_obj_set_style_border_width(s, 2, 0);
      lv_obj_set_style_border_color(s, current_theme.text_main, 0);
    }
  }
  wiz_sub(c, "12 themes repaint the interface.");
}

static void page_ready(lv_obj_t *c) {
  wiz_mascot(c);
  wiz_heading(c, "You're all set!");
  wiz_sub(c, "Tips will guide you as you explore.");
}

typedef void (*wiz_build_fn)(lv_obj_t *);

typedef struct {
  wiz_build_fn build;
  const char *foot;
} wiz_page_t;

static const wiz_page_t PAGES[] = {
    {page_language, "OK  Next"},
    {page_datetime, "OK  Next      BACK  Back"},
    {page_storage, "OK  Next      BACK  Back"},
    {page_companion, "OK  Next      BACK  Back"},
    {page_terms, "OK  Accept      BACK  Back"},
    {page_setupdone, "OK  Continue      BACK  Back"},
    {page_welcome, "OK  Next      BACK  Back"},
    {page_controls, "OK  Next      BACK  Back"},
    {page_toolkit, "OK  Next      BACK  Back"},
    {page_ethics, "OK  Got it      BACK  Back"},
    {page_theme, "OK  Next      BACK  Back"},
    {page_ready, "OK  Enter      BACK  Back"},
};

#define PAGE_COUNT ((int)(sizeof(PAGES) / sizeof(PAGES[0])))

static void show_page(int idx) {
  s_page = idx;
  lv_obj_clean(s_content);
  PAGES[idx].build(s_content);
  lv_label_set_text(s_foot, PAGES[idx].foot);
  lv_label_set_text_fmt(s_step, "%d / %d", idx + 1, PAGE_COUNT);
  lv_obj_set_width(s_prog_fill, WIZ_PROG_W * (idx + 1) / PAGE_COUNT);
  s_open_tick = lv_tick_get();
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

  lv_obj_t *root = s_root;
  s_root = NULL;
  s_content = NULL;
  s_prog_fill = NULL;
  s_step = NULL;
  s_foot = NULL;

  ui_switch_screen(SCREEN_HOME);
  if (root != NULL)
    lv_obj_del_async(root);
}

static void key_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_KEY)
    return;
  if (lv_tick_get() - s_open_tick < WIZ_ARM_MS)
    return;
  uint32_t key = lv_event_get_key(e);
  if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT) {
    if (s_page + 1 < PAGE_COUNT)
      show_page(s_page + 1);
    else
      finish();
  } else if (key == LV_KEY_LEFT || key == LV_KEY_ESC) {
    if (s_page > 0)
      show_page(s_page - 1);
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

  if (main_group != NULL) {
    lv_group_remove_all_objs(main_group);
    lv_group_add_obj(main_group, root);
    lv_group_focus_obj(root);
    lv_group_set_editing(main_group, false);
  }

  show_page(0);
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
