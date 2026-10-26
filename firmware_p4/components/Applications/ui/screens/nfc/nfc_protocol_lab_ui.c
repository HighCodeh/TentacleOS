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

#include "nfc_protocol_lab_ui.h"

#include <stdio.h>

#include "lvgl.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define REFRESH_MS 100

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_heading;
static lv_obj_t *s_lines[NFC_PROTOCOL_RESULT_LINES];
static lv_obj_t *s_footer;
static lv_timer_t *s_timer;
static nfc_protocol_op_t s_operation;
static nfc_protocol_state_t s_last_state = (nfc_protocol_state_t)-1;

static void clear_result(void) {
  lv_label_set_text(s_heading, "Live RF result");
  for (int i = 0; i < NFC_PROTOCOL_RESULT_LINES; i++)
    lv_label_set_text(s_lines[i], "");
}

static void render(const nfc_protocol_status_t *status) {
  char text[72];
  if (status->state == NFC_PROTOCOL_STATE_RUNNING) {
    clear_result();
    lv_label_set_text(s_status, "Reading physical tag...");
    lv_obj_set_style_text_color(s_status, current_theme.border_accent, 0);
    ui_chrome_footer_set_text(s_footer, "Keep tag still   BACK cancel");
    return;
  }
  if (status->state == NFC_PROTOCOL_STATE_DONE) {
    lv_label_set_text(s_status, "Read complete");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_label_set_text(s_heading, status->result.heading[0] ? status->result.heading : "Result");
    for (int i = 0; i < NFC_PROTOCOL_RESULT_LINES; i++)
      lv_label_set_text(s_lines[i], i < status->result.line_count ? status->result.lines[i] : "");
    ui_feedback(UI_FB_READ);
  } else if (status->state == NFC_PROTOCOL_STATE_CANCELLED) {
    lv_label_set_text(s_status, "Read cancelled");
    lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);
  } else if (status->state == NFC_PROTOCOL_STATE_ERROR) {
    if (status->error == HB_NFC_ERR_NO_CARD || status->error == HB_NFC_ERR_NOT_FOUND)
      snprintf(
          text, sizeof(text), "No compatible %s tag", nfc_protocol_operation_name(s_operation));
    else
      snprintf(text, sizeof(text), "Read error 0x%X", (unsigned)status->error);
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_DANGER), 0);
    clear_result();
    lv_label_set_text(s_lines[0], "Only live RF responses are shown.");
  } else {
    lv_label_set_text(s_status, "Ready");
  }
  ui_chrome_footer_set_text(s_footer, "OK rescan   BACK exit");
}

static void timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen)
    return;
  nfc_protocol_status_t status;
  nfc_protocol_service_get_status(&status);
  if (status.state != s_last_state) {
    s_last_state = status.state;
    render(&status);
  }
}

static void start_read(void) {
  nfc_protocol_status_t status;
  nfc_protocol_service_get_status(&status);
  if (status.running)
    return;
  s_last_state = (nfc_protocol_state_t)-1;
  if (nfc_protocol_service_start(s_operation) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Could not start NFC task");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_DANGER), 0);
  }
}

static void input_cb(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (event->action != INPUT_ACTION_PRESS)
    return;
  nfc_protocol_status_t status;
  nfc_protocol_service_get_status(&status);
  if (event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_LEFT) {
    if (status.running)
      nfc_protocol_service_cancel();
    else
      ui_switch_screen(SCREEN_NFC_MENU);
  } else if (event->button == INPUT_BTN_OK && !status.running) {
    ui_feedback(UI_FB_SELECT);
    start_read();
  }
}

void ui_nfc_protocol_lab_stop(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  nfc_protocol_service_stop();
}

void ui_nfc_protocol_lab_open(nfc_protocol_op_t operation, const char *title) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_operation = operation;
  s_last_state = (nfc_protocol_state_t)-1;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  ui_chrome_header(s_screen, title, "/assets/icons/nfc.bin");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(
      body, ui_screen_w() - 16, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 6, 0);
  lv_obj_set_style_pad_row(body, 3, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  s_status = lv_label_create(body);
  lv_obj_set_width(s_status, lv_pct(100));
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);

  lv_obj_t *box = lv_obj_create(body);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(box, 9, 0);
  lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(box, 7, 0);
  lv_obj_set_style_pad_row(box, 2, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

  s_heading = lv_label_create(box);
  lv_obj_set_style_text_font(s_heading, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_heading, current_theme.border_accent, 0);
  for (int i = 0; i < NFC_PROTOCOL_RESULT_LINES; i++) {
    s_lines[i] = lv_label_create(box);
    lv_obj_set_width(s_lines[i], lv_pct(100));
    lv_label_set_long_mode(s_lines[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_lines[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lines[i], current_theme.text_main, 0);
  }
  clear_result();

  s_footer = ui_chrome_footer(s_screen, "Keep tag still   BACK cancel");
  ui_input_set_screen_handler(input_cb, NULL);
  s_timer = lv_timer_create(timer_cb, REFRESH_MS, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  start_read();
}
