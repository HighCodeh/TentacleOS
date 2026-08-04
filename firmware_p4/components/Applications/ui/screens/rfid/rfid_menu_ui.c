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

#include "st7789.h"

#include "assets_manager.h"
#include "button_ui.h"
#include "buttons_gpio.h"
#include "capture_result_ui.h"
#include "keyboard_ui.h"
#include "menu_component_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "page_dots_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "RFID_UI";

#define NAV_TIMER_MS 50
#define REVEAL_MS    3000

#define SIG_GREEN  0x00E676
#define COL_DIM    0x8A8594
#define COL_DANGER 0xFF5470

#define LIST_TITLE_ICON "/assets/icons/contactless.bin"
#define CARD_ICON       "/assets/icons/badge.bin"
#define RADAR_ICON      "/assets/icons/sensors.bin"
#define FILE_ICON       "/assets/icons/description.bin"
#define EMULATE_ICON    "/assets/icons/contactless.bin"
#define WRITE_ICON      "/assets/icons/edit.bin"
#define SAVED_ICON      "/assets/icons/bookmarks.bin"
#define COPY_ICON       "/assets/icons/content_copy.bin"

#define FADE_IN_MS 200

#define SCAN_MS      2600
#define DOT_CYCLE_MS 350

#define STATUS_Y 50
#define FREQ_Y   68

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

#define CARD_H_WIDE  162
#define CARD_Y_READ  8
#define SAVED_CARD_Y (-28)
#define SAVED_ACT_Y  (-24)

#define HINT_SAVED "UP/DOWN choose   OK open   BACK exit"

#define SAVED_CF_CARD_W     210
#define SAVED_CF_CARD_H     122
#define SAVED_CF_PEEK       60
#define SAVED_CF_RADIUS     14
#define SAVED_CF_SEL_BORDER 3
#define SAVED_CF_GLOW_W     14
#define SAVED_CF_SCROLL_OFS 6
#define SAVED_CF_DOTS_Y     (-28)
#define SAVED_CF_TOP        0x3A1170
#define SAVED_CF_BOT        0x140230
#define SAVED_CF_EDGE       0xB060FF
#define SAVED_CF_CHIP_TOP   0xD9A521
#define SAVED_CF_CHIP_BOT   0xF4D36B
#define SAVED_CF_CHIP_LINE  0x7A5A10
#define SAVED_DETAIL_BUF    48

#define WIEGAND_FC_SHIFT 16
#define WIEGAND_FC_MASK  0xFF
#define WIEGAND_CN_MASK  0xFFFF

#define WIEGAND_ROWS 3
#define WIEGAND_Y0   91
#define WIEGAND_STEP 15

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
#define TX_DOT_GAP      4
#define TX_DOT_COUNT    3
#define TX_DOT_BLINK_MS 400
#define TX_DOT_Y_OFS    -30

#define EMU_TX_LABEL_Y  -46
#define EMU_TX_BLINK_MS 600
#define STATUS_BLINK_LO LV_OPA_40

#define CLONE_REVEAL_MS 1200
#define CLONE_WRITE_MS  2200
#define CLONE_BAR_Y_OFS 50

#define READ_STATUS_BUSY "Reading"
#define EMU_TX_BUSY      "Transmitting"

#define HINT_SCAN "BACK  Cancel"
#define HINT_SHOW "BACK  Exit"
#define HINT_MENU "UP/DOWN choose   OK do   BACK exit"
#define HINT_ADD  "OK edit   L/R change   BACK exit"

#define CARD_TITLE    "EM4100"
#define CARD_SUBTITLE "Low-Frequency 125 kHz"
#define CARD_LINE     "UID  1A 2B 3C 4D 55"
#define CARD_META     "64-bit  ·  Read-only"

static const char *const WIEGAND_LINES[] = {
    "Format:  HID 26-bit",
    "Facility:  123",
    "Card:  45678",
};

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
#define IDX_CLONE   3
#define IDX_ADD     4

typedef struct {
  char name[24];
  char proto[16];
  char uid[24];
  char bits[28];
} rfid_card_t;

static const rfid_card_t SAVED_CARDS[] = {
    {"Office_Badge", "EM4100", "1A 2B 3C 4D 55", "64-bit  ·  Read-only"},
    {"Garage_Fob", "HIDProx", "20 06 EC 0C 86", "44-bit  ·  Read-only"},
    {"Gym_Tag", "Indala", "A0 00 1C FE 49", "64-bit  ·  Read-only"},
    {"Locker_03", "EM4100", "09 FB 2D 77 11", "64-bit  ·  Read-only"},
};
#define SAVED_CARD_COUNT ((int)(sizeof(SAVED_CARDS) / sizeof(SAVED_CARDS[0])))
#define RFID_MAX_CARDS   16

