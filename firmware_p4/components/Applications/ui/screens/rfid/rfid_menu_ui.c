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

#include "rfid_menu_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "lvgl.h"

#include "assets_manager.h"
#include "buttons_gpio.h"
#include "capture_result_ui.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "RFID_UI";

#define NAV_TIMER_MS 50
#define REVEAL_MS    3000

#define SIG_GREEN 0x00E676

#define HEADER_TITLE_Y     10
#define HEADER_RULE_Y      32
#define HEADER_RULE_W      70
#define HEADER_RULE_H      2
#define HEADER_RULE_RADIUS 1

#define LIST_TITLE_ICON "/assets/icons/card_icon.bin"
#define CARD_ICON       "/assets/icons/card_icon.bin"
#define RADAR_ICON      "/assets/icons/radar_icon.bin"
#define FILE_ICON       "/assets/icons/file_icon.bin"
#define EMULATE_ICON    "/assets/icons/emulate_icon.bin"
#define WRITE_ICON      "/assets/icons/write_icon.bin"
#define SAVED_ICON      "/assets/icons/saved_icon.bin"
#define COPY_ICON       "/assets/icons/copy_icon.bin"

#define FADE_IN_MS 200

#define SCAN_MS      2600
#define DOT_CYCLE_MS 350

#define STATUS_Y  50
#define FREQ_Y    68
#define FREQ_TEXT "125 kHz LF"

#define WAVES_Y 10

#define PAD_W       154
#define PAD_H       86
#define PAD_RADIUS  12
#define PAD_BORDER  2
#define PAD_Y_OFS   -12
#define PAD_DIM_OPA LV_OPA_50

#define SIL_W      96
#define SIL_H      58
#define SIL_RADIUS 8
#define SIL_OPA    LV_OPA_20

#define BEAM_W      (PAD_W - PAD_BORDER * 2)
#define BEAM_H      3
#define BEAM_MARGIN 8
#define BEAM_TRAVEL (PAD_H - PAD_BORDER * 2 - BEAM_MARGIN * 2 - BEAM_H)
#define BEAM_MS     820
#define BEAM_GLOW_W 10

#define HEX_BYTES     6
#define HEX_BUF_LEN   24
#define HEX_Y_OFS     46
#define HEX_UPDATE_MS 80

#define PROG_W     154
#define PROG_H     4
#define PROG_Y_OFS 66

#define CARD_W        210
#define CARD_H        120
#define CARD_RADIUS   14
#define CARD_PAD      12
#define CARD_BORDER   1
#define CARD_SHADOW_W 12
#define CARD_Y_OFS    18
#define CARD_RISE_PX  26
#define CARD_RISE_MS  300

#define FIELD_FADE_MS    220
#define FIELD_STAGGER_MS 70

#define BADGE_W       30
#define BADGE_H       24
#define BADGE_RADIUS  6
#define BADGE_ICON_PX 16

#define CARD_TITLE_X 38
#define CARD_TITLE_Y 1
#define CARD_SUB_X   38
#define CARD_SUB_Y   21
#define CARD_LINE_Y  52
#define CARD_META_Y  74

#define COIL_COUNT  3
#define COIL_W      42
#define COIL_H      2
#define COIL_RADIUS 1
#define COIL_GAP    5
#define COIL_OPA    LV_OPA_40
#define COIL_X_OFS  -6
#define COIL_Y_OFS  -6

#define TX_DOT_SIZE     12
#define TX_DOT_RADIUS   6
#define TX_DOT_GAP      4
#define TX_DOT_COUNT    3
#define TX_DOT_BLINK_MS 400
#define TX_DOT_Y_OFS    54

#define EMU_TX_LABEL_Y  78
#define EMU_TX_BLINK_MS 600
#define STATUS_BLINK_LO LV_OPA_40

#define HINT_Y_OFS -6

#define READ_STATUS_BUSY "Reading"
#define EMU_TX_BUSY      "Transmitting"

