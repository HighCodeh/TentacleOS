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
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "highboy_nfc.h"
#include "lvgl.h"
#include "nfc_poller.h"
#include "nfc_scanner.h"
#include "power_manager.h"
#include "sys_prio.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define NFC_ICON   "/assets/icons/contactless.bin"
#define TECH_COUNT 4
#define REFRESH_MS 100
#define TASK_STACK 6144

typedef struct {
  hb_nfc_err_t error;
  nfc_scanner_event_t event;
  bool cancelled;
} scan_result_t;

static const struct {
  const char *tech;
  const char *iso;
} TECHS[TECH_COUNT] = {
    {"NFC-A", "ISO14443A"},
    {"NFC-B", "ISO14443B"},
    {"NFC-F", "FeliCa"},
    {"NFC-V", "ISO15693"},
};

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_summary;
static lv_obj_t *s_row[TECH_COUNT];
static lv_obj_t *s_dot[TECH_COUNT];
static lv_obj_t *s_value[TECH_COUNT];
static lv_timer_t *s_timer;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_running;
static bool s_cancel;
static bool s_result_ready;
static scan_result_t s_result;

static void scanner_callback(nfc_scanner_event_t event, void *ctx) {
  scan_result_t *result = ctx;
  result->event = event;
}

static bool cancel_requested(void) {
  bool cancel;
  portENTER_CRITICAL(&s_mux);
  cancel = s_cancel;
  portEXIT_CRITICAL(&s_mux);
  return cancel;
}

static void publish_result(const scan_result_t *result) {
  portENTER_CRITICAL(&s_mux);
  s_result = *result;
  s_result_ready = true;
  s_running = false;
  portEXIT_CRITICAL(&s_mux);
}

static void scan_task(void *arg) {
  (void)arg;
  scan_result_t result = {.error = HB_NFC_ERR_INTERNAL};
  bool no_sleep = false;
  bool poller = false;
  nfc_scanner_t *scanner = NULL;

  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
    if (highboy_nfc_init(&config) != ESP_OK)
      goto done;
  }
  if (power_manager_no_sleep_acquire() != ESP_OK)
    goto done;
  no_sleep = true;
  result.error = nfc_poller_start();
  if (result.error != HB_NFC_OK)
    goto done;
  poller = true;
  vTaskDelay(pdMS_TO_TICKS(100));
  if (cancel_requested())
    goto done;

  scanner = nfc_scanner_alloc();
  if (scanner == NULL) {
    result.error = HB_NFC_ERR_INTERNAL;
    goto done;
  }
  result.error = nfc_scanner_start(scanner, scanner_callback, &result);

done:
  if (scanner != NULL)
    nfc_scanner_free(scanner);
  if (poller)
    nfc_poller_stop();
  else if (highboy_nfc_is_initialized())
    (void)highboy_nfc_field_off();
  if (no_sleep)
    (void)power_manager_no_sleep_release();
  result.cancelled = cancel_requested();
  publish_result(&result);
  vTaskDelete(NULL);
}

static bool event_has(const nfc_scanner_event_t *event, hb_nfc_protocol_t protocol) {
  for (uint8_t i = 0; i < event->count; i++) {
    if (event->protocols[i] == protocol)
      return true;
  }
  return false;
}

static bool tech_present(const nfc_scanner_event_t *event, int tech) {
  switch (tech) {
    case 0:
      return event_has(event, HB_PROTO_ISO14443_3A) || event_has(event, HB_PROTO_ISO14443_4A) ||
             event_has(event, HB_PROTO_MF_CLASSIC) || event_has(event, HB_PROTO_MF_ULTRALIGHT) ||
             event_has(event, HB_PROTO_MF_PLUS) || event_has(event, HB_PROTO_MF_DESFIRE);
    case 1:
      return event_has(event, HB_PROTO_ISO14443_3B) || event_has(event, HB_PROTO_ISO14443_4B);
    case 2:
      return event_has(event, HB_PROTO_FELICA);
    case 3:
      return event_has(event, HB_PROTO_ISO15693) || event_has(event, HB_PROTO_SLIX);
    default:
      return false;
  }
}

static const char *tech_detail(const nfc_scanner_event_t *event, int tech) {
  if (tech == 0) {
    if (event_has(event, HB_PROTO_MF_CLASSIC))
      return "MIFARE Classic";
    if (event_has(event, HB_PROTO_MF_ULTRALIGHT))
      return "Ultralight/NTAG";
    if (event_has(event, HB_PROTO_MF_PLUS))
      return "MIFARE Plus";
    if (event_has(event, HB_PROTO_MF_DESFIRE))
      return "MIFARE DESFire";
    if (event_has(event, HB_PROTO_ISO14443_4A))
      return "ISO-DEP Type A";
  }
  if (tech == 1 && event_has(event, HB_PROTO_ISO14443_4B))
    return "ISO-DEP Type B";
  return TECHS[tech].iso;
}