static rfid_card_t s_cards[RFID_MAX_CARDS];
static int s_card_count = 0;
static bool s_store_ready = false;

static const char *ADD_PROTOS[] = {"EM4100", "HIDProx", "Indala", "T5577"};
#define ADD_PROTO_COUNT ((int)(sizeof(ADD_PROTOS) / sizeof(ADD_PROTOS[0])))
static const char *ADD_BITS[] = {"44-bit", "64-bit", "128-bit"};
#define ADD_BITS_COUNT ((int)(sizeof(ADD_BITS) / sizeof(ADD_BITS[0])))

typedef enum {
  VIEW_LIST = 0,
  VIEW_READ,
  VIEW_OPTIONS,
  VIEW_SAVED,
  VIEW_SAVED_INFO,
  VIEW_EMULATE,
  VIEW_CLONE,
  VIEW_ADD,
  VIEW_COUNT
} rfid_view_t;

enum { CL_READ, CL_REVEAL, CL_WRITE, CL_DONE };

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static rfid_view_t s_view = VIEW_LIST;
static int s_saved_sel = 0;
static int s_pending_view = -1;

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

static button_ui_t s_info_emulate;
static button_ui_t s_info_delete;
static int s_info_sel = 0;

static lv_obj_t *s_fob[RFID_MAX_CARDS];
static lv_obj_t *s_fob_name[RFID_MAX_CARDS];
static lv_obj_t *s_fob_id[RFID_MAX_CARDS];
static lv_obj_t *s_saved_cont = NULL;
static page_dots_t s_dots;
static bool s_has_dots = false;

static char s_emu_title[24];
static char s_emu_sub[28];
static char s_emu_line[40];
static char s_emu_meta[28];
static char s_emu_freq[40];

static int s_clone_stage = CL_READ;
static uint32_t s_clone_start = 0;
static lv_obj_t *s_clone_card = NULL;
static lv_obj_t *s_clone_freq = NULL;
static lv_obj_t *s_clone_bar = NULL;

static int s_add_proto = 0;
static int s_add_bits = 1;
static char s_add_uid[24] = "";

static bool s_up_last = false;
static bool s_down_last = false;
static bool s_left_last = false;
static bool s_right_last = false;
static bool s_ok_last = false;
static bool s_back_last = false;

static void nav_timer_cb(lv_timer_t *t);
static void build_screen(void);

static void store_init(void) {
  if (s_store_ready)
    return;
  s_card_count = 0;
  for (int i = 0; i < SAVED_CARD_COUNT && s_card_count < RFID_MAX_CARDS; i++)
    s_cards[s_card_count++] = SAVED_CARDS[i];
  s_store_ready = true;
}

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
                                 bool assemble,
                                 bool wiegand) {
  lv_color_t accent = current_theme.border_accent;
  uint32_t delay = assemble ? FIELD_STAGGER_MS : 0;

  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, CARD_W, wiegand ? CARD_H_WIDE : CARD_H);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_pad_all(card, CARD_PAD, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
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
  lv_obj_set_style_bg_color(badge, current_theme.bg_primary, 0);
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

  if (wiegand) {
    for (int i = 0; i < WIEGAND_ROWS; i++) {
      lv_obj_t *w = lv_label_create(card);
      lv_label_set_text(w, WIEGAND_LINES[i]);
      lv_obj_set_style_text_color(w, lv_color_hex(COL_DIM), 0);
      lv_obj_set_style_text_font(w, &lv_font_montserrat_12, 0);
      lv_obj_align(w, LV_ALIGN_TOP_LEFT, 0, WIEGAND_Y0 + i * WIEGAND_STEP);
      if (assemble)
        fade_in(w, FIELD_FADE_MS, delay * (5 + i));
    }
  }

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

static void status_dots(lv_obj_t *lbl, const char *base, uint32_t elapsed) {
  int dots = (elapsed / DOT_CYCLE_MS) % 4;
  char buf[28];
  snprintf(buf,
           sizeof(buf),
           "%s%s",
           base,
           dots == 1   ? "."
           : dots == 2 ? ".."
           : dots == 3 ? "..."
                       : "");
  lv_label_set_text(lbl, buf);
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

  lv_obj_t *card =
      build_data_card(s_screen, CARD_TITLE, CARD_SUBTITLE, CARD_LINE, CARD_META, true, true);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_READ);
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
  ui_chrome_header(s_screen, "READ", RADAR_ICON);

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
  lv_label_set_text(freq, "125 kHz LF");
  lv_obj_set_style_text_color(freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(freq, &lv_font_montserrat_12, 0);
  lv_obj_align(freq, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  build_scan_field();

  s_hint = ui_chrome_footer(s_screen, HINT_SCAN);

  s_scan_timer = lv_timer_create(scan_done_cb, SCAN_MS, NULL);
  lv_timer_set_repeat_count(s_scan_timer, 1);
}

static void build_saved_empty(void) {
  ui_chrome_header(s_screen, "SAVED CARDS", SAVED_ICON);

  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, 200, 104);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_style_radius(card, 13, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 16, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, -4, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_image_dsc_t *dsc = assets_get(SAVED_ICON);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  }
  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, "No saved cards");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);
  lv_obj_t *s = lv_label_create(card);
  lv_label_set_text(s, "Read or add a tag first");
  lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s, lv_color_hex(COL_DIM), 0);

  s_hint = ui_chrome_footer(s_screen, HINT_SHOW);
}

