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

#include "nfc_read_ui.h"

#include <stdio.h>

#include "esp_random.h"
#include "lvgl.h"

#include "buttons_gpio.h"
#include "capture_result_ui.h"
#include "keyboard_ui.h"
#include "notify_ui.h"
#include "ui_feedback.h"
#include "msgbox_ui.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 33
#define REVEAL_MS    3000

enum { ST_SCAN, ST_FOUND, ST_OPTIONS };

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_card_panel = NULL;
static nfc_ui_field_t s_field;
static lv_timer_t *s_timer = NULL;
static capture_result_t s_cr = {0};

static int s_state = ST_SCAN;
static uint32_t s_scan_start = 0;
static uint32_t s_scan_deadline = 1800;
static uint32_t s_found_at = 0;
static nfc_sim_card_t s_card;

static bool s_ok_last, s_back_last, s_right_last, s_up_last, s_down_last;

static void show_rings(bool show) {
  for (int i = 0; i < 3; i++) {
    if (!s_field.ring[i])
      continue;
    if (show)
      lv_obj_remove_flag(s_field.ring[i], LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_field.ring[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void begin_scan(void) {
  s_state = ST_SCAN;
  s_scan_start = lv_tick_get();
  s_scan_deadline = 1500 + (esp_random() % 1400);
  if (s_card_panel) {
    lv_obj_del(s_card_panel);
    s_card_panel = NULL;
  }
  capture_result_destroy(&s_cr);
  show_rings(true);
  lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_label_set_text(s_status, "Searching");
  ui_chrome_footer_set_text(s_hint, "BACK  Exit");
}

static void reveal(void) {
  s_state = ST_FOUND;
  s_found_at = lv_tick_get();
  nfc_sim_random_card(&s_card);
  show_rings(false);
  s_card_panel = nfc_ui_card_panel(s_screen, &s_card);
  lv_obj_align(s_card_panel, LV_ALIGN_CENTER, 0, 10);
  lv_obj_fade_in(s_card_panel, 280, 0);
  lv_obj_set_style_text_color(s_status, lv_color_hex(0x00E676), 0);
  lv_label_set_text(s_status, "Tag found!");
  nfc_ui_play_sound(NFC_SND_FOUND);
  ui_chrome_footer_set_text(s_hint, "BACK  Exit");
}

static void show_options(void) {
  if (s_card_panel) {
    lv_obj_del(s_card_panel);
    s_card_panel = NULL;
  }
  if (s_status)
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);

  static char uidbuf[40];
  char uid[24];
  nfc_sim_format_uid(&s_card, uid, sizeof(uid));
  snprintf(uidbuf, sizeof(uidbuf), "UID %s", uid);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = "/assets/icons/nfc_icon.bin",
      .card_title = "Tag captured",
      .card_sub = s_card.type,
      .card_value = uidbuf,
      .primary_label = "Emulate",
      .again_label = "Read again",
  };
  s_cr = capture_result_create(s_screen, &cfg);
  s_state = ST_OPTIONS;
  ui_chrome_footer_set_text(s_hint, "UP/DOWN choose   OK do   BACK exit");
}

static void on_name_submit(const char *text, void *ud) {
  (void)ud;
  const char *nm = (text && text[0]) ? text : s_card.type;
  snprintf(s_card.name, NFC_SIM_NAME_LEN, "%.*s", NFC_SIM_NAME_LEN - 1, nm);
  bool saved = nfc_sim_add(&s_card);
  if (saved) {
    nfc_ui_play_sound(NFC_SND_SAVE);
    capture_result_mark_saved(&s_cr);
    notify(NOTIFY_SAVED, "Tag saved to library");
  } else {
    notify(NOTIFY_WARNING, "Library full");
  }
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  bool ok = ok_button_is_down();
  bool back = back_button_is_down();
  bool right = ui_btn_right();
  bool up = ui_btn_up();
  bool down = ui_btn_down();

  if (msgbox_is_open() || keyboard_is_open() || ui_input_is_locked()) {
    s_ok_last = ok;
    s_back_last = back;
    s_right_last = right;
    s_up_last = up;
    s_down_last = down;
    return;
  }
  if (back && !s_back_last) {
    ui_switch_screen(SCREEN_NFC_MENU);
    return;
  }

  if (s_state == ST_SCAN) {
    uint32_t el = lv_tick_get() - s_scan_start;
    nfc_ui_field_tick(&s_field, el);
    int dots = (el / 350) % 4;
    char buf[20];
    snprintf(buf,
             sizeof(buf),
             "Searching%s",
             dots == 1   ? "."
             : dots == 2 ? ".."
             : dots == 3 ? "..."
                         : "");
    lv_label_set_text(s_status, buf);
    if (el >= s_scan_deadline)
      reveal();
  } else if (s_state == ST_FOUND) {
    if (lv_tick_get() - s_found_at >= REVEAL_MS)
      show_options();
  } else {
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
          ui_switch_screen(SCREEN_NFC_EMULATE);
          return;
        case CAP_ACT_SAVE:
          keyboard_open(NULL, on_name_submit, NULL);
          break;
        case CAP_ACT_AGAIN:
          begin_scan();
          break;
        case CAP_ACT_DISCARD:
          ui_switch_screen(SCREEN_NFC_MENU);
          return;
        default:
          break;
      }
    }
  }

  s_ok_last = ok;
  s_back_last = back;
  s_right_last = right;
  s_up_last = up;
  s_down_last = down;
}

void ui_nfc_read_open(void) {
  nfc_sim_init();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_card_panel = NULL;
  s_cr = (capture_result_t){0};
  s_found_at = 0;
  s_ok_last = s_back_last = s_right_last = s_up_last = s_down_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "READ TAG", "/assets/icons/nfc_icon.bin");
  nfc_ui_field_create(&s_field, s_screen, ui_theme_get_accent());

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, "Searching");
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 52);

  s_hint = ui_chrome_footer(s_screen, "BACK  Exit");

  begin_scan();

  if (s_timer == NULL)
    s_timer = lv_timer_create(tick_cb, NAV_TIMER_MS, NULL);

  ui_screen_load(s_screen);
}
