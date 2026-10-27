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

#include "nfc_p2p_ui.h"

#include <stdio.h>

#include "lvgl.h"

#include "nfc_p2p_service.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define REFRESH_MS 100

static lv_obj_t *s_screen;
static lv_obj_t *s_payload;
static lv_obj_t *s_peer;
static lv_obj_t *s_link;
static lv_obj_t *s_transfer;
static lv_obj_t *s_status;
static lv_obj_t *s_footer;
static lv_timer_t *s_timer;
static nfc_p2p_payload_t s_payload_kind = NFC_P2P_PAYLOAD_TEXT;
static nfc_p2p_state_t s_last_state = (nfc_p2p_state_t)-1;

static lv_obj_t *make_value_row(lv_obj_t *parent, const char *name, lv_obj_t **value) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, 30);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(
      row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *label = lv_label_create(row);
  lv_label_set_text(label, name);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, current_theme.text_secondary, 0);

  *value = lv_label_create(row);
  lv_label_set_text(*value, "Idle");
  lv_obj_set_style_text_font(*value, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(*value, current_theme.text_main, 0);
  return row;
}

static void set_value(lv_obj_t *label, const char *text, bool good) {
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(
      label, good ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_main, 0);
}

static void render_status(const nfc_p2p_status_t *st) {
  char buf[64];
  switch (st->state) {
    case NFC_P2P_STATE_IDLE:
      set_value(s_peer, "Not started", false);
      set_value(s_link, "Idle", false);
      set_value(s_transfer, "Ready", false);
      lv_label_set_text(s_status, "Choose payload and press OK");
      break;
    case NFC_P2P_STATE_INITIALIZING:
      set_value(s_peer, "Starting radio", false);
      set_value(s_link, "Idle", false);
      set_value(s_transfer, "Waiting", false);
      lv_label_set_text(s_status, "Initialising ST25R3916...");
      break;
    case NFC_P2P_STATE_SEARCHING:
      snprintf(buf, sizeof(buf), "Searching (%u)", (unsigned)st->attempts);
      set_value(s_peer, buf, false);
      set_value(s_link, "ATR_REQ", false);
      set_value(s_transfer, "Waiting", false);
      lv_label_set_text(s_status, "Hold an NFC-DEP peer nearby");
      break;
    case NFC_P2P_STATE_LLCP_CONNECTED:
      set_value(s_peer, "NFC-DEP found", true);
      snprintf(buf, sizeof(buf), "LLCP MIU %u", (unsigned)st->negotiated_miu);
      set_value(s_link, buf, true);
      set_value(s_transfer, "Preparing", false);
      lv_label_set_text(s_status, "LLCP negotiated");
      break;
    case NFC_P2P_STATE_SNEP_SENDING:
      set_value(s_peer, "NFC-DEP found", true);
      snprintf(buf, sizeof(buf), "LLCP MIU %u", (unsigned)st->negotiated_miu);
      set_value(s_link, buf, true);
      snprintf(buf, sizeof(buf), "SNEP PUT %u B", (unsigned)st->ndef_len);
      set_value(s_transfer, buf, false);
      lv_label_set_text(s_status, "Sending real NDEF message...");
      break;
    case NFC_P2P_STATE_DONE:
      set_value(s_peer, "NFC-DEP found", true);
      set_value(s_link, "LLCP active", true);
      snprintf(buf, sizeof(buf), "Success 0x%02X", st->snep_response);
      set_value(s_transfer, buf, true);
      lv_label_set_text(s_status, "NDEF delivered by SNEP");
      break;
    case NFC_P2P_STATE_CANCELLED:
      set_value(s_transfer, "Cancelled", false);
      lv_label_set_text(s_status, "Transfer cancelled");
      break;
    case NFC_P2P_STATE_ERROR:
    default:
      snprintf(buf, sizeof(buf), "Error 0x%X", (unsigned)st->error);
      set_value(s_transfer, buf, false);
      lv_obj_set_style_text_color(s_transfer, lv_color_hex(UI_COL_DANGER), 0);
      lv_label_set_text(s_status,
                        st->error == HB_NFC_ERR_NO_CARD
                            ? "No NFC-DEP/LLCP peer found"
                            : "Radio or protocol error; press OK to retry");
      break;
  }
  lv_label_set_text(s_payload, nfc_p2p_payload_name(s_payload_kind));
  ui_chrome_footer_set_text(s_footer,
                            st->running ? "BACK cancel" : "UP/DOWN payload   OK send   BACK");
}

static void refresh_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen)
    return;
  nfc_p2p_status_t status;
  nfc_p2p_service_get_status(&status);
  if (status.state != s_last_state || status.state == NFC_P2P_STATE_SEARCHING) {
    s_last_state = status.state;
    render_status(&status);
  }
}

static void nfc_p2p_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (ev->action != INPUT_ACTION_PRESS)
    return;
  nfc_p2p_status_t status;
  nfc_p2p_service_get_status(&status);

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (status.running) {
        nfc_p2p_service_cancel();
        lv_label_set_text(s_status, "Cancelling...");
      } else {
        ui_switch_screen(SCREEN_NFC_MENU);
      }
      break;
    case INPUT_BTN_UP:
    case INPUT_BTN_DOWN:
      if (!status.running) {
        s_payload_kind = (nfc_p2p_payload_t)((s_payload_kind + 1) % NFC_P2P_PAYLOAD_COUNT);
        lv_label_set_text(s_payload, nfc_p2p_payload_name(s_payload_kind));
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (!status.running) {
        ui_feedback(UI_FB_SELECT);
        if (nfc_p2p_service_start(s_payload_kind) != HB_NFC_OK)
          lv_label_set_text(s_status, "Could not start P2P task");
      }
      break;
    default:
      break;
  }
}

void ui_nfc_p2p_stop(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  nfc_p2p_service_stop();
}

void ui_nfc_p2p_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_last_state = (nfc_p2p_state_t)-1;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "SHARE (P2P)", "/assets/icons/nfc.bin");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(
      body, ui_screen_w() - 16, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 8, 0);
  lv_obj_set_style_pad_row(body, 3, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  s_payload = lv_label_create(body);
  lv_label_set_text(s_payload, nfc_p2p_payload_name(s_payload_kind));
  lv_obj_set_width(s_payload, lv_pct(100));
  lv_obj_set_style_text_font(s_payload, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_payload, current_theme.border_accent, 0);
  lv_obj_set_style_text_align(s_payload, LV_TEXT_ALIGN_CENTER, 0);

  make_value_row(body, "Peer", &s_peer);
  make_value_row(body, "Link", &s_link);
  make_value_row(body, "Transfer", &s_transfer);

  s_status = lv_label_create(body);
  lv_label_set_text(s_status, "Choose payload and press OK");
  lv_obj_set_width(s_status, lv_pct(100));
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);

  s_footer = ui_chrome_footer(s_screen, "UP/DOWN payload   OK send   BACK");
  ui_input_set_screen_handler(nfc_p2p_input, NULL);
  s_timer = lv_timer_create(refresh_cb, REFRESH_MS, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  nfc_p2p_status_t status;
  nfc_p2p_service_get_status(&status);
  render_status(&status);
}
