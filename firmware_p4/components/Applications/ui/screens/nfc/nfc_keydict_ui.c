// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nfc_keydict_ui.h"

#include <ctype.h>
#include <stdio.h>

#include "keyboard_ui.h"
#include "lvgl.h"
#include "mf_key_dict.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define VISIBLE_KEYS 5

static lv_obj_t *s_screen;
static lv_obj_t *s_count_label;
static lv_obj_t *s_rows[VISIBLE_KEYS];
static lv_obj_t *s_keys[VISIBLE_KEYS];
static lv_obj_t *s_indexes[VISIBLE_KEYS];
static int s_count;
static int s_selected;

static int hex_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  c = (char)toupper((unsigned char)c);
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool parse_key(const char *text, uint8_t key[6]) {
  int nibble_count = 0;
  uint8_t nibbles[12];
  if (text == NULL)
    return false;
  for (const char *p = text; *p != '\0'; p++) {
    const int value = hex_nibble(*p);
    if (value >= 0) {
      if (nibble_count >= 12)
        return false;
      nibbles[nibble_count++] = (uint8_t)value;
    } else if (!isspace((unsigned char)*p) && *p != ':' && *p != '-') {
      return false;
    }
  }
  if (nibble_count != 12)
    return false;
  for (int i = 0; i < 6; i++)
    key[i] = (uint8_t)((nibbles[i * 2] << 4) | nibbles[i * 2 + 1]);
  return true;
}

static void refresh(void) {
  char count[40];
  snprintf(count, sizeof(count), "%d real keys loaded", s_count);
  lv_label_set_text(s_count_label, count);
  int first = s_selected - VISIBLE_KEYS / 2;
  if (first < 0)
    first = 0;
  if (first > s_count - VISIBLE_KEYS)
    first = s_count > VISIBLE_KEYS ? s_count - VISIBLE_KEYS : 0;

  for (int row = 0; row < VISIBLE_KEYS; row++) {
    const int index = first + row;
    const bool valid = index < s_count;
    const bool selected = valid && index == s_selected;
    char index_text[12];
    char key_text[28];
    if (valid) {
      uint8_t key[6] = {0};
      mf_key_dict_get(index, key);
      snprintf(index_text, sizeof(index_text), "#%03d", index);
      snprintf(key_text,
               sizeof(key_text),
               "%02X %02X %02X %02X %02X %02X",
               key[0],
               key[1],
               key[2],
               key[3],
               key[4],
               key[5]);
    } else {
      index_text[0] = '\0';
      key_text[0] = '\0';
    }
    lv_label_set_text(s_indexes[row], index_text);
    lv_label_set_text(s_keys[row], key_text);
    lv_obj_set_style_border_color(
        s_rows[row], selected ? current_theme.border_accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(s_rows[row], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        s_rows[row], selected ? current_theme.bg_primary : current_theme.bg_secondary, 0);
    lv_obj_set_style_text_color(
        s_keys[row], selected ? current_theme.border_accent : current_theme.text_main, 0);
  }
}

static void add_key_submit(const char *text, void *ctx) {
  (void)ctx;
  uint8_t key[6];
  if (!parse_key(text, key)) {
    notify(NOTIFY_WARNING, "Enter exactly 12 hexadecimal digits");
    return;
  }
  const bool existed = mf_key_dict_contains(key);
  hb_nfc_err_t err = mf_key_dict_add(key);
  if (err == HB_NFC_OK) {
    s_count = mf_key_dict_count();
    if (!existed)
      s_selected = s_count - 1;
    refresh();
    notify(NOTIFY_SAVED, existed ? "Key already in dictionary" : "Key saved to user dictionary");
  } else {
    notify(NOTIFY_WARNING, "Could not persist key to SD card");
  }
}

static void input_cb(const input_event_t *event, void *ctx) {
  (void)ctx;
  const bool press = event->action == INPUT_ACTION_PRESS;
  const bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if ((event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_LEFT) && press) {
    ui_switch_screen(SCREEN_NFC_MENU);
  } else if (event->button == INPUT_BTN_DOWN && nav && s_count > 0) {
    s_selected = (s_selected + 1) % s_count;
    refresh();
    ui_feedback(UI_FB_NAV);
  } else if (event->button == INPUT_BTN_UP && nav && s_count > 0) {
    s_selected = (s_selected - 1 + s_count) % s_count;
    refresh();
    ui_feedback(UI_FB_NAV);
  } else if (event->button == INPUT_BTN_OK && press) {
    keyboard_open(NULL, add_key_submit, NULL);
  }
}

void ui_nfc_keydict_open(void) {
  mf_key_dict_init();
  s_count = mf_key_dict_count();
  s_selected = 0;
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  ui_chrome_header(s_screen, "KEY DICTIONARY", "/assets/icons/key.bin");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(body, lv_pct(100), 176);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 8, 0);
  lv_obj_set_style_pad_row(body, 4, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  s_count_label = lv_label_create(body);
  lv_obj_set_style_text_font(s_count_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_count_label, current_theme.border_accent, 0);

  for (int i = 0; i < VISIBLE_KEYS; i++) {
    s_rows[i] = lv_obj_create(body);
    lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_rows[i], lv_pct(100), 25);
    lv_obj_set_style_radius(s_rows[i], 7, 0);
    lv_obj_set_style_bg_color(s_rows[i], current_theme.bg_secondary, 0);
    lv_obj_set_style_bg_opa(s_rows[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_rows[i], 1, 0);
    lv_obj_set_style_pad_hor(s_rows[i], 6, 0);
    lv_obj_set_style_pad_ver(s_rows[i], 0, 0);
    lv_obj_set_flex_flow(s_rows[i], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        s_rows[i], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_indexes[i] = lv_label_create(s_rows[i]);
    lv_obj_set_style_text_font(s_indexes[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_indexes[i], current_theme.text_secondary, 0);
    s_keys[i] = lv_label_create(s_rows[i]);
    lv_obj_set_style_text_font(s_keys[i], &lv_font_montserrat_12, 0);
  }
  refresh();
  ui_chrome_footer(s_screen, "UP/DOWN browse   OK add key   BACK");
  ui_input_set_screen_handler(input_cb, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
}
