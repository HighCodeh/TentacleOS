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

#include "nfc_apdu_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "keyboard_ui.h"
#include "lvgl.h"
#include "msgbox_ui.h"
#include "nfc_apdu.h"
#include "nfc_apdu_lab.h"
#include "sd_card_init.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define APDU_UI_REFRESH_MS 100
#define APDU_UI_PREVIEW_TX 20U
#define APDU_UI_PREVIEW_RX 28U

typedef enum {
  APDU_ACTION_RAW = 0,
  APDU_ACTION_SELECT_AID,
  APDU_ACTION_SELECT_NDEF,
  APDU_ACTION_SELECT_PPSE,
  APDU_ACTION_GET_CHALLENGE,
  APDU_ACTION_TRACE,
  APDU_ACTION_CLEAR,
  APDU_ACTION_RECONNECT,
  APDU_ACTION_COUNT,
} apdu_action_t;

static const char *const ACTION_NAMES[APDU_ACTION_COUNT] = {
    "RAW APDU",
    "SELECT CUSTOM AID",
    "SELECT NDEF APP",
    "SELECT PPSE",
    "GET CHALLENGE",
    "TRACE / SD STATUS",
    "CLEAR HISTORY",
    "RECONNECT CARD",
};

static const uint8_t APDU_SELECT_NDEF[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07, 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00};
static const uint8_t APDU_SELECT_PPSE[] = {0x00, 0xA4, 0x04, 0x00, 0x0E, 0x32, 0x50,
                                           0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E,
                                           0x44, 0x44, 0x46, 0x30, 0x31, 0x00};
static const uint8_t APDU_GET_CHALLENGE[] = {0x00, 0x84, 0x00, 0x00, 0x08};

static lv_obj_t *s_screen;
static lv_obj_t *s_status_label;
static lv_obj_t *s_link_label;
static lv_obj_t *s_action_label;
static lv_obj_t *s_heading_label;
static lv_obj_t *s_tx_label;
static lv_obj_t *s_rx_label;
static lv_obj_t *s_sw_label;
static lv_obj_t *s_meta_label;
static lv_obj_t *s_footer;
static lv_timer_t *s_timer;
static apdu_action_t s_action;
static uint8_t s_history_view;
static uint32_t s_seen_sequence;
static nfc_apdu_lab_state_t s_seen_state = (nfc_apdu_lab_state_t)-1;
static uint8_t s_pending[NFC_APDU_LAB_TX_MAX];
static size_t s_pending_len;
static nfc_apdu_risk_t s_pending_risk;

static void
preview_hex(const uint8_t *data, size_t len, size_t preview, char *out, size_t out_size) {
  const size_t shown = len < preview ? len : preview;
  nfc_apdu_hex_encode(data, shown, ' ', out, out_size);
  if (shown < len) {
    const size_t used = strlen(out);
    if (used + 5U < out_size)
      snprintf(out + used, out_size - used, " ...");
  }
}

static void show_action(void) {
  lv_label_set_text_fmt(s_action_label,
                        "ACTION %u/%u  %s",
                        (unsigned)s_action + 1U,
                        APDU_ACTION_COUNT,
                        ACTION_NAMES[s_action]);
}

static void show_link(const nfc_apdu_lab_status_t *status) {
  char id[32];
  nfc_apdu_hex_encode(status->identifier, status->identifier_len, ':', id, sizeof(id));
  if (status->protocol == HB_PROTO_ISO14443_4B) {
    lv_label_set_text_fmt(s_link_label,
                          "ISO-B %s | RF %umV A%u%s",
                          id,
                          status->regulator_mv,
                          status->rf_amplitude,
                          status->regulator_current_limit ? " LIMIT" : "");
  } else {
    lv_label_set_text_fmt(s_link_label,
                          "ISO-A %s FSC%u | RF %umV A%u%s",
                          id,
                          status->frame_size,
                          status->regulator_mv,
                          status->rf_amplitude,
                          status->regulator_current_limit ? " LIMIT" : "");
  }
}