#define HINT_SCAN "BACK  Cancel"
#define HINT_SHOW "BACK  Exit"
#define HINT_MENU "UP/DOWN choose   OK do   BACK exit"

#define CARD_TITLE    "EM4100"
#define CARD_SUBTITLE "Low-Frequency 125 kHz"
#define CARD_LINE     "UID  1A 2B 3C 4D 55"
#define CARD_META     "64-bit  ·  Read-only"

static const struct {
  const char *name;
  const char *icon;
} RFID_ITEMS[] = {
    {"Read", RADAR_ICON},
    {"Saved", SAVED_ICON},
    {"Emulate", EMULATE_ICON},
    {"Clone", COPY_ICON},
    {"Add Manually", WRITE_ICON},
};
#define RFID_ITEM_COUNT ((int)(sizeof(RFID_ITEMS) / sizeof(RFID_ITEMS[0])))

#define IDX_READ    0
#define IDX_SAVED   1
#define IDX_EMULATE 2

static const struct {
  const char *name;
  const char *proto;
  const char *uid;
  const char *bits;
} SAVED_CARDS[] = {
    {"Office_Badge", "EM4100", "1A 2B 3C 4D 55", "64-bit  ·  Read-only"},
    {"Garage_Fob", "HIDProx", "20 06 EC 0C 86", "44-bit  ·  Read-only"},
    {"Gym_Tag", "Indala", "A0 00 1C FE 49", "64-bit  ·  Read-only"},
    {"Locker_03", "EM4100", "09 FB 2D 77 11", "64-bit  ·  Read-only"},
};
#define SAVED_CARD_COUNT ((int)(sizeof(SAVED_CARDS) / sizeof(SAVED_CARDS[0])))

typedef enum {
  VIEW_LIST = 0,
  VIEW_READ,
  VIEW_OPTIONS,
  VIEW_SAVED,
  VIEW_SAVED_INFO,
  VIEW_EMULATE,
  VIEW_COUNT
} rfid_view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static rfid_view_t s_view = VIEW_LIST;
static int s_saved_sel = 0;

static lv_timer_t *s_nav_timer = NULL;
static lv_timer_t *s_scan_timer = NULL;

static lv_obj_t *s_status_lbl = NULL;
static lv_obj_t *s_scan_group = NULL;
static lv_obj_t *s_hex_lbl = NULL;
static lv_obj_t *s_waves = NULL;
static lv_obj_t *s_hint = NULL;
static uint32_t s_scan_start = 0;
static uint32_t s_hex_last = 0;
static bool s_card_revealed = false;
static bool s_saved = false;
static capture_result_t s_cr = {0};
static uint32_t s_revealed_at = 0;

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_right_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);

static void stop_scan_timers(void) {
  if (s_scan_timer != NULL) {
    lv_timer_delete(s_scan_timer);
    s_scan_timer = NULL;
  }
}

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void translate_y_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void bar_value_cb(void *var, int32_t v) {
  lv_bar_set_value((lv_obj_t *)var, v, LV_ANIM_OFF);
}

static void fade_in(lv_obj_t *obj, uint32_t duration_ms, uint32_t delay_ms) {
  lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, duration_ms);
  lv_anim_set_delay(&a, delay_ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void blink_loop(lv_obj_t *obj, lv_opa_t low, uint32_t half_ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, opa_cb);
  lv_anim_set_values(&a, low, LV_OPA_COVER);
  lv_anim_set_duration(&a, half_ms);
  lv_anim_set_playback_duration(&a, half_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static void build_header(const char *text) {
  lv_obj_t *title = lv_label_create(s_screen);
  lv_label_set_text(title, text);
  lv_obj_set_style_text_color(title, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, HEADER_TITLE_Y);

  lv_obj_t *rule = lv_obj_create(s_screen);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(rule, lv_pct(HEADER_RULE_W), HEADER_RULE_H);
  lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, HEADER_RULE_Y);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, HEADER_RULE_RADIUS, 0);
  lv_obj_set_style_bg_color(rule, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);
}

