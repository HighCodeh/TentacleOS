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
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sys_prio.h"

#include "capture_result_ui.h"
#include "keyboard_ui.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "nfc_capabilities.h"
#include "ui_feedback.h"
#include "nfc_card_info.h"
#include "nfc_manager.h"
#include "nfc_device.h"
#include "nfc_mfc_dump.h"
#include "nfc_mfc_ndef.h"
#include "nfc_poller.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "mf_classic.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define REFRESH_MS       33
#define REVEAL_MS        1400
#define SCAN_TIMEOUT_MS  15000
#define BLOCK_COUNT      NFC_MFC1K_BLOCKS
#define BLOCK_SIZE       NFC_MFC_BLOCK_SIZE
#define BLOCK_ROW_COUNT  4
#define BLOCK_WORK_STACK 8192

#define DUMP_W       214
#define DUMP_Y       102
#define DUMP_ROW_GAP 2

enum {
  ST_SCAN,
  ST_FOUND,
  ST_OPTIONS,
  ST_ERROR,
  ST_BLOCK_READING,
  ST_BLOCK_DONE,
  ST_BLOCK_ERROR,
};

#define DUMP_LINE_COUNT 3
#define DUMP_LINE_LEN   48

static const char *TAG = "NFC_READ_UI";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_card_panel = NULL;
static lv_obj_t *s_dump = NULL;
static nfc_ui_field_t s_field;
static lv_timer_t *s_timer = NULL;
static capture_result_t s_cr = {0};

static int s_state = ST_SCAN;
static uint32_t s_scan_start = 0;
static uint32_t s_found_at = 0;
static nfc_sim_card_t s_card;
static hb_nfc_card_data_t s_hw_card;
static nfc_card_capabilities_t s_capabilities;
static char s_dump_lines[DUMP_LINE_COUNT][DUMP_LINE_LEN];
static bool s_scan_active = false;

typedef nfc_mfc_dump_result_t block_read_result_t;

static TaskHandle_t s_block_task = NULL;
static block_read_result_t s_block_result;
static nfc_mfc_dump_progress_t s_block_progress;
static bool s_block_result_ready = false;
static bool s_block_cancel = false;
static bool s_exit_after_block = false;
static bool s_show_ndef_after_read = false;
static char s_ndef_text[NFC_MFC_NDEF_TEXT_MAX + 1U];
static int s_block_selected = 0;
static lv_obj_t *s_block_title = NULL;
static lv_obj_t *s_block_rows[BLOCK_ROW_COUNT] = {0};
static lv_obj_t *s_block_key = NULL;

/* The radio task only publishes a card here. LVGL objects remain owned by the
 * UI task and are updated from refresh_cb(). */
static portMUX_TYPE s_card_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_card_pending = false;

static void show_rings(bool show);
static void start_block_read(void);
static void show_block_result(const block_read_result_t *result);
static void run_context_action(nfc_card_action_id_t action);

static void stop_scan(void) {
  if (!s_scan_active)
    return;
  nfc_manager_stop();
  s_scan_active = false;
}

static void card_found_cb(const hb_nfc_card_data_t *card, void *ctx) {
  (void)ctx;
  if (card == NULL)
    return;

  portENTER_CRITICAL(&s_card_mux);
  if (!s_card_pending) {
    s_hw_card = *card;
    s_card_pending = true;
  }
  portEXIT_CRITICAL(&s_card_mux);
}

static bool take_pending_card(hb_nfc_card_data_t *out) {
  bool ready;
  portENTER_CRITICAL(&s_card_mux);
  ready = s_card_pending;
  if (ready) {
    *out = s_hw_card;
    s_card_pending = false;
  }
  portEXIT_CRITICAL(&s_card_mux);
  return ready;
}

static bool block_cancel_requested(void) {
  bool cancel;
  portENTER_CRITICAL(&s_card_mux);
  cancel = s_block_cancel;
  portEXIT_CRITICAL(&s_card_mux);
  return cancel;
}

static void publish_block_result(const block_read_result_t *result) {
  portENTER_CRITICAL(&s_card_mux);
  s_block_result = *result;
  s_block_result_ready = true;
  s_block_task = NULL;
  portEXIT_CRITICAL(&s_card_mux);
}