static void uid_wiegand(const char *uid, int *fc, int *cn) {
  uint32_t v = 0;
  for (const char *c = uid; c != NULL && *c != '\0'; c++) {
    int nib = -1;
    if (*c >= '0' && *c <= '9')
      nib = *c - '0';
    else if (*c >= 'a' && *c <= 'f')
      nib = 10 + (*c - 'a');
    else if (*c >= 'A' && *c <= 'F')
      nib = 10 + (*c - 'A');
    if (nib < 0)
      continue;
    v = (v << 4) | (uint32_t)nib;
  }
  *fc = (int)((v >> WIEGAND_FC_SHIFT) & WIEGAND_FC_MASK);
  *cn = (int)(v & WIEGAND_CN_MASK);
}

static void saved_row_detail(const rfid_card_t *c, char *out, size_t n) {
  int fc = 0;
  int cn = 0;
  uid_wiegand(c->uid, &fc, &cn);
  snprintf(out, n, "%s  ·  FC %d  CN %d", c->proto, fc, cn);
}

static void saved_apply_selection(void) {
  lv_color_t accent = current_theme.border_accent;
  lv_color_t edge = lv_color_hex(SAVED_CF_EDGE);
  for (int i = 0; i < s_card_count; i++) {
    lv_obj_t *f = s_fob[i];
    int y = i * SAVED_CF_PEEK;
    if (i > s_saved_sel)
      y += (SAVED_CF_CARD_H - SAVED_CF_PEEK);
    lv_obj_align(f, LV_ALIGN_TOP_MID, 0, y);

    bool sel = (i == s_saved_sel);
    if (sel) {
      lv_obj_set_style_border_width(f, SAVED_CF_SEL_BORDER, 0);
      lv_obj_set_style_border_color(f, accent, 0);
      lv_obj_set_style_shadow_color(f, accent, 0);
      lv_obj_set_style_shadow_width(f, SAVED_CF_GLOW_W, 0);
      lv_obj_set_style_shadow_opa(f, LV_OPA_50, 0);
      lv_obj_set_style_shadow_spread(f, -3, 0);
    } else {
      lv_obj_set_style_border_width(f, 1, 0);
      lv_obj_set_style_border_color(f, edge, 0);
      lv_obj_set_style_shadow_width(f, 0, 0);
      lv_obj_set_style_shadow_opa(f, LV_OPA_TRANSP, 0);
    }
  }

  int sy = s_saved_sel * SAVED_CF_PEEK - SAVED_CF_SCROLL_OFS;
  if (sy < 0)
    sy = 0;
  if (s_saved_cont != NULL)
    lv_obj_scroll_to_y(s_saved_cont, sy, LV_ANIM_OFF);
  if (s_has_dots)
    page_dots_set(&s_dots, s_saved_sel);
}

