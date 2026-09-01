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

#include "sd_health_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"
#include "vfs_core.h"
#include "vfs_sdcard.h"

#define RETEST_TICK_MS 90

#define HDR_TITLE  "SD HEALTH"
#define HDR_ICON   "/assets/icons/sd_card.bin"
#define FOOTER_TXT "OK REMOUNT   R RETEST   BACK"

#define MX        8
#define CONTENT_W (ui_screen_w() - 2 * MX)

#define COL_ACC2 0xB89AFF

#define HERO_Y   48
#define HERO_H   108
#define TILE_Y   164
#define TILE_H   66
#define TILE_GAP 8
#define TILE_W   ((CONTENT_W - TILE_GAP) / 2)
#define CTA_Y    238
#define CTA_H    34

#define HERO_PAD     10
#define HERO_INNER_W (CONTENT_W - 2 * HERO_PAD - 2)

#define GLYPH_W 34
#define GLYPH_H 44
#define INFO_X  (GLYPH_W + 10)
#define BAR_Y   54
#define BAR_H   10
#define KV_Y    70

#define SD_PATH      "/sdcard"
#define BYTES_PER_GB 1000000000ULL
#define CTA_TXT      "Remount & retest"

#define READ_VAL  "--"
#define WRITE_VAL "--"
#define UNIT_IDLE "MB/s"
#define UNIT_OK   "MB/s OK"
#define UNIT_TEST "MB/s ..."

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_retest_timer = NULL;

static lv_obj_t *s_read_val = NULL;
static lv_obj_t *s_write_val = NULL;
static lv_obj_t *s_read_sub = NULL;
static lv_obj_t *s_write_sub = NULL;

static lv_obj_t *make_card(lv_obj_t *parent, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  return card;
}