static void block_progress_cb(const nfc_mfc_dump_progress_t *progress, void *ctx) {
  (void)ctx;
  portENTER_CRITICAL(&s_card_mux);
  s_block_progress = *progress;
  portEXIT_CRITICAL(&s_card_mux);
}

static bool block_cancel_cb(void *ctx) {
  (void)ctx;
  return block_cancel_requested();
}

static void block_read_task(void *arg) {
  (void)arg;
  block_read_result_t result = {0};
  nfc_iso14443a_data_t card = s_hw_card.iso14443a;

  result.err = nfc_mfc_dump_read_1k(&card, block_cancel_cb, block_progress_cb, NULL, &result);
  if (!result.cancelled && result.blocks_read == NFC_MFC1K_BLOCKS) {
    const nfc_mfc_dump_progress_t saving = {
        .phase = NFC_MFC_DUMP_PHASE_SAVE,
        .sector = NFC_MFC1K_SECTORS - 1,
        .blocks_read = result.blocks_read,
        .keys_found = result.keys_a_found + result.keys_b_found,
    };
    block_progress_cb(&saving, NULL);
    if (nfc_mfc_dump_save(&card, &result) != ESP_OK)
      ESP_LOGW(TAG, "Complete dump could not be saved");
  }

  publish_block_result(&result);
  vTaskDelete(NULL);
}

static void clear_result_objects(void) {
  if (s_card_panel) {
    lv_obj_del(s_card_panel);
    s_card_panel = NULL;
  }
  if (s_dump) {
    lv_obj_del(s_dump);
    s_dump = NULL;
  }
  capture_result_destroy(&s_cr);
  s_block_title = NULL;
  s_block_key = NULL;
  memset(s_block_rows, 0, sizeof(s_block_rows));
}

static void show_block_error(const char *message) {
  show_rings(false);
  lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFB300), 0);
  lv_label_set_text(s_status, message);
  ui_chrome_footer_set_text(s_hint, "OK Retry dump   BACK Exit");
  s_state = ST_BLOCK_ERROR;
}

static void update_block_view(void) {
  if (s_block_title == NULL || s_block_selected < 0 || s_block_selected >= BLOCK_COUNT)
    return;

  const int sector = s_block_selected / 4;
  const int sector_block = s_block_selected % 4;
  char title[48];
  snprintf(title,
           sizeof(title),
           "BLOCK %02d  SECTOR %02d%s",
           s_block_selected,
           sector,
           s_block_selected == 0 ? "  MANUFACTURER"
           : sector_block == 3   ? "  TRAILER"
                                 : "");
  lv_label_set_text(s_block_title, title);

  for (int row = 0; row < BLOCK_ROW_COUNT; row++) {
    char line[24];
    if (!s_block_result.block_read[s_block_selected]) {
      lv_label_set_text(s_block_rows[row], "--  --  --  --");
      continue;
    }
    int off = row * 4;
    snprintf(line,
             sizeof(line),
             "%02X  %02X  %02X  %02X",
             s_block_result.blocks[s_block_selected][off + 0],
             s_block_result.blocks[s_block_selected][off + 1],
             s_block_result.blocks[s_block_selected][off + 2],
             s_block_result.blocks[s_block_selected][off + 3]);
    lv_label_set_text(s_block_rows[row], line);
  }

  lv_label_set_text_fmt(s_block_key,
                        "S%02d  Key A:%s  Key B:%s",
                        sector,
                        s_block_result.key_a_found[sector] ? "OK" : "--",
                        s_block_result.key_b_found[sector] ? "OK" : "--");
}