static void build_saved_card(int i) {
  lv_color_t edge = lv_color_hex(SAVED_CF_EDGE);
  lv_color_t text = lv_color_white();

  lv_obj_t *card = lv_obj_create(s_saved_cont);
  s_fob[i] = card;
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, SAVED_CF_CARD_W, SAVED_CF_CARD_H);
  lv_obj_set_style_radius(card, SAVED_CF_RADIUS, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(SAVED_CF_TOP), 0);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(SAVED_CF_BOT), 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, edge, 0);
  lv_obj_set_style_shadow_color(card, edge, 0);
  lv_obj_set_style_shadow_width(card, 10, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);

  lv_obj_t *chip = lv_obj_create(card);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, 30, 22);
  lv_obj_align(chip, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(chip, 5, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(SAVED_CF_CHIP_TOP), 0);
  lv_obj_set_style_bg_grad_color(chip, lv_color_hex(SAVED_CF_CHIP_BOT), 0);
  lv_obj_set_style_bg_grad_dir(chip, LV_GRAD_DIR_VER, 0);
  for (int k = 0; k < 2; k++) {
    lv_obj_t *ln = lv_obj_create(chip);
    lv_obj_remove_flag(ln, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ln, 26, 1);
    lv_obj_align(ln, LV_ALIGN_CENTER, 0, k == 0 ? -5 : 5);
    lv_obj_set_style_border_width(ln, 0, 0);
    lv_obj_set_style_radius(ln, 0, 0);
    lv_obj_set_style_bg_color(ln, lv_color_hex(SAVED_CF_CHIP_LINE), 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_70, 0);
  }

  lv_obj_t *name = lv_label_create(card);
  s_fob_name[i] = name;
  lv_obj_set_width(name, lv_pct(100));
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_label_set_text(name, s_cards[i].name);
  lv_obj_set_style_text_color(name, text, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 30);

  lv_obj_t *sub = lv_label_create(card);
  lv_obj_set_width(sub, lv_pct(100));
  lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
  lv_label_set_text(sub, s_cards[i].proto);
  lv_obj_set_style_text_color(sub, text, 0);
  lv_obj_set_style_text_opa(sub, LV_OPA_60, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
  lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 0, 49);

  lv_obj_t *uidl = lv_label_create(card);
  s_fob_id[i] = uidl;
  lv_obj_set_width(uidl, lv_pct(100));
  lv_label_set_long_mode(uidl, LV_LABEL_LONG_DOT);
  lv_label_set_text_fmt(uidl, "UID  %s", s_cards[i].uid);
  lv_obj_set_style_text_color(uidl, edge, 0);
  lv_obj_set_style_text_font(uidl, &lv_font_montserrat_12, 0);
  lv_obj_align(uidl, LV_ALIGN_TOP_LEFT, 0, 66);

  char detail[SAVED_DETAIL_BUF];
  saved_row_detail(&s_cards[i], detail, sizeof(detail));
  lv_obj_t *meta = lv_label_create(card);
  lv_obj_set_width(meta, lv_pct(100));
  lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
  lv_label_set_text(meta, detail);
  lv_obj_set_style_text_color(meta, text, 0);
  lv_obj_set_style_text_opa(meta, LV_OPA_50, 0);
  lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
  lv_obj_align(meta, LV_ALIGN_TOP_LEFT, 0, 83);
}

static void build_saved_list(void) {
  if (s_card_count <= 0) {
    build_saved_empty();
    return;
  }
  ui_chrome_header(s_screen, "SAVED CARDS", SAVED_ICON);

  if (s_saved_sel < 0)
    s_saved_sel = 0;
  if (s_saved_sel >= s_card_count)
    s_saved_sel = s_card_count - 1;

  s_saved_cont = lv_obj_create(s_screen);
  lv_obj_set_size(s_saved_cont, lv_pct(100), LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_saved_cont, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_saved_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_saved_cont, 0, 0);
  lv_obj_set_style_pad_all(s_saved_cont, 0, 0);
  lv_obj_set_scrollbar_mode(s_saved_cont, LV_SCROLLBAR_MODE_OFF);

  for (int i = 0; i < s_card_count; i++)
    build_saved_card(i);

  lv_obj_update_layout(s_screen);
  saved_apply_selection();

  s_dots = page_dots_create(s_screen, s_card_count, LV_ALIGN_BOTTOM_MID, 0, SAVED_CF_DOTS_Y);
  s_has_dots = true;
  page_dots_set(&s_dots, s_saved_sel);

  s_hint = ui_chrome_footer(s_screen, HINT_SAVED);
}

static void info_update_selection(void) {
  button_ui_set_selected(&s_info_emulate, s_info_sel == 0);
  button_ui_set_selected(&s_info_delete, s_info_sel == 1);
}

