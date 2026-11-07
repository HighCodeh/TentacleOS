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

#include "nfc_bankcard_ui.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "power_manager.h"
#include "sys_prio.h"

#include "emv.h"
#include "nfc_emv.h"
#include "nfc_archive.h"
#include "keyboard_ui.h"
#include "highboy_nfc.h"
#include "iso_dep.h"
#include "nfc_poller.h"
#include "nfc_ui_common.h"
#include "poller.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define EMV_UI_MAX_APPS       8
#define EMV_UI_APDU_TIMEOUT   600
#define EMV_UI_SCAN_ATTEMPTS  100
#define EMV_UI_SCAN_DELAY_MS  100
#define EMV_UI_RADIO_DELAY_MS 180
#define EMV_UI_REFRESH_MS     50
#define EMV_UI_TASK_STACK     8192
#define EMV_ISO_DEP_SAK       0x20U

typedef enum {
  EMV_STAGE_IDLE,
  EMV_STAGE_INIT,
  EMV_STAGE_SEARCH,
  EMV_STAGE_ACTIVATE,
  EMV_STAGE_PPSE,
  EMV_STAGE_SELECT,
  EMV_STAGE_DONE,
} emv_stage_t;

typedef struct {
  hb_nfc_err_t err;
  emv_stage_t failed_stage;
  nfc_iso14443a_data_t card;
  nfc_iso_dep_data_t dep;
  emv_app_t apps[EMV_UI_MAX_APPS];
  uint16_t app_sw[EMV_UI_MAX_APPS];
  esp_err_t app_error[EMV_UI_MAX_APPS];
  char saved[EMV_UI_MAX_APPS][32];
  uint16_t ppse_sw;
  size_t app_count;
} emv_ui_result_t;

static const char *TAG = "NFC_EMV_UI";

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_uid;
static lv_obj_t *s_link;
static lv_obj_t *s_app;
static lv_obj_t *s_aid;
static lv_obj_t *s_sw;
static lv_obj_t *s_footer;
static lv_timer_t *s_timer;

static TaskHandle_t s_task;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_cancel;
static bool s_result_ready;
static emv_stage_t s_stage;
static emv_ui_result_t s_result;
static size_t s_selected_app;
static size_t s_page;
EXT_RAM_BSS_ATTR static nfc_emv_card_t s_cards[EMV_UI_MAX_APPS];
static char s_load_path[NFC_ARCHIVE_PATH_SIZE];
static void start_scan(void);

static void set_stage(emv_stage_t stage) {
  portENTER_CRITICAL(&s_mux);
  s_stage = stage;
  portEXIT_CRITICAL(&s_mux);
}

static bool cancel_requested(void) {
  bool cancel;
  portENTER_CRITICAL(&s_mux);
  cancel = s_cancel;
  portEXIT_CRITICAL(&s_mux);
  return cancel;
}

static void publish_result(const emv_ui_result_t *result) {
  portENTER_CRITICAL(&s_mux);
  s_result = *result;
  s_result_ready = true;
  s_task = NULL;
  portEXIT_CRITICAL(&s_mux);
}

static esp_err_t save_profile(const nfc_emv_card_t *card, char *out_name, size_t capacity) {
  if (mkdir("/sdcard/nfc", 0775) && errno != EEXIST)
    return ESP_FAIL;
  if (mkdir(NFC_ARCHIVE_DIRECTORY, 0775) && errno != EEXIST)
    return ESP_FAIL;
  for (unsigned i = 0; i < 10000; i++) {
    char path[NFC_ARCHIVE_PATH_SIZE];
    snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/emv_%04u.nfc", i);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
      if (errno == EEXIST)
        continue;
      return ESP_FAIL;
    }
    close(fd);
    esp_err_t error = nfc_emv_save(path, card);
    if (error != ESP_OK)
      unlink(path);
    else
      snprintf(out_name, capacity, "emv_%04u.nfc", i);
    return error;
  }
  return ESP_ERR_NO_MEM;
}