static void show_block_result(const block_read_result_t *result) {
  s_block_result = *result;
  s_block_selected = 0;
  show_rings(false);
  clear_result_objects();

  if (result->blocks_read == 0) {
    show_block_error("No sectors could be read");
    return;
  }

  lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(s_status,
                              result->blocks_read == BLOCK_COUNT && result->saved
                                  ? lv_color_hex(0x00E676)
                                  : lv_color_hex(0xFFB300),
                              0);
  lv_label_set_text_fmt(s_status,
                        "Dump %d/64  Keys %d/32%s",
                        result->blocks_read,
                        result->keys_a_found + result->keys_b_found,
                        result->saved ? " saved" : "");

  s_dump = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_dump, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_dump, 214, 170);
  lv_obj_align(s_dump, LV_ALIGN_CENTER, 0, 24);
  lv_obj_set_style_radius(s_dump, 12, 0);
  lv_obj_set_style_bg_color(s_dump, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_dump, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_dump, 1, 0);
  lv_obj_set_style_border_color(s_dump, current_theme.border_accent, 0);
  lv_obj_set_style_pad_all(s_dump, 12, 0);
  lv_obj_set_flex_flow(s_dump, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_dump, 7, 0);

  s_block_title = lv_label_create(s_dump);
  lv_obj_set_style_text_font(s_block_title, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_block_title, current_theme.border_accent, 0);

  for (int row = 0; row < BLOCK_ROW_COUNT; row++) {
    s_block_rows[row] = lv_label_create(s_dump);
    lv_obj_set_style_text_font(s_block_rows[row], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_block_rows[row], current_theme.text_main, 0);
  }

  s_block_key = lv_label_create(s_dump);
  lv_obj_set_style_text_font(s_block_key, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_block_key, current_theme.text_secondary, 0);

  update_block_view();
  ui_chrome_footer_set_text(s_hint, "UP/DOWN Block   OK Redump   BACK");
  nfc_ui_play_sound(NFC_SND_FOUND);
  s_state = ST_BLOCK_DONE;

  size_t text_len = 0;
  const hb_nfc_err_t ndef_err = nfc_mfc_ndef_extract_text(
      result->blocks, result->block_read, s_ndef_text, sizeof(s_ndef_text), &text_len);
  if (ndef_err == HB_NFC_OK && text_len > 0U) {
    msgbox_open_info("/assets/icons/description.bin",
                     "MIFARE NDEF TEXT",
                     s_ndef_text,
                     lv_color_hex(UI_COL_SUCCESS));
  } else if (s_show_ndef_after_read) {
    msgbox_open_info("/assets/icons/warning.bin",
                     "NO NDEF TEXT",
                     ndef_err == HB_NFC_ERR_PROTOCOL
                         ? "The MAD/NDEF structure is invalid or incomplete."
                         : "No UTF-8 Text record was found in this MIFARE Classic 1K.",
                     lv_color_hex(UI_COL_WARNING));
  }
  s_show_ndef_after_read = false;
}

static void start_block_read(void) {
  if (s_block_task != NULL)
    return;
  if (s_hw_card.protocol != HB_PROTO_MF_CLASSIC || s_hw_card.iso14443a.uid_len < 4 ||
      mf_classic_get_type(s_hw_card.iso14443a.sak) != MF_CLASSIC_1K) {
    show_block_error("Tag is not MIFARE Classic 1K");
    return;
  }

  clear_result_objects();
  portENTER_CRITICAL(&s_card_mux);
  s_block_result_ready = false;
  s_block_cancel = false;
  memset(&s_block_progress, 0, sizeof(s_block_progress));
  portEXIT_CRITICAL(&s_card_mux);
  s_exit_after_block = false;
  s_state = ST_BLOCK_READING;
  s_scan_start = lv_tick_get();
  show_rings(true);
  lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_label_set_text(s_status, "Starting full 1K dump...");
  ui_chrome_footer_set_text(s_hint, "Keep tag still   BACK Cancel");

  if (xTaskCreatePinnedToCore(block_read_task,
                              "nfc_blocks",
                              BLOCK_WORK_STACK,
                              NULL,
                              SYS_PRIO_SERVICE_HI,
                              &s_block_task,
                              SYS_CORE_RADIO) != pdPASS) {
    s_block_task = NULL;
    show_block_error("Reader task failed");
  }
}