static void build_saved_info(void) {
  ui_chrome_header(s_screen, "CARD INFO", CARD_ICON);

  char line[40];
  snprintf(line, sizeof(line), "UID  %s", s_cards[s_saved_sel].uid);

  lv_obj_t *card = build_data_card(s_screen,
                                   s_cards[s_saved_sel].name,
                                   s_cards[s_saved_sel].proto,
                                   line,
                                   s_cards[s_saved_sel].bits,
                                   true,
                                   true);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, SAVED_CARD_Y);
  card_rise(card);

  lv_obj_t *actions = lv_obj_create(s_screen);
  lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(actions, 210, LV_SIZE_CONTENT);
  lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, SAVED_ACT_Y);
  lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actions, 0, 0);
  lv_obj_set_style_pad_all(actions, 0, 0);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(actions, 8, 0);

  lv_color_t danger = lv_color_hex(COL_DANGER);
  s_info_emulate = button_ui_create(actions, 200, 34, "Emulate", EMULATE_ICON, NULL);
  s_info_delete = button_ui_create(actions, 200, 34, "Delete", NULL, &danger);
  s_info_sel = 0;
  info_update_selection();

  s_hint = ui_chrome_footer(s_screen, HINT_MENU);
}

static void seed_emulate_default(void) {
  snprintf(s_emu_title, sizeof(s_emu_title), "%s", CARD_TITLE);
  snprintf(s_emu_sub, sizeof(s_emu_sub), "%s", CARD_SUBTITLE);
  snprintf(s_emu_line, sizeof(s_emu_line), "%s", CARD_LINE);
  snprintf(s_emu_meta, sizeof(s_emu_meta), "%s", CARD_META);
  snprintf(s_emu_freq, sizeof(s_emu_freq), "EM4100  ·  125 kHz LF");
}

static void seed_emulate_from_card(const rfid_card_t *c) {
  snprintf(s_emu_title, sizeof(s_emu_title), "%s", c->name);
  snprintf(s_emu_sub, sizeof(s_emu_sub), "%s", c->proto);
  snprintf(s_emu_line, sizeof(s_emu_line), "UID  %s", c->uid);
  snprintf(s_emu_meta, sizeof(s_emu_meta), "%s", c->bits);
  snprintf(s_emu_freq, sizeof(s_emu_freq), "%s  ·  125 kHz LF", c->proto);
}

static void emu_tick(void) {
  if (s_status_lbl == NULL)
    return;
  status_dots(s_status_lbl, "Emulating", lv_tick_get() - s_scan_start);
}

static void build_emulate(void) {
  ui_chrome_header(s_screen, "EMULATE", EMULATE_ICON);

  s_scan_start = lv_tick_get();

  s_status_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_status_lbl, "Emulating");
  lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(SIG_GREEN), 0);
  lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  lv_obj_t *freq = lv_label_create(s_screen);
  lv_label_set_text(freq, s_emu_freq);
  lv_obj_set_style_text_color(freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(freq, &lv_font_montserrat_12, 0);
  lv_obj_align(freq, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, -6, NULL, NULL);

  lv_obj_t *card =
      build_data_card(s_screen, s_emu_title, s_emu_sub, s_emu_line, s_emu_meta, false, false);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 2);

  lv_obj_t *tx = lv_label_create(s_screen);
  lv_label_set_text(tx, EMU_TX_BUSY);
  lv_obj_set_style_text_color(tx, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(tx, &lv_font_montserrat_12, 0);
  lv_obj_align(tx, LV_ALIGN_BOTTOM_MID, 0, EMU_TX_LABEL_Y);
  blink_loop(tx, STATUS_BLINK_LO, EMU_TX_BLINK_MS);

  int total_w = TX_DOT_COUNT * TX_DOT_SIZE + (TX_DOT_COUNT - 1) * TX_DOT_GAP;
  int x0 = -(total_w / 2) + TX_DOT_SIZE / 2;
  for (int i = 0; i < TX_DOT_COUNT; i++) {
    lv_obj_t *dot = lv_obj_create(s_screen);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, TX_DOT_SIZE, TX_DOT_SIZE);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, x0 + i * (TX_DOT_SIZE + TX_DOT_GAP), TX_DOT_Y_OFS);
    lv_obj_set_style_radius(dot, TX_DOT_SIZE / 2, 0);
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