static void emv_scan_task(void *arg) {
  (void)arg;
  emv_ui_result_t result = {.err = HB_NFC_ERR_NOT_FOUND};
  bool no_sleep_held = false;
  bool poller_started = false;

  memset(s_cards, 0, sizeof(s_cards));
  if (s_load_path[0]) {
    result.err = nfc_emv_load(s_load_path, &s_cards[0]);
    if (result.err == ESP_OK && !cancel_requested()) {
      result.app_count = 1;
      result.apps[0] = s_cards[0].profile.app;
      result.app_sw[0] = s_cards[0].profile.status;
      result.app_error[0] = s_cards[0].profile.is_complete ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
      memcpy(result.card.uid, s_cards[0].identity.uid, s_cards[0].identity.uid_len);
      result.card.uid_len = s_cards[0].identity.uid_len;
      strlcpy(result.saved[0], strrchr(s_load_path, '/') + 1, sizeof(result.saved[0]));
    } else if (cancel_requested())
      result.err = ESP_ERR_INVALID_STATE;
    goto done;
  }

  set_stage(EMV_STAGE_INIT);
  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t cfg = HIGHBOY_NFC_CONFIG_DEFAULT();
    if (highboy_nfc_init(&cfg) != ESP_OK) {
      result.err = HB_NFC_ERR_INTERNAL;
      result.failed_stage = EMV_STAGE_INIT;
      goto done;
    }
  }

  if (power_manager_no_sleep_acquire() != ESP_OK) {
    result.err = HB_NFC_ERR_INTERNAL;
    result.failed_stage = EMV_STAGE_INIT;
    goto done;
  }
  no_sleep_held = true;

  if (nfc_poller_start() != HB_NFC_OK) {
    result.err = HB_NFC_ERR_FIELD;
    result.failed_stage = EMV_STAGE_INIT;
    goto done;
  }
  poller_started = true;
  vTaskDelay(pdMS_TO_TICKS(EMV_UI_RADIO_DELAY_MS));

  set_stage(EMV_STAGE_SEARCH);
  for (int i = 0; i < EMV_UI_SCAN_ATTEMPTS && !cancel_requested(); i++) {
    if (iso14443a_poller_select(&result.card) == HB_NFC_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(EMV_UI_SCAN_DELAY_MS));
  }
  if (cancel_requested()) {
    result.err = ESP_ERR_INVALID_STATE;
    result.failed_stage = EMV_STAGE_SEARCH;
    goto done;
  }
  if (result.card.uid_len == 0) {
    result.err = HB_NFC_ERR_NOT_FOUND;
    result.failed_stage = EMV_STAGE_SEARCH;
    goto done;
  }
  if ((result.card.sak & EMV_ISO_DEP_SAK) == 0U) {
    result.err = HB_NFC_ERR_PROTOCOL;
    result.failed_stage = EMV_STAGE_ACTIVATE;
    goto done;
  }

  set_stage(EMV_STAGE_ACTIVATE);
  result.err = iso_dep_rats(8, 0, &result.dep);
  if (result.err != HB_NFC_OK) {
    result.failed_stage = EMV_STAGE_ACTIVATE;
    goto done;
  }

  set_stage(EMV_STAGE_PPSE);
  uint8_t fci[512] = {0};
  size_t fci_len = 0;
  result.err = emv_select_ppse(HB_PROTO_ISO14443_4A,
                               &result.dep,
                               fci,
                               sizeof(fci),
                               &fci_len,
                               &result.ppse_sw,
                               EMV_UI_APDU_TIMEOUT);
  if (result.err != HB_NFC_OK || result.ppse_sw != 0x9000U) {
    if (result.err == HB_NFC_OK)
      result.err = HB_NFC_ERR_PROTOCOL;
    result.failed_stage = EMV_STAGE_PPSE;
    goto done;
  }

  result.app_count = emv_extract_aids(fci, fci_len, result.apps, EMV_UI_MAX_APPS);
  if (result.app_count == 0) {
    result.err = HB_NFC_ERR_NOT_FOUND;
    result.failed_stage = EMV_STAGE_PPSE;
    goto done;
  }

  set_stage(EMV_STAGE_SELECT);
  if ((result.card.uid_len != 4 && result.card.uid_len != 7) ||
      result.dep.ats_len > T4T_EMU_ATS_CAPACITY) {
    result.err = ESP_ERR_NOT_SUPPORTED;
    result.failed_stage = EMV_STAGE_ACTIVATE;
    goto done;
  }
  for (size_t i = 0; i < result.app_count && !cancel_requested(); i++) {
    nfc_emv_card_t *card = &s_cards[i];
    memcpy(card->identity.uid, result.card.uid, result.card.uid_len);
    card->identity.uid_len = result.card.uid_len;
    memcpy(card->identity.atqa, result.card.atqa, 2);
    card->identity.sak = result.card.sak;
    memcpy(card->identity.ats, result.dep.ats, result.dep.ats_len);
    card->identity.ats_len = result.dep.ats_len;
    card->profile.app = result.apps[i];
    result.app_error[i] = emv_read_application(
        HB_PROTO_ISO14443_4A, &result.dep, &card->profile, cancel_requested, EMV_UI_APDU_TIMEOUT);
    result.app_sw[i] = card->profile.status;
    if (cancel_requested())
      break;
    if (save_profile(card, result.saved[i], sizeof(result.saved[i])) != ESP_OK)
      strlcpy(result.saved[i], "SD save failed; data in RAM", sizeof(result.saved[i]));
  }

  if (cancel_requested()) {
    result.err = ESP_ERR_INVALID_STATE;
    result.failed_stage = EMV_STAGE_SELECT;
    goto done;
  }
  result.err = HB_NFC_ERR_PROTOCOL;
  for (size_t i = 0; i < result.app_count; i++)
    if (s_cards[i].profile.is_complete)
      result.err = HB_NFC_OK;
  result.failed_stage = EMV_STAGE_DONE;
  set_stage(EMV_STAGE_DONE);

