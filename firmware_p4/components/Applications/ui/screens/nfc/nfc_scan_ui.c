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

#include "nfc_scan_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NFC_ICON "/assets/icons/contactless.bin"

#define SIG_GREEN 0x00E676
#define COL_DIM   0x8A8594
#define COL_RAISE 0x170A28

#define SCAN_STEP_MS 480

#define STATUS_Y 50

#define CARD_W           214
#define CARD_Y_OFS       8
#define CARD_RADIUS      14
#define CARD_PAD         12
#define CARD_ROW_GAP     7
#define CARD_BORDER      1
#define CARD_GLOW_W      16
#define CARD_GLOW_SPREAD -4

#define ROW_H         26
#define ROW_RADIUS    8
#define ROW_PAD_HOR   6
#define ROW_GROUP_GAP 9

#define DOT_SZ     9
#define DOT_RADIUS 5

#define STATUS_SCAN "Polling field"
#define STATUS_DONE "Identify complete"

#define HINT_SCAN "Identifying..."
#define HINT_DONE "BACK  Exit"

#define TITLE_TEXT "Tag technologies"

#define VAL_QUEUED "queued"
#define VAL_CHECK  "checking..."
#define VAL_ABSENT "not present"

#define UID_BUF   16
#define VALUE_BUF 40

#define UID_HEAD_BYTE 0x04

typedef enum {
  ROW_PENDING = 0,
  ROW_CHECKING,
  ROW_PRESENT,
  ROW_ABSENT,
} row_state_t;

static const struct {
  const char *tech;
  const char *iso;
  bool present;
} TECHS[] = {
    {"NFC-A", "ISO14443A", true},
    {"NFC-B", "ISO14443B", false},
    {"NFC-F", "FeliCa", false},
    {"NFC-V", "ISO15693", false},
};
#define TECH_COUNT ((int)(sizeof(TECHS) / sizeof(TECHS[0])))

static lv_obj_t *s_screen = NULL;
static lv_timer_t *s_scan_timer = NULL;

static lv_obj_t *s_status = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_summary = NULL;
static lv_obj_t *s_row[TECH_COUNT];
static lv_obj_t *s_dot[TECH_COUNT];
static lv_obj_t *s_value[TECH_COUNT];

static char s_present_value[VALUE_BUF];
static int s_cursor = 0;

static void scan_tick_cb(lv_timer_t *t);

static void stop_timer(lv_timer_t **t) {
  if (*t != NULL) {
    lv_timer_delete(*t);
    *t = NULL;
  }
}

static void build_uid(void) {
  uint32_t r = esp_random();
  char uid[UID_BUF];
  snprintf(uid,
           sizeof(uid),
           "%02X:%02X:%02X:%02X",
           UID_HEAD_BYTE,
           (unsigned)(r & 0xFF),
           (unsigned)((r >> 8) & 0xFF),
           (unsigned)((r >> 16) & 0xFF));
  snprintf(s_present_value, sizeof(s_present_value), "%s  %s", TECHS[0].iso, uid);
}