static void build_clone(void) {
  ui_chrome_header(s_screen, "CLONE", COPY_ICON);

  s_clone_stage = CL_READ;
  s_clone_card = NULL;
  s_clone_bar = NULL;
  s_scan_start = lv_tick_get();
  s_hex_last = s_scan_start;
  s_clone_start = s_scan_start;

  s_status_lbl = lv_label_create(s_screen);
  lv_label_set_text(s_status_lbl, "Reading source");
  lv_obj_set_style_text_color(s_status_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  s_clone_freq = lv_label_create(s_screen);
  lv_label_set_text(s_clone_freq, "Present source tag");
  lv_obj_set_style_text_color(s_clone_freq, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_clone_freq, &lv_font_montserrat_12, 0);
  lv_obj_align(s_clone_freq, LV_ALIGN_TOP_MID, 0, FREQ_Y);

  build_scan_field();

  s_hint = ui_chrome_footer(s_screen, HINT_SCAN);
}

static void clone_begin_write(void) {
  if (s_clone_card != NULL) {
    lv_obj_del(s_clone_card);
    s_clone_card = NULL;
  }
  if (s_clone_freq != NULL) {
    lv_obj_del(s_clone_freq);
    s_clone_freq = NULL;
  }
  if (s_status_lbl != NULL) {
    lv_label_set_text(s_status_lbl, "Writing copy");
    lv_obj_set_style_text_color(s_status_lbl, current_theme.text_main, 0);
  }

  s_waves = waves_create(s_screen, LV_ALIGN_CENTER, 0, -6, NULL, WRITE_ICON);

  s_clone_bar = lv_bar_create(s_screen);
  lv_obj_set_size(s_clone_bar, PROG_W, PROG_H);
  lv_obj_align(s_clone_bar, LV_ALIGN_CENTER, 0, CLONE_BAR_Y_OFS);
  lv_bar_set_range(s_clone_bar, 0, 100);
  lv_bar_set_value(s_clone_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_clone_bar, current_theme.bg_secondary, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_clone_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_clone_bar, PROG_H / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_clone_bar, current_theme.border_accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_clone_bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s_clone_bar, PROG_H / 2, LV_PART_INDICATOR);

  lv_anim_t pa;
  lv_anim_init(&pa);
  lv_anim_set_var(&pa, s_clone_bar);
  lv_anim_set_exec_cb(&pa, bar_value_cb);
  lv_anim_set_values(&pa, 0, 100);
  lv_anim_set_duration(&pa, CLONE_WRITE_MS);
  lv_anim_set_path_cb(&pa, lv_anim_path_linear);
  lv_anim_start(&pa);
}

static void clone_tick(void) {
  uint32_t now = lv_tick_get();
  uint32_t el = now - s_clone_start;

  if (s_clone_stage == CL_READ) {
    if (s_status_lbl != NULL)
      status_dots(s_status_lbl, "Reading source", now - s_scan_start);
    if (s_hex_lbl != NULL && (now - s_hex_last) >= HEX_UPDATE_MS) {
      s_hex_last = now;
      char hex[HEX_BUF_LEN];
      scramble_hex(hex, sizeof(hex), now);
      lv_label_set_text(s_hex_lbl, hex);
    }
    if (el >= SCAN_MS) {
      if (s_scan_group != NULL) {
        lv_obj_del(s_scan_group);
        s_scan_group = NULL;
        s_hex_lbl = NULL;
      }
      if (s_clone_freq != NULL) {
        lv_obj_del(s_clone_freq);
        s_clone_freq = NULL;
      }
      if (s_status_lbl != NULL) {
        lv_label_set_text(s_status_lbl, "Source captured!");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(SIG_GREEN), 0);
      }
      s_clone_card =
          build_data_card(s_screen, CARD_TITLE, CARD_SUBTITLE, CARD_LINE, CARD_META, true, false);
      lv_obj_align(s_clone_card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
      card_rise(s_clone_card);
      ui_feedback(UI_FB_READ);
      ESP_LOGI(TAG, "mock rfid clone source: %s %s", CARD_TITLE, CARD_LINE);
      s_clone_stage = CL_REVEAL;
      s_clone_start = now;
    }
  } else if (s_clone_stage == CL_REVEAL) {
    if (el >= CLONE_REVEAL_MS) {
      clone_begin_write();
      s_clone_stage = CL_WRITE;
      s_clone_start = now;
    }
  } else if (s_clone_stage == CL_WRITE) {
    if (s_status_lbl != NULL)
      status_dots(s_status_lbl, "Writing copy", el);
    if (el >= CLONE_WRITE_MS) {
      if (s_status_lbl != NULL) {
        lv_label_set_text(s_status_lbl, "Cloned!");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(SIG_GREEN), 0);
      }
      ui_feedback(UI_FB_WRITE);
      ESP_LOGI(TAG, "mock rfid clone written: %s", CARD_TITLE);
      notify(NOTIFY_SAVED, "Tag cloned");
      msgbox_open_info(CARD_ICON,
                       "Clone complete",
                       "Source tag written to a blank card",
                       current_theme.border_accent);
      s_clone_stage = CL_DONE;
      s_pending_view = VIEW_LIST;
    }
  }
}