static void prepare_card_view(const hb_nfc_card_data_t *card) {
  memset(&s_card, 0, sizeof(s_card));
  nfc_capabilities_build(card, &s_capabilities);
  strlcpy(s_card.type, s_capabilities.family, sizeof(s_card.type));

  switch (card->protocol) {
    case HB_PROTO_ISO14443_3B:
    case HB_PROTO_ISO14443_4B:
      s_card.uid_len = sizeof(card->data.iso14443b.pupi);
      memcpy(s_card.uid, card->data.iso14443b.pupi, s_card.uid_len);
      break;
    case HB_PROTO_FELICA:
      s_card.uid_len = sizeof(card->data.felica.idm);
      memcpy(s_card.uid, card->data.felica.idm, s_card.uid_len);
      break;
    case HB_PROTO_ISO15693:
    case HB_PROTO_SLIX:
      s_card.uid_len = sizeof(card->data.iso15693.uid);
      /* ISO15693 transmits UID least-significant byte first. Present the UID
       * in the conventional human-readable order. */
      for (uint8_t i = 0; i < s_card.uid_len; i++)
        s_card.uid[i] = card->data.iso15693.uid[s_card.uid_len - 1U - i];
      break;
    default: {
      const nfc_iso14443a_data_t *a = &card->iso14443a;
      s_card.uid_len = a->uid_len < sizeof(s_card.uid) ? a->uid_len : sizeof(s_card.uid);
      memcpy(s_card.uid, a->uid, s_card.uid_len);
      s_card.atqa = ((uint16_t)a->atqa[0] << 8) | a->atqa[1];
      s_card.sak = a->sak;
      break;
    }
  }

  snprintf(s_dump_lines[0], sizeof(s_dump_lines[0]), "%s", s_capabilities.standard);
  snprintf(s_dump_lines[1], sizeof(s_dump_lines[1]), "%s", s_capabilities.evidence);
  if (card->rf.valid) {
    snprintf(s_dump_lines[2],
             sizeof(s_dump_lines[2]),
             "RF raw %u  %u.%uV%s",
             card->rf.amplitude,
             card->rf.regulator_mv / 1000U,
             (card->rf.regulator_mv % 1000U) / 100U,
             card->rf.current_limit ? " LIMIT" : "");
  } else {
    snprintf(s_dump_lines[2], sizeof(s_dump_lines[2]), "RF diagnostics unavailable");
  }
}

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
  stop_scan();

  s_state = ST_SCAN;
  s_scan_start = lv_tick_get();
  portENTER_CRITICAL(&s_card_mux);
  s_card_pending = false;
  portEXIT_CRITICAL(&s_card_mux);
  if (s_card_panel) {
    lv_obj_del(s_card_panel);
    s_card_panel = NULL;
  }
  if (s_dump) {
    lv_obj_del(s_dump);
    s_dump = NULL;
  }
  capture_result_destroy(&s_cr);
  show_rings(true);
  lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_label_set_text(s_status, "Searching");
  ui_chrome_footer_set_text(s_hint, "Place tag   BACK Exit");

  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t cfg = HIGHBOY_NFC_CONFIG_DEFAULT();
    esp_err_t err = highboy_nfc_init(&cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "NFC init failed: %s", esp_err_to_name(err));
      show_rings(false);
      lv_obj_set_style_text_color(s_status, lv_color_hex(0xFF5252), 0);
      lv_label_set_text(s_status, "NFC hardware error");
      ui_chrome_footer_set_text(s_hint, "OK Retry   BACK Exit");
      s_state = ST_ERROR;
      return;
    }
  }

  hb_nfc_err_t err = nfc_manager_start(card_found_cb, NULL);
  if (err != HB_NFC_OK) {
    ESP_LOGE(TAG, "NFC scan start failed: %s", hb_nfc_err_str(err));
    highboy_nfc_field_off();
    show_rings(false);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFF5252), 0);
    lv_label_set_text(s_status, "NFC field unavailable");
    ui_chrome_footer_set_text(s_hint, "OK Retry   BACK Exit");
    s_state = ST_ERROR;
    return;
  }
  s_scan_active = true;
}