done:
  if (poller_started)
    nfc_poller_stop();
  if (no_sleep_held)
    (void)power_manager_no_sleep_release();
  publish_result(&result);
  vTaskDelete(NULL);
}

static const char *scheme_for_aid(const emv_app_t *app) {
  static const struct {
    uint8_t rid[5];
    const char *name;
  } schemes[] = {
      {{0xA0, 0x00, 0x00, 0x00, 0x03}, "Visa"},
      {{0xA0, 0x00, 0x00, 0x00, 0x04}, "Mastercard"},
      {{0xA0, 0x00, 0x00, 0x00, 0x25}, "American Express"},
      {{0xA0, 0x00, 0x00, 0x00, 0x65}, "JCB"},
      {{0xA0, 0x00, 0x00, 0x01, 0x52}, "Discover"},
      {{0xA0, 0x00, 0x00, 0x03, 0x33}, "UnionPay"},
      {{0xA0, 0x00, 0x05, 0x06, 0x00}, "Elo"},
  };
  if (app == NULL || app->aid_len < 5)
    return "EMV application";
  for (size_t i = 0; i < sizeof(schemes) / sizeof(schemes[0]); i++) {
    if (memcmp(app->aid, schemes[i].rid, 5) == 0)
      return schemes[i].name;
  }
  return "EMV application";
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_size) {
  size_t pos = 0;
  if (out_size == 0)
    return;
  for (size_t i = 0; i < len && pos + 2 < out_size; i++)
    pos += (size_t)snprintf(out + pos, out_size - pos, "%02X", data[i]);
  out[pos] = '\0';
}

static void
field_text(const emv_profile_t *profile, emv_field_t field, char *out, size_t capacity) {
  if (!profile->length[field]) {
    strlcpy(out, "unavailable", capacity);
    return;
  }
  bytes_to_hex(profile->value[field], profile->length[field], out, capacity);
  if (field == EMV_FIELD_PAN) {
    size_t length = strlen(out);
    if (length && out[length - 1] == 'F')
      out[length - 1] = 0;
  }
}