static void set_row_state(int i, row_state_t state) {
  if (s_dot[i] == NULL || s_value[i] == NULL || s_row[i] == NULL)
    return;

  lv_color_t dot_color = lv_color_hex(COL_DIM);
  lv_color_t txt_color = lv_color_hex(COL_DIM);
  const char *txt = VAL_QUEUED;
  bool raise = false;

  switch (state) {
    case ROW_CHECKING:
      dot_color = current_theme.border_accent;
      txt_color = current_theme.border_accent;
      txt = VAL_CHECK;
      raise = true;
      break;
    case ROW_PRESENT:
      dot_color = lv_color_hex(SIG_GREEN);
      txt_color = lv_color_hex(SIG_GREEN);
      txt = s_present_value;
      break;
    case ROW_ABSENT:
      dot_color = lv_color_hex(COL_DIM);
      txt_color = lv_color_hex(COL_DIM);
      txt = VAL_ABSENT;
      break;
    case ROW_PENDING:
    default:
      break;
  }

  lv_obj_set_style_bg_color(s_row[i], lv_color_hex(COL_RAISE), 0);
  lv_obj_set_style_bg_opa(s_row[i], raise ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(s_dot[i], dot_color, 0);
  lv_label_set_text(s_value[i], txt);
  lv_obj_set_style_text_color(s_value[i], txt_color, 0);
}

static void build_card(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(card, CARD_W);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(card, CARD_BORDER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, CARD_GLOW_W, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(card, CARD_GLOW_SPREAD, 0);
  lv_obj_set_style_pad_all(card, CARD_PAD, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, CARD_ROW_GAP, 0);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, TITLE_TEXT);
  lv_obj_set_style_text_color(title, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);

  for (int i = 0; i < TECH_COUNT; i++) {
    lv_obj_t *row = lv_obj_create(card);
    s_row[i] = row;
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, ROW_H);
    lv_obj_set_style_radius(row, ROW_RADIUS, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_style_pad_hor(row, ROW_PAD_HOR, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *group = lv_obj_create(row);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(group, ROW_GROUP_GAP, 0);

    lv_obj_t *dot = lv_obj_create(group);
    s_dot[i] = dot;
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, DOT_SZ, DOT_SZ);
    lv_obj_set_style_radius(dot, DOT_RADIUS, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(COL_DIM), 0);

    lv_obj_t *tech = lv_label_create(group);
    lv_label_set_text(tech, TECHS[i].tech);
    lv_obj_set_style_text_color(tech, current_theme.text_main, 0);
    lv_obj_set_style_text_font(tech, &lv_font_montserrat_14, 0);

    lv_obj_t *value = lv_label_create(row);
    s_value[i] = value;
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(value, 1);
    lv_label_set_text(value, VAL_QUEUED);
    lv_obj_set_style_text_color(value, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
  }

  s_summary = lv_label_create(card);
  lv_label_set_text(s_summary, "");
  lv_obj_set_style_text_color(s_summary, current_theme.border_inactive, 0);
  lv_obj_set_style_text_font(s_summary, &lv_font_montserrat_12, 0);
}

static void resolve_row(int i) {
  if (TECHS[i].present) {
    set_row_state(i, ROW_PRESENT);
    ui_feedback(UI_FB_READ);
  } else {
    set_row_state(i, ROW_ABSENT);
  }
}

static void finish_scan(void) {
  int present = 0;
  for (int i = 0; i < TECH_COUNT; i++) {
    if (TECHS[i].present)
      present++;
  }
  if (s_summary != NULL) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d of %d present", present, TECH_COUNT);
    lv_label_set_text(s_summary, buf);
    lv_obj_set_style_text_color(
        s_summary, present > 0 ? lv_color_hex(SIG_GREEN) : lv_color_hex(COL_DIM), 0);
  }
  if (s_status != NULL) {
    lv_label_set_text(s_status, STATUS_DONE);
    lv_obj_set_style_text_color(s_status, lv_color_hex(SIG_GREEN), 0);
  }
  if (s_hint != NULL)
    ui_chrome_footer_set_text(s_hint, HINT_DONE);
}

static void scan_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_scan_timer = NULL;
    return;
  }
  if (s_cursor > 0)
    resolve_row(s_cursor - 1);

  if (s_cursor < TECH_COUNT) {
    set_row_state(s_cursor, ROW_CHECKING);
    s_cursor++;
  } else {
    finish_scan();
    lv_timer_delete(t);
    s_scan_timer = NULL;
  }
}

static void nfc_scan_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    default:
      break;
  }
}

void ui_nfc_scan_open(void) {
  stop_timer(&s_scan_timer);
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_status = NULL;
  s_hint = NULL;
  s_summary = NULL;
  for (int i = 0; i < TECH_COUNT; i++) {
    s_row[i] = NULL;
    s_dot[i] = NULL;
    s_value[i] = NULL;
  }
  s_cursor = 0;
  build_uid();

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "IDENTIFY", NFC_ICON);

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, STATUS_SCAN);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, STATUS_Y);

  build_card();

  s_hint = ui_chrome_footer(s_screen, HINT_SCAN);

  s_scan_timer = lv_timer_create(scan_tick_cb, SCAN_STEP_MS, NULL);
  ui_input_set_screen_handler(nfc_scan_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