static void build_dump(void) {
  s_dump = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_dump, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(s_dump, DUMP_W);
  lv_obj_set_height(s_dump, LV_SIZE_CONTENT);
  const int dumpH = DUMP_LINE_COUNT * 14 + (DUMP_LINE_COUNT - 1) * DUMP_ROW_GAP;
  const int dump_y =
      LV_MIN(DUMP_Y, (ui_screen_h() - UI_CHROME_FOOTER_H) - ui_screen_h() / 2 - dumpH / 2);
  lv_obj_align(s_dump, LV_ALIGN_CENTER, 0, dump_y);
  lv_obj_set_style_bg_opa(s_dump, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_dump, 0, 0);
  lv_obj_set_style_pad_all(s_dump, 0, 0);
  lv_obj_set_flex_flow(s_dump, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_dump, DUMP_ROW_GAP, 0);
  for (int i = 0; i < DUMP_LINE_COUNT; i++) {
    lv_obj_t *ln = lv_label_create(s_dump);
    lv_label_set_text(ln, s_dump_lines[i]);
    lv_obj_set_style_text_color(ln, current_theme.text_secondary, 0);
    lv_obj_set_style_text_font(ln, &lv_font_montserrat_12, 0);
  }
}

static void reveal(const hb_nfc_card_data_t *card) {
  s_state = ST_FOUND;
  s_found_at = lv_tick_get();
  prepare_card_view(card);
  show_rings(false);
  s_card_panel = nfc_ui_card_panel(s_screen, &s_card);
  lv_obj_align(s_card_panel, LV_ALIGN_CENTER, 0, 10);
  lv_obj_fade_in(s_card_panel, 280, 0);
  build_dump();
  lv_obj_fade_in(s_dump, 280, 120);
  lv_obj_set_style_text_color(s_status, lv_color_hex(0x00E676), 0);
  lv_label_set_text(s_status, "Tag found!");
  nfc_ui_play_sound(NFC_SND_FOUND);
  ui_chrome_footer_set_text(s_hint, "Protocol verified   BACK Exit");
}

static void show_no_tag(void) {
  stop_scan();
  s_state = ST_ERROR;
  show_rings(false);
  lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);
  lv_label_set_text(s_status, "No tag found");
  ui_chrome_footer_set_text(s_hint, "OK Retry   BACK Exit");
}

static const char *action_symbol(nfc_card_action_id_t action) {
  switch (action) {
    case NFC_CARD_ACTION_MFC_DUMP:
    case NFC_CARD_ACTION_MFC_NDEF_READ:
    case NFC_CARD_ACTION_ULTRALIGHT_READ:
    case NFC_CARD_ACTION_ISO15693_READ:
    case NFC_CARD_ACTION_FELICA_READ:
      return LV_SYMBOL_DOWNLOAD;
    case NFC_CARD_ACTION_NDEF_READ:
      return LV_SYMBOL_FILE;
    case NFC_CARD_ACTION_APDU:
    case NFC_CARD_ACTION_KEY_DICTIONARY:
      return LV_SYMBOL_SETTINGS;
    case NFC_CARD_ACTION_PAYMENT_INFO:
      return LV_SYMBOL_EYE_OPEN;
    case NFC_CARD_ACTION_DESFIRE_INFO:
      return LV_SYMBOL_LIST;
    case NFC_CARD_ACTION_WRITE_DUMP:
      return LV_SYMBOL_EDIT;
    case NFC_CARD_ACTION_EMULATE_DUMP:
      return LV_SYMBOL_PLAY;
    case NFC_CARD_ACTION_SAVE:
      return LV_SYMBOL_SAVE;
    case NFC_CARD_ACTION_RESCAN:
      return LV_SYMBOL_REFRESH;
    default:
      return LV_SYMBOL_RIGHT;
  }
}

static void show_options(void) {
  if (s_card_panel) {
    lv_obj_del(s_card_panel);
    s_card_panel = NULL;
  }
  if (s_dump) {
    lv_obj_del(s_dump);
    s_dump = NULL;
  }
  if (s_status)
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);

  static char uidbuf[40];
  char uid[24];
  nfc_sim_format_uid(&s_card, uid, sizeof(uid));
  snprintf(uidbuf, sizeof(uidbuf), "UID %s", uid);

  capture_result_cfg_t cfg = {
      .accent = current_theme.border_accent,
      .card_icon = "/assets/icons/nfc.bin",
      .card_title = s_capabilities.standard,
      .card_sub = s_capabilities.family,
      .card_value = uidbuf,
  };

  capture_result_action_cfg_t actions[NFC_CAPABILITY_MAX_ACTIONS] = {0};
  for (uint8_t i = 0; i < s_capabilities.action_count; i++) {
    actions[i].id = s_capabilities.actions[i].id;
    actions[i].symbol = action_symbol(s_capabilities.actions[i].id);
    actions[i].label = s_capabilities.actions[i].label;
  }
  s_cr = capture_result_create_custom(s_screen, &cfg, actions, s_capabilities.action_count);
  s_state = ST_OPTIONS;
  ui_chrome_footer_set_text(s_hint, "UP/DOWN options   OK open   BACK");
}