static void show_selected_app(void) {
  if (!s_result.app_count || s_selected_app >= s_result.app_count)
    return;
  const emv_profile_t *profile = &s_cards[s_selected_app].profile;
  const emv_app_t *app = &profile->app;
  s_page %= 3U + profile->log_count;
  char first[64], second[64], third[64];
  lv_label_set_text_fmt(s_status,
                        "APP %u/%u  %s",
                        (unsigned)s_selected_app + 1,
                        (unsigned)s_result.app_count,
                        profile->is_complete ? "read" : "partial / failed");
  lv_obj_set_style_text_color(
      s_status, lv_color_hex(profile->is_complete ? 0x58E6A9 : 0xFFB300), 0);
  lv_label_set_text_fmt(s_uid,
                        "%s  [%u/%u]",
                        app->label[0] ? app->label : scheme_for_aid(app),
                        (unsigned)s_page + 1,
                        3U + profile->log_count);
  if (s_page == 0) {
    field_text(profile, EMV_FIELD_PAN, first, sizeof(first));
    field_text(profile, EMV_FIELD_EXPIRY, second, sizeof(second));
    lv_label_set_text_fmt(s_link, "PAN %s", first);
    lv_label_set_text_fmt(s_app,
                          "Holder: %s",
                          profile->length[EMV_FIELD_HOLDER]
                              ? (char *)profile->value[EMV_FIELD_HOLDER]
                              : "unavailable");
    lv_label_set_text_fmt(s_aid,
                          "Name: %s",
                          profile->length[EMV_FIELD_NAME] ? (char *)profile->value[EMV_FIELD_NAME]
                                                          : "unavailable");
    lv_label_set_text_fmt(s_sw, "Expiry YYMMDD: %s", second);
  } else if (s_page == 1) {
    bytes_to_hex(app->aid, app->aid_len, first, sizeof(first));
    lv_label_set_text_fmt(s_link, "AID %s", first);
    field_text(profile, EMV_FIELD_AIP, first, sizeof(first));
    lv_label_set_text_fmt(s_app, "AIP %s", first);
    field_text(profile, EMV_FIELD_COUNTRY, first, sizeof(first));
    field_text(profile, EMV_FIELD_CURRENCY, second, sizeof(second));
    lv_label_set_text_fmt(s_aid, "Country %s / Currency %s", first, second);
    field_text(profile, EMV_FIELD_EFFECTIVE, first, sizeof(first));
    lv_label_set_text_fmt(s_sw, "Effective YYMMDD: %s", first);
  } else if (s_page == 2) {
    field_text(profile, EMV_FIELD_ATC, first, sizeof(first));
    field_text(profile, EMV_FIELD_LAST_ATC, second, sizeof(second));
    lv_label_set_text_fmt(s_link, "ATC %s / Last online %s", first, second);
    if (profile->length[EMV_FIELD_PIN_TRIES])
      lv_label_set_text_fmt(
          s_app, "PIN tries remaining: %u", profile->value[EMV_FIELD_PIN_TRIES][0]);
    else
      lv_label_set_text(s_app, "PIN tries: unavailable");
    lv_label_set_text_fmt(s_aid,
                          "Records %u Logs %u SW %04X Err %X",
                          profile->records_read,
                          profile->log_count,
                          profile->status,
                          (unsigned)s_result.app_error[s_selected_app]);
    lv_label_set_text(s_sw, s_result.saved[s_selected_app]);
  } else {
    const emv_log_t *log = &profile->logs[s_page - 3];
    bytes_to_hex(log->value[0], log->length[0], first, sizeof(first));
    bytes_to_hex(log->value[1], log->length[1], second, sizeof(second));
    lv_label_set_text_fmt(s_link,
                          "ATC %s / Amount (BCD) %s",
                          *first ? first : "unavailable",
                          *second ? second : "unavailable");
    bytes_to_hex(log->value[2], log->length[2], first, sizeof(first));
    bytes_to_hex(log->value[3], log->length[3], second, sizeof(second));
    lv_label_set_text_fmt(s_app, "Country %s / Currency %s", first, second);
    bytes_to_hex(log->value[4], log->length[4], third, sizeof(third));
    lv_label_set_text_fmt(s_aid, "Date YYMMDD %s", *third ? third : "unavailable");
    bytes_to_hex(log->value[5], log->length[5], third, sizeof(third));
    lv_label_set_text_fmt(s_sw, "Time HHMMSS %s", *third ? third : "unavailable");
  }
}