static void show_entry(const nfc_apdu_lab_status_t *status) {
  nfc_apdu_lab_entry_t entry;
  if (!nfc_apdu_lab_get_history(s_history_view, &entry)) {
    lv_label_set_text(s_heading_label, "No APDU exchanges yet");
    lv_label_set_text(s_tx_label, "TX  Choose an action and press OK");
    lv_label_set_text(s_rx_label, "RX  --");
    lv_label_set_text(s_sw_label, "SW  --");
    lv_label_set_text(s_meta_label, "Safe mode: payment-state commands blocked");
    return;
  }

  nfc_apdu_cmd_info_t command;
  const bool parsed = nfc_apdu_parse_command(entry.tx, entry.tx_len, &command);
  lv_label_set_text_fmt(s_heading_label,
                        "#%lu  %s  [%s]  H%u/%u",
                        (unsigned long)entry.sequence,
                        parsed ? nfc_apdu_ins_name(command.cla, command.ins) : "INVALID APDU",
                        nfc_apdu_risk_name(entry.risk),
                        (unsigned)s_history_view + 1U,
                        status->history_count);
  char hex[120];
  preview_hex(entry.tx, entry.tx_len, APDU_UI_PREVIEW_TX, hex, sizeof(hex));
  lv_label_set_text_fmt(s_tx_label, "TX  %s", hex);
  preview_hex(entry.rx, entry.rx_len, APDU_UI_PREVIEW_RX, hex, sizeof(hex));
  lv_label_set_text_fmt(s_rx_label, "RX  %s", entry.rx_len > 0U ? hex : "<no data>");
  char sw_text[80];
  if (entry.sw != 0U)
    nfc_apdu_sw_format(entry.sw, sw_text, sizeof(sw_text));
  else
    snprintf(sw_text,
             sizeof(sw_text),
             "%s (0x%X); no card frame",
             esp_err_to_name(entry.error),
             (unsigned)entry.error);
  lv_label_set_text_fmt(s_sw_label, entry.sw != 0U ? "SW  %s" : "TRANSPORT  %s", sw_text);
  lv_obj_set_style_text_color(s_sw_label,
                              entry.sw == APDU_SW_OK ? lv_color_hex(UI_COL_SUCCESS)
                              : entry.sw == 0U       ? lv_color_hex(UI_COL_DANGER)
                                                     : lv_color_hex(UI_COL_WARNING),
                              0);
  lv_label_set_text_fmt(s_meta_label,
                        "%u B RX  %lu us  %u exchange%s%s%s%s",
                        (unsigned)entry.rx_len,
                        (unsigned long)entry.elapsed_us,
                        entry.exchanges,
                        entry.exchanges == 1U ? "" : "s",
                        entry.le_corrected ? "  Le fixed" : "",
                        entry.truncated ? "  TRUNCATED" : "",
                        entry.session_recovered ? "  REACTIVATED" : "");
}

static void render(const nfc_apdu_lab_status_t *status) {
  switch (status->state) {
    case NFC_APDU_LAB_STARTING:
      lv_label_set_text(s_status_label, "Starting ST25R3916 field...");
      break;
    case NFC_APDU_LAB_SEARCHING:
      lv_label_set_text(s_status_label, "Searching ISO-DEP A/B card...");
      break;
    case NFC_APDU_LAB_ACTIVATING:
      lv_label_set_text(s_status_label, "Activating / recovering ISO-DEP...");
      break;
    case NFC_APDU_LAB_EXCHANGING:
      lv_label_set_text(s_status_label, "Exchanging APDU - keep card still");
      lv_obj_set_style_text_color(s_status_label, current_theme.border_accent, 0);
      break;
    case NFC_APDU_LAB_READY:
      lv_label_set_text(s_status_label, "SESSION READY");
      lv_obj_set_style_text_color(s_status_label, lv_color_hex(UI_COL_SUCCESS), 0);
      show_link(status);
      show_entry(status);
      break;
    case NFC_APDU_LAB_ERROR:
    case NFC_APDU_LAB_STOPPED:
      if (status->error != HB_NFC_OK) {
        lv_label_set_text_fmt(s_status_label, "Session error 0x%X", (unsigned)status->error);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(UI_COL_DANGER), 0);
        lv_label_set_text(s_link_label, "Place an ISO14443-4 A/B card and reconnect");
      }
      break;
    default:
      break;
  }
  show_action();
}

static void timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen)
    return;
  nfc_apdu_lab_status_t status;
  nfc_apdu_lab_get_status(&status);
  if (status.latest_sequence != s_seen_sequence) {
    s_seen_sequence = status.latest_sequence;
    s_history_view = 0;
    ui_feedback(UI_FB_READ);
  }
  if (status.state != s_seen_state || status.latest_sequence == s_seen_sequence) {
    s_seen_state = status.state;
    render(&status);
  }
}

static void submit_pending_confirm(bool confirm) {
  if (confirm && s_pending_len > 0U) {
    const hb_nfc_err_t err = nfc_apdu_lab_submit(s_pending, s_pending_len, true);
    if (err != HB_NFC_OK)
      lv_label_set_text_fmt(s_status_label, "Queue error 0x%X", (unsigned)err);
  }
  s_pending_len = 0;
}