static void build_glyph(lv_obj_t *card) {
  lv_obj_t *body = lv_obj_create(card);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(body, GLYPH_W, GLYPH_H);
  lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(body, 3, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_bg_color(body, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(body, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(body, 2, 0);

  for (int i = 0; i < 3; i++) {
    lv_obj_t *pin = lv_obj_create(body);
    lv_obj_remove_flag(pin, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pin, 2, 6);
    lv_obj_align(pin, LV_ALIGN_TOP_LEFT, 8 + i * 6, 3);
    lv_obj_set_style_border_width(pin, 0, 0);
    lv_obj_set_style_radius(pin, 1, 0);
    lv_obj_set_style_bg_color(pin, current_theme.border_accent, 0);
    lv_obj_set_style_bg_opa(pin, LV_OPA_COVER, 0);
  }
}

static void build_hero(void) {
  bool mounted = vfs_sdcard_is_mounted();
  char namebuf[24] = "No SD card";
  char sizebuf[16] = "--";
  char usedbuf[24] = "Insert an SD card";
  char freebuf[24] = "";
  unsigned pct = 0;
  if (mounted) {
    if (!(vfs_sdcard_get_name(namebuf, sizeof(namebuf)) && namebuf[0]))
      strlcpy(namebuf, "SD Card", sizeof(namebuf));
    vfs_statvfs_t st = {0};
    if (vfs_statvfs(SD_PATH, &st) == ESP_OK && st.total_bytes > 0) {
      pct = (unsigned)((st.used_bytes * 100ULL) / st.total_bytes);
      unsigned used_tenths = (unsigned)((st.used_bytes * 10ULL) / BYTES_PER_GB);
      unsigned free_tenths = (unsigned)((st.free_bytes * 10ULL) / BYTES_PER_GB);
      unsigned total_gb = (unsigned)((st.total_bytes + BYTES_PER_GB / 2) / BYTES_PER_GB);
      snprintf(sizebuf, sizeof(sizebuf), "%uGB", total_gb);
      snprintf(usedbuf, sizeof(usedbuf), "%u.%u GB used", used_tenths / 10, used_tenths % 10);
      snprintf(freebuf, sizeof(freebuf), "%u.%u GB free", free_tenths / 10, free_tenths % 10);
    }
  }

  lv_obj_t *card = make_card(s_screen, CONTENT_W, HERO_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, HERO_Y);
  lv_obj_set_style_pad_all(card, HERO_PAD, 0);

  build_glyph(card);

  lv_obj_t *name = lv_label_create(card);
  lv_label_set_text(name, namebuf);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(name, current_theme.text_main, 0);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, INFO_X, 4);

  lv_obj_t *spec = lv_label_create(card);
  lv_label_set_text(spec, mounted ? vfs_sdcard_fs_type() : "Not mounted");
  lv_obj_set_style_text_font(spec, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(spec, current_theme.text_secondary, 0);
  lv_obj_align(spec, LV_ALIGN_TOP_LEFT, INFO_X, 26);

  lv_obj_t *chip = lv_obj_create(card);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, 46, 22);
  lv_obj_align(chip, LV_ALIGN_TOP_RIGHT, 0, 10);
  lv_obj_set_style_radius(chip, 6, 0);
  lv_obj_set_style_pad_all(chip, 0, 0);
  lv_obj_set_style_bg_color(chip, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_t *chip_lbl = lv_label_create(chip);
  lv_label_set_text(chip_lbl, sizebuf);
  lv_obj_set_style_text_font(chip_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(chip_lbl, current_theme.screen_base, 0);
  lv_obj_center(chip_lbl);

  lv_obj_t *bar = lv_bar_create(card);
  lv_obj_set_size(bar, HERO_INNER_W, BAR_H);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, BAR_Y);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, (int32_t)pct, LV_ANIM_OFF);
  lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, current_theme.bg_primary, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, current_theme.text_secondary, LV_PART_MAIN);
  lv_obj_set_style_border_opa(bar, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(bar, lv_color_hex(COL_ACC2), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);

  lv_obj_t *used = lv_label_create(card);
  lv_label_set_text(used, usedbuf);
  lv_obj_set_style_text_font(used, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(used, current_theme.text_secondary, 0);
  lv_obj_align(used, LV_ALIGN_TOP_LEFT, 0, KV_Y);

  lv_obj_t *free_lbl = lv_label_create(card);
  lv_label_set_text(free_lbl, freebuf);
  lv_obj_set_style_text_font(free_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(free_lbl, current_theme.text_main, 0);
  lv_obj_align(free_lbl, LV_ALIGN_TOP_RIGHT, 0, KV_Y);
}

static void
build_tile(int x, const char *caption, const char *value, lv_obj_t **val_out, lv_obj_t **sub_out) {
  lv_obj_t *card = make_card(s_screen, TILE_W, TILE_H);
  lv_obj_align(
      card, LV_ALIGN_TOP_LEFT, x, LV_MIN(TILE_Y, ui_screen_h() - UI_CHROME_FOOTER_H - TILE_H));
  lv_obj_set_style_pad_ver(card, 4, 0);
  lv_obj_set_style_pad_row(card, 0, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *cap = lv_label_create(card);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(cap, current_theme.text_secondary, 0);

  lv_obj_t *val = lv_label_create(card);
  lv_label_set_text(val, value);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(val, lv_color_hex(UI_COL_SUCCESS), 0);

  lv_obj_t *sub = lv_label_create(card);
  lv_label_set_text(sub, UNIT_IDLE);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(sub, current_theme.text_secondary, 0);

  *val_out = val;
  *sub_out = sub;
}

static void build_cta(void) {
  lv_obj_t *row = lv_obj_create(s_screen);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, CONTENT_W, CTA_H);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, LV_MIN(CTA_Y, ui_screen_h() - UI_CHROME_FOOTER_H - CTA_H));
  lv_obj_set_style_radius(row, 9, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(row, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_shadow_width(row, 14, 0);
  lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_spread(row, -3, 0);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, CTA_TXT);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, current_theme.border_accent, 0);
  lv_obj_center(lbl);
}

static void set_tiles_testing(bool testing) {
  const char *unit = testing ? UNIT_TEST : UNIT_OK;
  if (s_read_sub)
    lv_label_set_text(s_read_sub, unit);
  if (s_write_sub)
    lv_label_set_text(s_write_sub, unit);
}

static void set_speed(lv_obj_t *label, float mbs) {
  if (!label)
    return;
  int v = (int)(mbs * 10.0f + 0.5f);
  char buf[8];
  lv_snprintf(buf, sizeof(buf), "%d.%d", v / 10, v % 10);
  lv_label_set_text(label, buf);
}

static void retest_run_cb(lv_timer_t *t) {
  lv_timer_delete(t);
  s_retest_timer = NULL;
  if (lv_screen_active() != s_screen)
    return;

  float rmbs = 0.0f;
  float wmbs = 0.0f;
  esp_err_t r = vfs_sdcard_benchmark(&rmbs, &wmbs);
  set_tiles_testing(false);

  if (r == ESP_OK) {
    set_speed(s_read_val, rmbs);
    set_speed(s_write_val, wmbs);
    ui_feedback(UI_FB_READ);
    notify(NOTIFY_SAVED, "SD retest OK");
  } else {
    if (s_read_val)
      lv_label_set_text(s_read_val, "--");
    if (s_write_val)
      lv_label_set_text(s_write_val, "--");
    notify(NOTIFY_WARNING, "SD test failed");
  }
}

static void start_retest(void) {
  if (s_retest_timer != NULL)
    return;
  if (!vfs_sdcard_is_mounted()) {
    notify(NOTIFY_WARNING, "No SD card");
    return;
  }
  set_tiles_testing(true);
  // Defer one tick so the "..." state paints before the blocking benchmark runs.
  s_retest_timer = lv_timer_create(retest_run_cb, RETEST_TICK_MS, NULL);
  ui_feedback(UI_FB_SELECT);
}

static void sd_health_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_STORAGE);
      break;
    case INPUT_BTN_OK:
      if (press) {
        ui_feedback(UI_FB_SELECT);
        if (vfs_sdcard_is_mounted())
          vfs_sdcard_deinit();
        esp_err_t r = vfs_sdcard_init();
        notify(r == ESP_OK ? NOTIFY_SAVED : NOTIFY_WARNING,
               r == ESP_OK ? "SD remounted" : "No SD card");
        ui_sd_health_open();
      }
      break;
    case INPUT_BTN_RIGHT:
      if (press)
        start_retest();
      break;
    default:
      break;
  }
}

void ui_sd_health_open(void) {
  if (s_retest_timer != NULL) {
    lv_timer_delete(s_retest_timer);
    s_retest_timer = NULL;
  }
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_read_val = s_write_val = s_read_sub = s_write_sub = NULL;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_hero();
  build_tile(MX, "READ", READ_VAL, &s_read_val, &s_read_sub);
  build_tile(MX + TILE_W + TILE_GAP, "WRITE", WRITE_VAL, &s_write_val, &s_write_sub);
  build_cta();

  ui_chrome_footer(s_screen, FOOTER_TXT);

  ui_input_set_screen_handler(sd_health_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);

  // Fill the tiles with a real measurement as soon as the screen is up.
  start_retest();
}