static void show_result(void) {
  lv_obj_set_style_text_color(s_status, lv_color_hex(0x58E6A9), 0);
  s_selected_app = 0;
  s_page = 0;
  show_selected_app();
  ui_chrome_footer_set_text(s_footer, "UP/DN page  LEFT app  RIGHT file  OK scan");
  nfc_ui_play_sound(NFC_SND_FOUND);
}

static void show_error(void) {
  const char *message = "EMV read failed";
  switch (s_result.failed_stage) {
    case EMV_STAGE_SEARCH:
      message = "No contactless card found";
      break;
    case EMV_STAGE_ACTIVATE:
      message = "Card is not ISO-DEP / EMV";
      break;
    case EMV_STAGE_PPSE:
      message = s_result.ppse_sw != 0 ? "PPSE not supported" : "PPSE response failed";
      break;
    case EMV_STAGE_INIT:
      message = "NFC hardware unavailable";
      break;
    default:
      break;
  }
  lv_label_set_text(s_status, message);
  lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFB300), 0);
  lv_label_set_text(s_uid, "This inspector does not run a payment");
  lv_label_set_text(s_link, "Application data could not be read");
  lv_label_set_text(s_app, "");
  lv_label_set_text(s_aid, "");
  lv_label_set_text_fmt(s_sw, "Status %04X", s_result.ppse_sw);
  ui_chrome_footer_set_text(s_footer, "OK Retry   BACK Exit");
}

static void start_scan(void) {
  if (s_task != NULL)
    return;
  portENTER_CRITICAL(&s_mux);
  s_cancel = false;
  s_result_ready = false;
  s_stage = EMV_STAGE_INIT;
  memset(&s_result, 0, sizeof(s_result));
  portEXIT_CRITICAL(&s_mux);

  s_selected_app = 0;
  s_page = 0;
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_label_set_text(s_status, "Starting NFC field...");
  lv_label_set_text(s_uid, "Place an EMV contactless card on High Boy");
  lv_label_set_text(s_link, "Read-only protocol inspection");
  lv_label_set_text(s_app, "");
  lv_label_set_text(s_aid, "");
  lv_label_set_text(s_sw, "Reading application metadata and available records");
  ui_chrome_footer_set_text(s_footer, "Keep card still   BACK Cancel");

  if (xTaskCreatePinnedToCore(emv_scan_task,
                              "emv_scan",
                              EMV_UI_TASK_STACK,
                              NULL,
                              SYS_PRIO_SERVICE_HI,
                              &s_task,
                              SYS_CORE_RADIO) != pdPASS) {
    s_task = NULL;
    s_result.err = HB_NFC_ERR_INTERNAL;
    s_result.failed_stage = EMV_STAGE_INIT;
    show_error();
  }
}