static void open_pending_confirm(void *unused) {
  (void)unused;
  char message[150];
  snprintf(message,
           sizeof(message),
           "%s command: %s. It may change card state or consume an authentication retry. Send only "
           "to a card you own?",
           nfc_apdu_risk_name(s_pending_risk),
           s_pending_len >= 2U ? nfc_apdu_ins_name(s_pending[0], s_pending[1]) : "APDU");
  msgbox_open(LV_SYMBOL_WARNING, message, "Send", "Cancel", submit_pending_confirm);
}

static void send_checked(const uint8_t *command, size_t command_len) {
  nfc_apdu_cmd_info_t parsed;
  if (!nfc_apdu_parse_command(command, command_len, &parsed)) {
    lv_label_set_text(s_status_label, "Invalid ISO 7816 APDU length/case");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(UI_COL_DANGER), 0);
    return;
  }
  const nfc_apdu_risk_t risk = nfc_apdu_classify(&parsed);
  if (risk == NFC_APDU_RISK_PAYMENT) {
    msgbox_open_info("/assets/icons/warning.bin",
                     "Blocked by safe mode",
                     "Payment-state APDUs are not transmitted by this workbench.",
                     lv_color_hex(UI_COL_DANGER));
    return;
  }
  if (risk == NFC_APDU_RISK_READ_ONLY) {
    const hb_nfc_err_t err = nfc_apdu_lab_submit(command, command_len, false);
    if (err != HB_NFC_OK)
      lv_label_set_text_fmt(s_status_label, "Queue error 0x%X", (unsigned)err);
    return;
  }
  memcpy(s_pending, command, command_len);
  s_pending_len = command_len;
  s_pending_risk = risk;
  lv_async_call(open_pending_confirm, NULL);
}

static void raw_submit(const char *text, void *user_data) {
  (void)user_data;
  uint8_t command[NFC_APDU_LAB_TX_MAX];
  size_t command_len = 0;
  size_t error_pos = 0;
  if (!nfc_apdu_hex_decode(text, command, sizeof(command), &command_len, &error_pos)) {
    lv_label_set_text_fmt(s_status_label, "Bad hex near character %u", (unsigned)error_pos + 1U);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(UI_COL_DANGER), 0);
    return;
  }
  send_checked(command, command_len);
}

static void aid_submit(const char *text, void *user_data) {
  (void)user_data;
  uint8_t aid[32];
  size_t aid_len = 0;
  size_t error_pos = 0;
  if (!nfc_apdu_hex_decode(text, aid, sizeof(aid), &aid_len, &error_pos) || aid_len > 16U) {
    lv_label_set_text(s_status_label, "AID must contain 1-16 bytes of hex");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(UI_COL_DANGER), 0);
    return;
  }
  uint8_t command[32];
  const size_t command_len =
      nfc_apdu_build_select_aid((nfc_apdu_buf_t){.buf = command, .max = sizeof(command)},
                                (nfc_apdu_data_t){.ptr = aid, .len = aid_len},
                                true,
                                0x00U);
  send_checked(command, command_len);
}

static void execute_action(void) {
  switch (s_action) {
    case APDU_ACTION_RAW:
      lv_label_set_text(s_status_label, "Enter command APDU as hexadecimal");
      keyboard_open(NULL, raw_submit, NULL);
      break;
    case APDU_ACTION_SELECT_AID:
      lv_label_set_text(s_status_label, "Enter application AID (1-16 bytes)");
      keyboard_open(NULL, aid_submit, NULL);
      break;
    case APDU_ACTION_SELECT_NDEF:
      send_checked(APDU_SELECT_NDEF, sizeof(APDU_SELECT_NDEF));
      break;
    case APDU_ACTION_SELECT_PPSE:
      send_checked(APDU_SELECT_PPSE, sizeof(APDU_SELECT_PPSE));
      break;
    case APDU_ACTION_GET_CHALLENGE:
      send_checked(APDU_GET_CHALLENGE, sizeof(APDU_GET_CHALLENGE));
      break;
    case APDU_ACTION_TRACE:
      msgbox_open_info("/assets/icons/sd_card.bin",
                       sd_is_mounted() ? "Live trace enabled" : "SD card not mounted",
                       sd_is_mounted() ? "/sdcard/nfc/apdu_trace.log is updated after every APDU."
                                       : "Insert an SD card to enable persistent APDU logging.",
                       sd_is_mounted() ? lv_color_hex(UI_COL_SUCCESS)
                                       : lv_color_hex(UI_COL_WARNING));
      break;
    case APDU_ACTION_CLEAR:
      nfc_apdu_lab_clear_history();
      s_seen_sequence = 0;
      s_history_view = 0;
      break;
    case APDU_ACTION_RECONNECT:
      nfc_apdu_lab_stop();
      (void)nfc_apdu_lab_start();
      s_seen_state = (nfc_apdu_lab_state_t)-1;
      s_seen_sequence = 0;
      s_history_view = 0;
      break;
    default:
      break;
  }
}