static void on_name_submit(const char *text, void *ud) {
  (void)ud;
  const char *nm = (text && text[0]) ? text : s_card.type;
  snprintf(s_card.name, NFC_SIM_NAME_LEN, "%.*s", NFC_SIM_NAME_LEN - 1, nm);
  bool saved = nfc_device_save_generic(s_card.name, &s_hw_card) == HB_NFC_OK;
  if (saved) {
    nfc_ui_play_sound(NFC_SND_SAVE);
    capture_result_mark_action_done(&s_cr, NFC_CARD_ACTION_SAVE, "Identification saved");
    notify(NOTIFY_SAVED, "Tag saved to library");
  } else {
    notify(NOTIFY_WARNING, "Library full");
  }
}

static void nfc_read_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (ev->button == INPUT_BTN_BACK) {
    if (press) {
      if (s_state == ST_BLOCK_READING) {
        portENTER_CRITICAL(&s_card_mux);
        s_block_cancel = true;
        portEXIT_CRITICAL(&s_card_mux);
        s_exit_after_block = true;
        show_rings(false);
        lv_label_set_text(s_status, "Stopping reader...");
        ui_chrome_footer_set_text(s_hint, "Stopping safely");
        return;
      }
      stop_scan();
      ui_switch_screen(SCREEN_NFC_MENU);
    }
    return;
  }

  if (s_state == ST_ERROR) {
    if (press && ev->button == INPUT_BTN_OK)
      begin_scan();
    return;
  }

  if (s_state == ST_BLOCK_ERROR) {
    if (press && ev->button == INPUT_BTN_OK)
      start_block_read();
    return;
  }

  if (s_state == ST_BLOCK_DONE) {
    if (nav && ev->button == INPUT_BTN_DOWN) {
      s_block_selected = (s_block_selected + 1) % BLOCK_COUNT;
      update_block_view();
      ui_feedback(UI_FB_NAV);
    } else if (nav && ev->button == INPUT_BTN_UP) {
      s_block_selected = (s_block_selected - 1 + BLOCK_COUNT) % BLOCK_COUNT;
      update_block_view();
      ui_feedback(UI_FB_NAV);
    } else if (press && ev->button == INPUT_BTN_OK) {
      start_block_read();
    }
    return;
  }

  if (s_state == ST_BLOCK_READING)
    return;

  if (s_state != ST_OPTIONS)
    return;

  switch (ev->button) {
    case INPUT_BTN_DOWN:
      if (nav) {
        capture_result_next(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        capture_result_prev(&s_cr);
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        run_context_action((nfc_card_action_id_t)capture_result_selected_id(&s_cr));
      }
      break;
    default:
      break;
  }
}