static void set_row(int index, const char *text, lv_color_t color, bool raised) {
  lv_obj_set_style_bg_color(s_row[index], current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(s_row[index], raised ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(s_dot[index], color, 0);
  lv_label_set_text(s_value[index], text);
  lv_obj_set_style_text_color(s_value[index], color, 0);
}

static void show_scanning(void) {
  lv_label_set_text(s_status, "Polling physical RF field");
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_label_set_text(s_summary, "A / B / F / V");
  for (int i = 0; i < TECH_COUNT; i++)
    set_row(i, "checking...", current_theme.border_accent, true);
  ui_chrome_footer_set_text(s_hint, "Keep tag still   BACK exit");
}

static void show_result(const scan_result_t *result) {
  if (result->cancelled)
    return;
  int detected = 0;
  for (int i = 0; i < TECH_COUNT; i++) {
    if (tech_present(&result->event, i)) {
      set_row(i, tech_detail(&result->event, i), lv_color_hex(UI_COL_SUCCESS), false);
      detected++;
    } else {
      set_row(i, "not detected", current_theme.text_secondary, false);
    }
  }

  char summary[40];
  if (result->error == HB_NFC_OK) {
    snprintf(
        summary, sizeof(summary), "%d technolog%s detected", detected, detected == 1 ? "y" : "ies");
    lv_label_set_text(s_status, "Physical scan complete");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_SUCCESS), 0);
    if (detected > 0)
      ui_feedback(UI_FB_READ);
  } else if (result->error == HB_NFC_ERR_NO_CARD || result->error == HB_NFC_ERR_NOT_FOUND) {
    snprintf(summary, sizeof(summary), "No NFC tag detected");
    lv_label_set_text(s_status, "Scan complete");
    lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);
  } else {
    snprintf(summary, sizeof(summary), "Radio error 0x%X", (unsigned)result->error);
    lv_label_set_text(s_status, "NFC hardware/protocol error");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_DANGER), 0);
  }
  lv_label_set_text(s_summary, summary);
  ui_chrome_footer_set_text(s_hint, "OK rescan   BACK exit");
}

static void timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen)
    return;
  scan_result_t result;
  bool ready;
  portENTER_CRITICAL(&s_mux);
  ready = s_result_ready;
  result = s_result;
  s_result_ready = false;
  portEXIT_CRITICAL(&s_mux);
  if (ready)
    show_result(&result);
}

static void start_scan(void) {
  portENTER_CRITICAL(&s_mux);
  if (s_running) {
    portEXIT_CRITICAL(&s_mux);
    return;
  }
  s_running = true;
  s_cancel = false;
  s_result_ready = false;
  memset(&s_result, 0, sizeof(s_result));
  portEXIT_CRITICAL(&s_mux);
  show_scanning();

  if (xTaskCreatePinnedToCore(
          scan_task, "nfc_scan", TASK_STACK, NULL, SYS_PRIO_SERVICE_HI, NULL, SYS_CORE_RADIO) !=
      pdPASS) {
    scan_result_t result = {.error = HB_NFC_ERR_INTERNAL};
    publish_result(&result);
  }
}

static void input_cb(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (event->action != INPUT_ACTION_PRESS)
    return;
  if (event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_LEFT) {
    ui_switch_screen(SCREEN_NFC_MENU);
  } else if (event->button == INPUT_BTN_OK) {
    start_scan();
  }
}

static void build_card(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(card, 214);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 9);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(card, 14, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_style_pad_row(card, 5, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

  for (int i = 0; i < TECH_COUNT; i++) {
    s_row[i] = lv_obj_create(card);
    lv_obj_remove_flag(s_row[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_row[i], lv_pct(100), 27);
    lv_obj_set_style_radius(s_row[i], 7, 0);
    lv_obj_set_style_border_width(s_row[i], 0, 0);
    lv_obj_set_style_bg_opa(s_row[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(s_row[i], 6, 0);
    lv_obj_set_style_pad_ver(s_row[i], 0, 0);
    lv_obj_set_flex_flow(s_row[i], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        s_row[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_dot[i] = lv_obj_create(s_row[i]);
    lv_obj_remove_flag(s_dot[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_dot[i], 8, 8);
    lv_obj_set_style_radius(s_dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_dot[i], 0, 0);
    lv_obj_set_style_bg_color(s_dot[i], current_theme.text_secondary, 0);

    lv_obj_t *tech = lv_label_create(s_row[i]);
    lv_label_set_text(tech, TECHS[i].tech);
    lv_obj_set_style_text_font(tech, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tech, current_theme.text_main, 0);
    lv_obj_set_style_pad_left(tech, 8, 0);

    s_value[i] = lv_label_create(s_row[i]);
    lv_obj_set_flex_grow(s_value[i], 1);
    lv_label_set_long_mode(s_value[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_value[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(s_value[i], &lv_font_montserrat_12, 0);
  }

  s_summary = lv_label_create(card);
  lv_obj_set_width(s_summary, lv_pct(100));
  lv_obj_set_style_text_align(s_summary, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_summary, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_summary, current_theme.text_secondary, 0);
}

void ui_nfc_scan_stop(void) {
  portENTER_CRITICAL(&s_mux);
  s_cancel = true;
  portEXIT_CRITICAL(&s_mux);
  for (;;) {
    portENTER_CRITICAL(&s_mux);
    const bool running = s_running;
    portEXIT_CRITICAL(&s_mux);
    if (!running)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
}

void ui_nfc_scan_open(void) {
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
  ui_chrome_header(s_screen, "IDENTIFY", NFC_ICON);

  s_status = lv_label_create(s_screen);
  lv_obj_set_width(s_status, lv_pct(100));
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  build_card();
  s_hint = ui_chrome_footer(s_screen, "Keep tag still   BACK exit");
  ui_input_set_screen_handler(input_cb, NULL);
  s_timer = lv_timer_create(timer_cb, REFRESH_MS, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  start_scan();
}