static void input_cb(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (event->action != INPUT_ACTION_PRESS)
    return;
  nfc_apdu_lab_status_t status;
  nfc_apdu_lab_get_status(&status);
  switch (event->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_UP:
      s_action = (apdu_action_t)((s_action + APDU_ACTION_COUNT - 1U) % APDU_ACTION_COUNT);
      show_action();
      ui_feedback(UI_FB_NAV);
      break;
    case INPUT_BTN_DOWN:
      s_action = (apdu_action_t)((s_action + 1U) % APDU_ACTION_COUNT);
      show_action();
      ui_feedback(UI_FB_NAV);
      break;
    case INPUT_BTN_RIGHT:
      if (status.history_count > 0U) {
        s_history_view = (uint8_t)((s_history_view + 1U) % status.history_count);
        show_entry(&status);
      }
      break;
    case INPUT_BTN_OK:
      if (status.state == NFC_APDU_LAB_READY || s_action == APDU_ACTION_RECONNECT ||
          s_action == APDU_ACTION_TRACE || s_action == APDU_ACTION_CLEAR) {
        ui_feedback(UI_FB_SELECT);
        execute_action();
      }
      break;
    default:
      break;
  }
}

void ui_nfc_apdu_stop(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (keyboard_is_open())
    keyboard_close();
  if (msgbox_is_open())
    msgbox_close();
  nfc_apdu_lab_stop();
}

void ui_nfc_apdu_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_action = APDU_ACTION_RAW;
  s_history_view = 0;
  s_seen_sequence = 0;
  s_seen_state = (nfc_apdu_lab_state_t)-1;
  s_pending_len = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  ui_chrome_header(s_screen, "APDU WORKBENCH", "/assets/icons/developer_board.bin");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(
      body, ui_screen_w() - 12, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 4, 0);
  lv_obj_set_style_pad_row(body, 2, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  s_status_label = lv_label_create(body);
  lv_obj_set_width(s_status_label, lv_pct(100));
  lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_12, 0);
  lv_label_set_text(s_status_label, "Starting APDU session...");

  s_link_label = lv_label_create(body);
  lv_obj_set_width(s_link_label, lv_pct(100));
  lv_label_set_long_mode(s_link_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(s_link_label, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(s_link_label, &lv_font_montserrat_12, 0);

  s_action_label = lv_label_create(body);
  lv_obj_set_width(s_action_label, lv_pct(100));
  lv_label_set_long_mode(s_action_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(s_action_label, current_theme.border_accent, 0);
  lv_obj_set_style_text_font(s_action_label, &lv_font_montserrat_12, 0);
  show_action();

  lv_obj_t *box = lv_obj_create(body);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(box, 8, 0);
  lv_obj_set_style_bg_color(box, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(box, 5, 0);
  lv_obj_set_style_pad_row(box, 1, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);

  s_heading_label = lv_label_create(box);
  s_tx_label = lv_label_create(box);
  s_rx_label = lv_label_create(box);
  s_sw_label = lv_label_create(box);
  s_meta_label = lv_label_create(box);
  lv_obj_t *labels[] = {s_heading_label, s_tx_label, s_rx_label, s_sw_label, s_meta_label};
  for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
    lv_obj_set_width(labels[i], lv_pct(100));
    lv_label_set_long_mode(labels[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(labels[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(labels[i], current_theme.text_main, 0);
  }
  lv_obj_set_style_text_color(s_heading_label, current_theme.border_accent, 0);
  lv_label_set_text(s_heading_label, "No APDU exchanges yet");
  lv_label_set_text(s_tx_label, "TX  --");
  lv_label_set_text(s_rx_label, "RX  --");
  lv_label_set_text(s_sw_label, "SW  --");
  lv_label_set_text(s_meta_label, "Payment-state commands blocked");

  s_footer = ui_chrome_footer(s_screen, "UP/DOWN action  OK run  RIGHT history  BACK");
  ui_input_set_screen_handler(input_cb, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_timer = lv_timer_create(timer_cb, APDU_UI_REFRESH_MS, NULL);
  (void)nfc_apdu_lab_start();
}