static void refresh_cb(lv_timer_t *timer) {
  (void)timer;
  if (s_screen == NULL || s_status == NULL)
    return;

  bool ready;
  emv_stage_t stage;
  portENTER_CRITICAL(&s_mux);
  ready = s_result_ready;
  if (ready)
    s_result_ready = false;
  stage = s_stage;
  portEXIT_CRITICAL(&s_mux);

  if (ready) {
    if (s_result.err == HB_NFC_OK ||
        (s_result.app_count && s_result.failed_stage == EMV_STAGE_DONE))
      show_result();
    else if (!s_cancel)
      show_error();
    return;
  }

  if (s_task == NULL)
    return;
  switch (stage) {
    case EMV_STAGE_SEARCH:
      lv_label_set_text(s_status, "Searching for ISO14443-A card...");
      break;
    case EMV_STAGE_ACTIVATE:
      lv_label_set_text(s_status, "Activating ISO-DEP / ATS...");
      break;
    case EMV_STAGE_PPSE:
      lv_label_set_text(s_status, "Selecting EMV PPSE...");
      break;
    case EMV_STAGE_SELECT:
      lv_label_set_text(s_status, "Reading GPO, records and metadata...");
      break;
    default:
      break;
  }
}

static void load_submit(const char *text, void *context) {
  (void)context;
  if (!text || !*text || strchr(text, '/') || strchr(text, '\\') || strstr(text, "..") ||
      strpbrk(text, "\r\n") || s_task)
    return;
  int count = snprintf(s_load_path, sizeof(s_load_path), NFC_ARCHIVE_DIRECTORY "/%s", text);
  if (count < 0 || (size_t)count >= sizeof(s_load_path)) {
    s_load_path[0] = 0;
    return;
  }
  start_scan();
}

static void nfc_bankcard_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  if (keyboard_is_open() || ev->action != INPUT_ACTION_PRESS)
    return;

  switch (ev->button) {
    case INPUT_BTN_BACK:
      portENTER_CRITICAL(&s_mux);
      s_cancel = true;
      portEXIT_CRITICAL(&s_mux);
      ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_LEFT:
      if (!s_task && s_result.app_count) {
        s_selected_app = (s_selected_app + 1) % s_result.app_count;
        s_page = 0;
        show_selected_app();
      }
      break;
    case INPUT_BTN_RIGHT:
      if (!s_task)
        keyboard_open(NULL, load_submit, NULL);
      break;
    case INPUT_BTN_OK:
      if (s_task == NULL) {
        s_load_path[0] = 0;
        start_scan();
      }
      break;
    case INPUT_BTN_UP:
      if (s_task == NULL && s_result.app_count > 0) {
        s_page = (s_page + 2U + s_cards[s_selected_app].profile.log_count) %
                 (3U + s_cards[s_selected_app].profile.log_count);
        show_selected_app();
      }
      break;
    case INPUT_BTN_DOWN:
      if (s_task == NULL && s_result.app_count > 0) {
        s_page++;
        show_selected_app();
      }
      break;
    default:
      break;
  }
}

static lv_obj_t *make_line(lv_obj_t *parent, lv_color_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, lv_pct(100));
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, color, 0);
  return label;
}

void ui_nfc_bankcard_stop(void) {
  portENTER_CRITICAL(&s_mux);
  s_cancel = true;
  portEXIT_CRITICAL(&s_mux);
  while (s_task != NULL)
    vTaskDelay(pdMS_TO_TICKS(10));
  if (s_timer != NULL) {
    lv_timer_del(s_timer);
    s_timer = NULL;
  }
}

void ui_nfc_bankcard_open(void) {
  s_load_path[0] = 0;
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

  ui_chrome_header(s_screen, "EMV INSPECT", "/assets/icons/contactless.bin");

  lv_obj_t *body = lv_obj_create(s_screen);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(
      body, ui_screen_w() - 16, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 6, 0);
  lv_obj_set_style_pad_row(body, 7, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  s_status = make_line(body, current_theme.text_main);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  s_uid = make_line(body, current_theme.text_secondary);
  s_link = make_line(body, current_theme.text_secondary);
  s_app = make_line(body, current_theme.text_main);
  s_aid = make_line(body, current_theme.border_accent);
  s_sw = make_line(body, current_theme.text_secondary);

  s_footer = ui_chrome_footer(s_screen, "Keep card still   BACK Cancel");
  ui_input_set_screen_handler(nfc_bankcard_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  s_timer = lv_timer_create(refresh_cb, EMV_UI_REFRESH_MS, NULL);
  start_scan();
}