static void format_uid(const char *text, char *out, size_t n) {
  static const char H[] = "0123456789ABCDEF";
  size_t p = 0;
  int nib = 0;
  for (const char *c = text; c != NULL && *c != '\0'; c++) {
    int v = -1;
    if (*c >= '0' && *c <= '9')
      v = *c - '0';
    else if (*c >= 'a' && *c <= 'f')
      v = 10 + (*c - 'a');
    else if (*c >= 'A' && *c <= 'F')
      v = 10 + (*c - 'A');
    if (v < 0)
      continue;
    if (nib > 0 && (nib % 2) == 0) {
      if (p + 1 >= n)
        break;
      out[p++] = ' ';
    }
    if (p + 1 >= n || nib >= HEX_BYTES * 2)
      break;
    out[p++] = H[v];
    nib++;
  }
  out[p] = '\0';
}

static void add_uid_submit(const char *text, void *ud) {
  (void)ud;
  format_uid(text, s_add_uid, sizeof(s_add_uid));
  menu_component_set_selector_value(&s_menu, 1, s_add_uid[0] ? s_add_uid : "—");
}

static void build_add_manual(void) {
  s_menu = menu_component_create(s_screen, "ADD MANUALLY", WRITE_ICON);
  menu_component_add_selector(&s_menu, CARD_ICON, "Protocol", ADD_PROTOS[s_add_proto]);
  menu_component_add_selector(&s_menu, COPY_ICON, "UID", s_add_uid[0] ? s_add_uid : "—");
  menu_component_add_selector(&s_menu, FILE_ICON, "Bits", ADD_BITS[s_add_bits]);
  menu_component_add_item(&s_menu, SAVED_ICON, "Save card");
  menu_component_set_hint(&s_menu, HINT_ADD);
  menu_component_select(&s_menu, 0);

  fade_in(s_menu.items_cont, FADE_IN_MS, 0);
  fade_in(s_menu.title_bar, FADE_IN_MS, 0);
}

static void add_manual_commit(void) {
  if (!s_add_uid[0]) {
    notify(NOTIFY_WARNING, "Enter a UID first");
    return;
  }
  if (s_card_count >= RFID_MAX_CARDS) {
    notify(NOTIFY_WARNING, "Library full");
    return;
  }
  rfid_card_t *c = &s_cards[s_card_count];
  snprintf(c->name, sizeof(c->name), "Manual_%02d", s_card_count + 1);
  snprintf(c->proto, sizeof(c->proto), "%s", ADD_PROTOS[s_add_proto]);
  snprintf(c->uid, sizeof(c->uid), "%s", s_add_uid);
  snprintf(c->bits, sizeof(c->bits), "%s  ·  Read-only", ADD_BITS[s_add_bits]);
  s_saved_sel = s_card_count;
  s_card_count++;

  ui_feedback(UI_FB_WRITE);
  ESP_LOGI(TAG, "mock rfid manual add: %s %s", c->name, c->uid);
  notify(NOTIFY_SAVED, "Card added");
  msgbox_open_info(CARD_ICON, "Saved", "Card added to library", current_theme.border_accent);
  s_pending_view = VIEW_SAVED;
}