static void refresh_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }

  if (s_state == ST_SCAN) {
    uint32_t el = lv_tick_get() - s_scan_start;
    hb_nfc_card_data_t card = {0};
    if (take_pending_card(&card)) {
      stop_scan();
      reveal(&card);
      return;
    }

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
    if (el >= SCAN_TIMEOUT_MS)
      show_no_tag();
  } else if (s_state == ST_FOUND) {
    if (lv_tick_get() - s_found_at >= REVEAL_MS)
      show_options();
  } else if (s_state == ST_BLOCK_READING) {
    uint32_t el = lv_tick_get() - s_scan_start;
    nfc_ui_field_tick(&s_field, el);
    nfc_mfc_dump_progress_t progress;
    portENTER_CRITICAL(&s_card_mux);
    progress = s_block_progress;
    portEXIT_CRITICAL(&s_card_mux);

    char buf[48];
    if (progress.phase == NFC_MFC_DUMP_PHASE_SAVE) {
      snprintf(buf, sizeof(buf), "Saving 1024-byte dump...");
    } else {
      snprintf(buf,
               sizeof(buf),
               "S%02d/16  B%02d/64  K%02d/32",
               progress.sector + 1,
               progress.blocks_read,
               progress.keys_found);
    }
    lv_label_set_text(s_status, buf);

    bool ready;
    portENTER_CRITICAL(&s_card_mux);
    ready = s_block_result_ready;
    if (ready)
      s_block_result_ready = false;
    portEXIT_CRITICAL(&s_card_mux);

    if (ready) {
      if (s_exit_after_block) {
        s_exit_after_block = false;
        ui_switch_screen(SCREEN_NFC_MENU);
      } else if (s_block_result.cancelled) {
        show_block_error("Block read cancelled");
      } else {
        show_block_result(&s_block_result);
      }
    }
  }
}

static void run_context_action(nfc_card_action_id_t action) {
  switch (action) {
    case NFC_CARD_ACTION_MFC_DUMP:
      s_show_ndef_after_read = false;
      start_block_read();
      break;
    case NFC_CARD_ACTION_MFC_NDEF_READ:
      s_show_ndef_after_read = true;
      start_block_read();
      break;
    case NFC_CARD_ACTION_KEY_DICTIONARY:
      ui_switch_screen(SCREEN_NFC_KEYDICT);
      break;
    case NFC_CARD_ACTION_ULTRALIGHT_READ:
      ui_switch_screen(SCREEN_NFC_ULTRALIGHT);
      break;
    case NFC_CARD_ACTION_NDEF_READ:
      ui_switch_screen(SCREEN_NFC_NDEF);
      break;
    case NFC_CARD_ACTION_APDU:
      ui_switch_screen(SCREEN_NFC_APDU);
      break;
    case NFC_CARD_ACTION_PAYMENT_INFO:
      ui_switch_screen(SCREEN_NFC_BANKCARD);
      break;
    case NFC_CARD_ACTION_DESFIRE_INFO:
      ui_switch_screen(SCREEN_NFC_DESFIRE);
      break;
    case NFC_CARD_ACTION_ISO15693_READ:
      ui_switch_screen(SCREEN_NFC_ISO15693);
      break;
    case NFC_CARD_ACTION_FELICA_READ:
      ui_switch_screen(SCREEN_NFC_FELICA);
      break;
    case NFC_CARD_ACTION_WRITE_DUMP:
      ui_switch_screen(SCREEN_NFC_WRITE);
      break;
    case NFC_CARD_ACTION_EMULATE_DUMP:
      ui_switch_screen(SCREEN_NFC_EMULATE);
      break;
    case NFC_CARD_ACTION_SAVE:
      keyboard_open(NULL, on_name_submit, NULL);
      break;
    case NFC_CARD_ACTION_RESCAN:
      begin_scan();
      break;
    default:
      break;
  }
}

void ui_nfc_read_stop(void) {
  stop_scan();

  portENTER_CRITICAL(&s_card_mux);
  s_block_cancel = true;
  portEXIT_CRITICAL(&s_card_mux);

  while (s_block_task != NULL)
    vTaskDelay(pdMS_TO_TICKS(10));
  nfc_poller_stop();
}

void ui_nfc_read_open(void) {
  nfc_sim_init();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_card_panel = NULL;
  s_dump = NULL;
  s_cr = (capture_result_t){0};
  s_found_at = 0;
  s_scan_active = false;
  s_show_ndef_after_read = false;
  s_ndef_text[0] = '\0';
  s_exit_after_block = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "READ TAG", "/assets/icons/contactless.bin");
  nfc_ui_field_create(&s_field, s_screen, ui_theme_get_accent());

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, "Searching");
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 52);

  s_hint = ui_chrome_footer(s_screen, "BACK  Exit");

  begin_scan();

  if (s_timer == NULL)
    s_timer = lv_timer_create(refresh_cb, REFRESH_MS, NULL);

  ui_input_set_screen_handler(nfc_read_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