static lv_obj_t *make_hint(const char *text) {
  lv_obj_t *hint = lv_label_create(s_screen);
  lv_label_set_text(hint, text);
  lv_obj_set_style_text_color(hint, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(hint, LV_OPA_60, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, HINT_Y_OFS);
  return hint;
}

static void scramble_hex(char *out, size_t n, uint32_t seed) {
  static const char H[] = "0123456789ABCDEF";
  size_t p = 0;
  for (int i = 0; i < HEX_BYTES && p + 3 < n; i++) {
    seed = seed * 1103515245u + 12345u;
    uint8_t b = (uint8_t)((seed >> 16) & 0xFF);
    out[p++] = H[(b >> 4) & 0xF];
    out[p++] = H[b & 0xF];
    if (i < HEX_BYTES - 1)
      out[p++] = ' ';
  }
  out[p] = '\0';
}

static lv_obj_t *build_data_card(lv_obj_t *parent,
                                 const char *title_txt,
                                 const char *sub_txt,
                                 const char *line_txt,
                                 const char *meta_txt,
                                 bool assemble) {
  lv_color_t accent = current_theme.border_accent;
  uint32_t delay = assemble ? FIELD_STAGGER_MS : 0;

  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, CARD_W, CARD_H);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_pad_all(card, CARD_PAD, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_grad_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, CARD_BORDER, 0);
  lv_obj_set_style_border_color(card, accent, 0);
  lv_obj_set_style_shadow_color(card, accent, 0);
  lv_obj_set_style_shadow_width(card, CARD_SHADOW_W, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);

  lv_obj_t *badge = lv_obj_create(card);
  lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(badge, BADGE_W, BADGE_H);
  lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(badge, BADGE_RADIUS, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_set_style_bg_color(badge, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_border_color(badge, accent, 0);

  lv_image_dsc_t *icon = assets_get(CARD_ICON);
  if (icon != NULL) {
    lv_obj_t *img = lv_image_create(badge);
    lv_image_set_src(img, icon);
    int32_t longest = icon->header.w > icon->header.h ? icon->header.w : icon->header.h;
    if (longest > 0)
      lv_image_set_scale(img, BADGE_ICON_PX * 256 / longest);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
  }
  if (assemble)
    fade_in(badge, FIELD_FADE_MS, 0);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, title_txt);
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, CARD_TITLE_X, CARD_TITLE_Y);
  if (assemble)
    fade_in(title, FIELD_FADE_MS, delay);

  lv_obj_t *sub = lv_label_create(card);
  lv_label_set_text(sub, sub_txt);
  lv_obj_set_style_text_color(sub, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(sub, LV_OPA_60, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_align(sub, LV_ALIGN_TOP_LEFT, CARD_SUB_X, CARD_SUB_Y);
  if (assemble)
    fade_in(sub, FIELD_FADE_MS, delay * 2);

  lv_obj_t *line = lv_label_create(card);
  lv_obj_set_width(line, lv_pct(100));
  lv_label_set_long_mode(line, LV_LABEL_LONG_DOT);
  lv_label_set_text(line, line_txt);
  lv_obj_set_style_text_color(line, accent, 0);
  lv_obj_set_style_text_font(line, &lv_font_montserrat_12, 0);
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, CARD_LINE_Y);
  if (assemble)
    fade_in(line, FIELD_FADE_MS, delay * 3);

  lv_obj_t *meta = lv_label_create(card);
  lv_obj_set_width(meta, lv_pct(100));
  lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
  lv_label_set_text(meta, meta_txt);
  lv_obj_set_style_text_color(meta, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(meta, LV_OPA_50, 0);
  lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
  lv_obj_align(meta, LV_ALIGN_TOP_LEFT, 0, CARD_META_Y);
  if (assemble)
    fade_in(meta, FIELD_FADE_MS, delay * 4);

  for (int i = 0; i < COIL_COUNT; i++) {
    lv_obj_t *coil = lv_obj_create(card);
    lv_obj_remove_flag(coil, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(coil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(coil, COIL_W - i * 8, COIL_H);
    lv_obj_align(coil, LV_ALIGN_BOTTOM_RIGHT, COIL_X_OFS, COIL_Y_OFS - i * COIL_GAP);
    lv_obj_set_style_radius(coil, COIL_RADIUS, 0);
    lv_obj_set_style_border_width(coil, 0, 0);
    lv_obj_set_style_bg_color(coil, accent, 0);
    lv_obj_set_style_bg_opa(coil, COIL_OPA, 0);
    if (assemble)
      fade_in(coil, FIELD_FADE_MS, delay * 5);
  }

  return card;
}

static void card_rise(lv_obj_t *card) {
  lv_obj_set_style_translate_y(card, CARD_RISE_PX, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, card);
  lv_anim_set_exec_cb(&a, translate_y_cb);
  lv_anim_set_values(&a, CARD_RISE_PX, 0);
  lv_anim_set_duration(&a, CARD_RISE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void build_scan_field(void) {
  s_scan_group = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_scan_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_scan_group, lv_pct(100), lv_pct(100));
  lv_obj_align(s_scan_group, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(s_scan_group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_scan_group, 0, 0);
  lv_obj_set_style_pad_all(s_scan_group, 0, 0);

  lv_obj_t *pad = lv_obj_create(s_scan_group);
  lv_obj_remove_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pad, PAD_W, PAD_H);
  lv_obj_align(pad, LV_ALIGN_CENTER, 0, PAD_Y_OFS);
  lv_obj_set_style_radius(pad, PAD_RADIUS, 0);
  lv_obj_set_style_pad_all(pad, 0, 0);
  lv_obj_set_style_bg_color(pad, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(pad, PAD_BORDER, 0);
  lv_obj_set_style_border_color(pad, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(pad, PAD_DIM_OPA, 0);
  lv_obj_set_style_clip_corner(pad, true, 0);

  lv_obj_t *sil = lv_obj_create(pad);
  lv_obj_remove_flag(sil, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(sil, SIL_W, SIL_H);
  lv_obj_center(sil);
  lv_obj_set_style_radius(sil, SIL_RADIUS, 0);
  lv_obj_set_style_bg_opa(sil, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sil, 1, 0);
  lv_obj_set_style_border_color(sil, current_theme.border_accent, 0);
  lv_obj_set_style_border_opa(sil, SIL_OPA, 0);

  lv_obj_t *beam = lv_obj_create(pad);
  lv_obj_remove_flag(beam, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(beam, BEAM_W, BEAM_H);
  lv_obj_align(beam, LV_ALIGN_TOP_MID, 0, BEAM_MARGIN);
  lv_obj_set_style_radius(beam, 0, 0);
  lv_obj_set_style_border_width(beam, 0, 0);
  lv_obj_set_style_bg_color(beam, current_theme.border_accent, 0);
  lv_obj_set_style_bg_opa(beam, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_color(beam, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(beam, BEAM_GLOW_W, 0);
  lv_obj_set_style_shadow_opa(beam, LV_OPA_50, 0);

  lv_anim_t sweep;
  lv_anim_init(&sweep);
  lv_anim_set_var(&sweep, beam);
  lv_anim_set_exec_cb(&sweep, translate_y_cb);
  lv_anim_set_values(&sweep, 0, BEAM_TRAVEL);
  lv_anim_set_duration(&sweep, BEAM_MS);
  lv_anim_set_playback_duration(&sweep, BEAM_MS);
  lv_anim_set_repeat_count(&sweep, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&sweep, lv_anim_path_ease_in_out);
  lv_anim_start(&sweep);

  s_hex_lbl = lv_label_create(s_scan_group);
  lv_label_set_text(s_hex_lbl, "-- -- -- -- -- --");
  lv_obj_set_style_text_color(s_hex_lbl, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(s_hex_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_hex_lbl, LV_ALIGN_CENTER, 0, HEX_Y_OFS);

  lv_obj_t *prog = lv_bar_create(s_scan_group);
  lv_obj_set_size(prog, PROG_W, PROG_H);
  lv_obj_align(prog, LV_ALIGN_CENTER, 0, PROG_Y_OFS);
  lv_bar_set_range(prog, 0, 100);
  lv_bar_set_value(prog, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(prog, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(prog, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(prog, PROG_H / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(prog, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(prog, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(prog, PROG_H / 2, LV_PART_INDICATOR);

  lv_anim_t pa;
  lv_anim_init(&pa);
  lv_anim_set_var(&pa, prog);
  lv_anim_set_exec_cb(&pa, bar_value_cb);
  lv_anim_set_values(&pa, 0, 100);
  lv_anim_set_duration(&pa, SCAN_MS);
  lv_anim_set_path_cb(&pa, lv_anim_path_linear);
  lv_anim_start(&pa);
}

static void reveal_captured_card(void) {
  if (s_scan_group != NULL) {
    lv_obj_del(s_scan_group);
    s_scan_group = NULL;
    s_hex_lbl = NULL;
  }
  if (s_status_lbl != NULL) {
    lv_anim_delete(s_status_lbl, opa_cb);
    lv_obj_set_style_opa(s_status_lbl, LV_OPA_COVER, 0);
    lv_label_set_text(s_status_lbl, "Tag detected!");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(SIG_GREEN), 0);
  }

  lv_obj_t *card = build_data_card(s_screen, CARD_TITLE, CARD_SUBTITLE, CARD_LINE, CARD_META, true);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
  card_rise(card);
}

static void scan_done_cb(lv_timer_t *t) {
  (void)t;
  s_scan_timer = NULL;
  if (lv_screen_active() != s_screen)
    return;

  s_card_revealed = true;
  s_revealed_at = lv_tick_get();
  reveal_captured_card();
  ESP_LOGI(TAG, "mock rfid capture: %s %s", CARD_TITLE, CARD_LINE);
  ui_feedback(UI_FB_READ);

  if (s_hint != NULL)
    ui_chrome_footer_set_text(s_hint, HINT_SHOW);
}

static void build_read(void) {
  ui_chrome_header(s_screen, "READ", "/assets/icons/nfc_card_icon.bin");

  s_card_revealed = false;
  s_saved = false;
  s_scan_start = lv_tick_get();
  s_hex_last = s_scan_start;

  s_status_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_status_lbl, READ_STATUS_BUSY);
  lv_obj_set_style_text_color(s_status_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  lv_obj_t *freq = lv_label_create(s_screen);
  lv_label_set_text(freq, FREQ_TEXT);
  lv_obj_set_style_text_color(freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(freq, &lv_font_montserrat_12, 0);
  lv_obj_align(freq, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  build_scan_field();

  s_hint = ui_chrome_footer(s_screen, HINT_SCAN);

  s_scan_timer = lv_timer_create(scan_done_cb, SCAN_MS, NULL);
  lv_timer_set_repeat_count(s_scan_timer, 1);
}

static void build_saved_list(void) {
  s_menu = menu_component_create(s_screen, "SAVED CARDS", SAVED_ICON);
  for (int i = 0; i < SAVED_CARD_COUNT; i++) {
    menu_component_add_item(&s_menu, CARD_ICON, SAVED_CARDS[i].name);
    menu_component_set_item_label_color(&s_menu, i, current_theme.text_main);
  }
  if (s_saved_sel > 0 && s_saved_sel < SAVED_CARD_COUNT)
    menu_component_select(&s_menu, s_saved_sel);

  fade_in(s_menu.items_cont, FADE_IN_MS, 0);
  fade_in(s_menu.title_bar, FADE_IN_MS, 0);
}

static void build_saved_info(void) {
  build_header("CARD INFO");

  char line[40];
  snprintf(line, sizeof(line), "UID  %s", SAVED_CARDS[s_saved_sel].uid);

  lv_obj_t *card = build_data_card(s_screen,
                                   SAVED_CARDS[s_saved_sel].name,
                                   SAVED_CARDS[s_saved_sel].proto,
                                   line,
                                   SAVED_CARDS[s_saved_sel].bits,
                                   true);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
  card_rise(card);

  make_hint("BACK to return");
}

static void build_emulate(void) {
  ui_chrome_header(s_screen, "EMULATE", EMULATE_ICON);

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, WAVES_Y, NULL, CARD_ICON);
  (void)s_waves;

  lv_obj_t *card =
      build_data_card(s_screen, CARD_TITLE, CARD_SUBTITLE, CARD_LINE, CARD_META, false);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);

  lv_obj_t *tx = lv_label_create(s_screen);
  lv_label_set_text(tx, EMU_TX_BUSY);
  lv_obj_set_style_text_color(tx, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(tx, &lv_font_montserrat_12, 0);
  lv_obj_align(tx, LV_ALIGN_CENTER, 0, EMU_TX_LABEL_Y);
  blink_loop(tx, STATUS_BLINK_LO, EMU_TX_BLINK_MS);

  int total_w = TX_DOT_COUNT * TX_DOT_SIZE + (TX_DOT_COUNT - 1) * TX_DOT_GAP;
  int x0 = -(total_w / 2) + TX_DOT_SIZE / 2;
  for (int i = 0; i < TX_DOT_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(s_screen);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, TX_DOT_SIZE, TX_DOT_SIZE);
    lv_obj_align(dot, LV_ALIGN_CENTER, x0 + i * (TX_DOT_SIZE + TX_DOT_GAP), TX_DOT_Y_OFS);
    lv_obj_set_style_radius(dot, TX_DOT_RADIUS, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, current_theme.border_accent, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_values(&a, STATUS_BLINK_LO, LV_OPA_COVER);
    lv_anim_set_duration(&a, TX_DOT_BLINK_MS);
    lv_anim_set_playback_duration(&a, TX_DOT_BLINK_MS);
    lv_anim_set_delay(&a, i * TX_DOT_BLINK_MS / TX_DOT_COUNT);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }

  ui_chrome_footer(s_screen, "BACK  Stop");
  ui_feedback(UI_FB_EMULATE);
}

static void build_screen(void) {
  stop_scan_timers();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status_lbl = NULL;
  s_scan_group = NULL;
  s_hex_lbl = NULL;
  s_waves = NULL;
  s_hint = NULL;
  s_cr = (capture_result_t){0};

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  switch (s_view) {
    case VIEW_READ:
      build_read();
      break;
    case VIEW_OPTIONS: {
      ui_chrome_header(s_screen, "READ", CARD_ICON);
      capture_result_cfg_t cfg = {
          .accent = current_theme.border_accent,
          .card_icon = CARD_ICON,
          .card_title = "Tag captured",
          .card_sub = CARD_TITLE,
          .card_value = CARD_LINE,
          .primary_label = "Emulate",
          .again_label = "Read again",
      };
      s_cr = capture_result_create(s_screen, &cfg);
      s_hint = ui_chrome_footer(s_screen, HINT_MENU);
      break;
    }
    case VIEW_SAVED:
      build_saved_list();
      break;
    case VIEW_SAVED_INFO:
      build_saved_info();
      break;
    case VIEW_EMULATE:
      build_emulate();
      break;
    case VIEW_LIST:
    default:
      s_menu = menu_component_create(s_screen, "RFID", LIST_TITLE_ICON);
      for (int i = 0; i < RFID_ITEM_COUNT; i++)
        menu_component_add_item(&s_menu, RFID_ITEMS[i].icon, RFID_ITEMS[i].name);
      fade_in(s_menu.items_cont, FADE_IN_MS, 0);
      fade_in(s_menu.title_bar, FADE_IN_MS, 0);
      break;
  }

  if (s_nav_timer == NULL)
    s_nav_timer = lv_timer_create(nav_timer_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}

static void read_tick(void) {
  uint32_t now = lv_tick_get();
  if (!s_card_revealed) {
    if (s_status_lbl != NULL) {
      int dots = ((now - s_scan_start) / DOT_CYCLE_MS) % 4;
      char buf[20];
      snprintf(buf,
               sizeof(buf),
               "%s%s",
               READ_STATUS_BUSY,
               dots == 1   ? "."
               : dots == 2 ? ".."
               : dots == 3 ? "..."
                           : "");
      lv_label_set_text(s_status_lbl, buf);
    }
    if (s_hex_lbl != NULL && (now - s_hex_last) >= HEX_UPDATE_MS) {
      s_hex_last = now;
      char hex[HEX_BUF_LEN];
      scramble_hex(hex, sizeof(hex), now);
      lv_label_set_text(s_hex_lbl, hex);
    }
  }
}

static void nav_timer_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_nav_timer = NULL;
    return;
  }
  if (ui_input_is_locked())
    return;

  if (s_view == VIEW_READ)
    read_tick();

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  switch (s_view) {
    case VIEW_LIST:
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (ok && !s_ok_last) {
        int sel = menu_component_get_selected(&s_menu);
        if (sel == IDX_READ) {
          s_view = VIEW_READ;
          build_screen();
          goto latch;
        } else if (sel == IDX_SAVED) {
          s_saved_sel = 0;
          s_view = VIEW_SAVED;
          build_screen();
          goto latch;
        } else if (sel == IDX_EMULATE) {
          s_view = VIEW_EMULATE;
          build_screen();
          goto latch;
        }
      }
      if (back && !s_back_last)
        ui_switch_screen(SCREEN_MENU);
      break;

    case VIEW_READ:
      if (s_card_revealed && lv_tick_get() - s_revealed_at >= REVEAL_MS) {
        s_view = VIEW_OPTIONS;
        build_screen();
        goto latch;
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_OPTIONS:
      if (down && !s_down_last) {
        capture_result_next(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      if (up && !s_up_last) {
        capture_result_prev(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      if (ok && !s_ok_last) {
        switch (capture_result_selected(&s_cr)) {
          case CAP_ACT_PRIMARY:
            s_view = VIEW_EMULATE;
            build_screen();
            goto latch;
          case CAP_ACT_SAVE:
            if (!s_saved) {
              s_saved = true;
              capture_result_mark_saved(&s_cr);
              ESP_LOGI(TAG, "mock rfid saved: %s", CARD_TITLE);
              ui_feedback(UI_FB_WRITE);
              notify(NOTIFY_SAVED, "RFID tag saved");
            }
            break;
          case CAP_ACT_AGAIN:
            s_view = VIEW_READ;
            build_screen();
            goto latch;
          case CAP_ACT_DISCARD:
            s_view = VIEW_LIST;
            build_screen();
            goto latch;
          default:
            break;
        }
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_SAVED:
      if (down && !s_down_last)
        menu_component_next(&s_menu);
      if (up && !s_up_last)
        menu_component_prev(&s_menu);
      if (ok && !s_ok_last) {
        s_saved_sel = menu_component_get_selected(&s_menu);
        s_view = VIEW_SAVED_INFO;
        build_screen();
        goto latch;
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_SAVED_INFO:
      if (back && !s_back_last) {
        s_view = VIEW_SAVED;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_EMULATE:
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    default:
      break;
  }

  s_up_last = up;
  s_down_last = down;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
  return;

latch:
  s_up_last = up;
  s_down_last = down;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_rfid_menu_open(void) {
  stop_scan_timers();
  s_view = VIEW_LIST;
  s_saved_sel = 0;
  s_card_revealed = false;
  s_saved = false;
  build_screen();
}