static void on_saved_delete_confirm(bool confirm) {
  if (!confirm)
    return;
  for (int i = s_saved_sel; i < s_card_count - 1; i++)
    s_cards[i] = s_cards[i + 1];
  if (s_card_count > 0)
    s_card_count--;
  if (s_saved_sel >= s_card_count)
    s_saved_sel = s_card_count > 0 ? s_card_count - 1 : 0;

  ui_feedback(UI_FB_WRITE);
  notify(NOTIFY_SAVED, "Card deleted");

  s_pending_view = VIEW_SAVED;
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
  s_clone_card = NULL;
  s_clone_freq = NULL;
  s_clone_bar = NULL;
  s_saved_cont = NULL;
  s_has_dots = false;
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
      ui_chrome_header(s_screen, "READ", RADAR_ICON);
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
    case VIEW_CLONE:
      build_clone();
      break;
    case VIEW_ADD:
      build_add_manual();
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
    if (s_status_lbl != NULL)
      status_dots(s_status_lbl, READ_STATUS_BUSY, now - s_scan_start);
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

  bool up = ui_btn_up();
  bool down = ui_btn_down();
  bool left = ui_btn_left();
  bool right = ui_btn_right();
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();

  if (s_pending_view >= 0) {
    if (!msgbox_is_open()) {
      rfid_view_t v = (rfid_view_t)s_pending_view;
      s_pending_view = -1;
      s_view = v;
      build_screen();
    }
    goto latch;
  }

  if (msgbox_is_open() || keyboard_is_open() || ui_input_is_locked())
    goto latch;

  if (s_view == VIEW_READ)
    read_tick();
  else if (s_view == VIEW_EMULATE)
    emu_tick();
  else if (s_view == VIEW_CLONE) {
    clone_tick();
    if (s_pending_view >= 0)
      goto latch;
  }

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
          seed_emulate_default();
          s_view = VIEW_EMULATE;
          build_screen();
          goto latch;
        } else if (sel == IDX_CLONE) {
          s_view = VIEW_CLONE;
          build_screen();
          goto latch;
        } else if (sel == IDX_ADD) {
          s_add_proto = 0;
          s_add_bits = 1;
          s_add_uid[0] = '\0';
          s_view = VIEW_ADD;
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
            seed_emulate_default();
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
      if (((down && !s_down_last) || (right && !s_right_last)) && s_saved_sel < s_card_count - 1) {
        s_saved_sel++;
        saved_apply_selection();
        ui_feedback(UI_FB_NAV);
      }
      if (((up && !s_up_last) || (left && !s_left_last)) && s_saved_sel > 0) {
        s_saved_sel--;
        saved_apply_selection();
        ui_feedback(UI_FB_NAV);
      }
      if (ok && !s_ok_last && s_card_count > 0) {
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
      if (down && !s_down_last && s_info_sel < 1) {
        s_info_sel++;
        info_update_selection();
        ui_feedback(UI_FB_NAV);
      }
      if (up && !s_up_last && s_info_sel > 0) {
        s_info_sel--;
        info_update_selection();
        ui_feedback(UI_FB_NAV);
      }
      if (ok && !s_ok_last) {
        ui_feedback(UI_FB_SELECT);
        if (s_info_sel == 0) {
          seed_emulate_from_card(&s_cards[s_saved_sel]);
          s_view = VIEW_EMULATE;
          build_screen();
          goto latch;
        } else {
          char msg[40];
          snprintf(msg, sizeof(msg), "Delete %s?", s_cards[s_saved_sel].name);
          msgbox_open(LV_SYMBOL_TRASH, msg, "Delete", "Cancel", on_saved_delete_confirm);
        }
      }
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

    case VIEW_CLONE:
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;

    case VIEW_ADD: {
      if (down && !s_down_last) {
        menu_component_next(&s_menu);
        ui_feedback(UI_FB_NAV);
      }
      if (up && !s_up_last) {
        menu_component_prev(&s_menu);
        ui_feedback(UI_FB_NAV);
      }
      int sel = menu_component_get_selected(&s_menu);
      if (right && !s_right_last) {
        if (sel == 0) {
          s_add_proto = (s_add_proto + 1) % ADD_PROTO_COUNT;
          menu_component_set_selector_value(&s_menu, 0, ADD_PROTOS[s_add_proto]);
          ui_feedback(UI_FB_NAV);
        } else if (sel == 2) {
          s_add_bits = (s_add_bits + 1) % ADD_BITS_COUNT;
          menu_component_set_selector_value(&s_menu, 2, ADD_BITS[s_add_bits]);
          ui_feedback(UI_FB_NAV);
        }
      }
      if (left && !s_left_last) {
        if (sel == 0) {
          s_add_proto = (s_add_proto + ADD_PROTO_COUNT - 1) % ADD_PROTO_COUNT;
          menu_component_set_selector_value(&s_menu, 0, ADD_PROTOS[s_add_proto]);
          ui_feedback(UI_FB_NAV);
        } else if (sel == 2) {
          s_add_bits = (s_add_bits + ADD_BITS_COUNT - 1) % ADD_BITS_COUNT;
          menu_component_set_selector_value(&s_menu, 2, ADD_BITS[s_add_bits]);
          ui_feedback(UI_FB_NAV);
        }
      }
      if (ok && !s_ok_last) {
        if (sel == 1) {
          ui_feedback(UI_FB_SELECT);
          keyboard_open(NULL, add_uid_submit, NULL);
        } else if (sel == 3) {
          ui_feedback(UI_FB_SELECT);
          add_manual_commit();
          if (s_pending_view >= 0)
            goto latch;
        }
      }
      if (back && !s_back_last) {
        s_view = VIEW_LIST;
        build_screen();
        goto latch;
      }
      break;
    }

    default:
      break;
  }

latch:
  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_rfid_menu_open(void) {
  store_init();
  stop_scan_timers();
  s_view = VIEW_LIST;
  s_saved_sel = 0;
  s_pending_view = -1;
  s_card_revealed = false;
  s_saved = false;
  s_up_last = s_down_last = s_left_last = s_right_last = s_ok_last = s_back_last = false;
  build_screen();
}
