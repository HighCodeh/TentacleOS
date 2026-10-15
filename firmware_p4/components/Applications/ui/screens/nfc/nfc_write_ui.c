// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nfc_write_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sys_prio.h"

#include "highboy_nfc.h"
#include "keyboard_ui.h"
#include "msgbox_ui.h"
#include "nfc_mfc_ndef.h"
#include "nfc_mfc_write.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define WRITE_ICON       "/assets/icons/edit.bin"
#define MFC_DUMP_DIR     "/sdcard/nfc/dumps"
#define MFC1K_DUMP_BYTES 1024L
#define WRITE_MAX_DUMPS  10
#define WRITE_REFRESH_MS 50
#define WRITE_TASK_STACK 8192

typedef enum {
  WRITE_STATE_SELECT,
  WRITE_STATE_RUNNING,
  WRITE_STATE_DONE,
  WRITE_STATE_ERROR,
} write_state_t;

typedef struct {
  nfc_sim_card_t card;
  char path[128];
} write_dump_entry_t;

static const char *TAG = "NFC_WRITE_UI";

static lv_obj_t *s_screen;
static lv_obj_t *s_body;
static lv_obj_t *s_card_panel;
static lv_obj_t *s_status;
static lv_obj_t *s_detail;
static lv_obj_t *s_progress;
static lv_obj_t *s_footer;
static lv_timer_t *s_timer;

static write_dump_entry_t s_dumps[WRITE_MAX_DUMPS];
static int s_count;
static int s_selected;
static write_state_t s_state;
static char s_message[NFC_MFC_NDEF_TEXT_MAX + 1U];
static int s_progress_max = NFC_MFC_WRITE_DATA_BLOCKS;

static TaskHandle_t s_task;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_cancel;
static bool s_result_ready;
static nfc_mfc_write_progress_t s_write_progress;
static nfc_mfc_write_result_t s_write_result;

static bool selected_is_text(void) {
  return s_selected == 0;
}

static int selected_dump_index(void) {
  return s_selected - 1;
}

static int source_count(void) {
  return s_count + 1; /* Text writer plus every saved dump. */
}

static bool inspect_dump(const char *path, nfc_sim_card_t *card) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL)
    return false;
  uint8_t block0[16] = {0};
  bool valid = fread(block0, 1, sizeof(block0), fp) == sizeof(block0) &&
               fseek(fp, 0, SEEK_END) == 0 && ftell(fp) == MFC1K_DUMP_BYTES;
  fclose(fp);
  if (!valid)
    return false;

  memset(card, 0, sizeof(*card));
  memcpy(card->uid, block0, 4);
  card->uid_len = 4;
  card->atqa = 0x0400;
  card->sak = 0x08;
  snprintf(card->name,
           sizeof(card->name),
           "MFC %02X%02X%02X%02X",
           block0[0],
           block0[1],
           block0[2],
           block0[3]);
  snprintf(card->type, sizeof(card->type), "MIFARE Classic 1K dump");
  return true;
}

static int scan_dumps(void) {
  memset(s_dumps, 0, sizeof(s_dumps));
  DIR *dir = opendir(MFC_DUMP_DIR);
  if (dir == NULL)
    return 0;

  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && count < WRITE_MAX_DUMPS) {
    size_t len = strlen(entry->d_name);
    if (len < 8 || strncmp(entry->d_name, "mf_", 3) != 0 ||
        strcmp(entry->d_name + len - 4, ".bin") != 0) {
      continue;
    }
    int n = snprintf(
        s_dumps[count].path, sizeof(s_dumps[count].path), MFC_DUMP_DIR "/%s", entry->d_name);
    if (n <= 0 || (size_t)n >= sizeof(s_dumps[count].path) ||
        !inspect_dump(s_dumps[count].path, &s_dumps[count].card)) {
      continue;
    }
    count++;
  }
  closedir(dir);
  return count;
}

static void rebuild_source(void) {
  if (s_body != NULL) {
    lv_obj_del(s_body);
    s_body = NULL;
  }
  s_card_panel = NULL;
  s_status = NULL;
  s_detail = NULL;
  s_progress = NULL;

  s_body = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_body, ui_screen_w(), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_body, 0, 0);
  lv_obj_set_style_pad_all(s_body, 0, 0);

  if (selected_is_text()) {
    s_status = lv_label_create(s_body);
    lv_label_set_text(s_status, "NDEF TEXT MESSAGE");
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status, current_theme.border_accent, 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, -30);
    s_detail = lv_label_create(s_body);
    lv_label_set_text(s_detail,
                      s_message[0] != '\0'
                          ? s_message
                          : "Press OK to type a message\nUID and keys stay unchanged");
    lv_obj_set_width(s_detail, ui_screen_w() - 28);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_detail, current_theme.text_secondary, 0);
    lv_obj_align(s_detail, LV_ALIGN_CENTER, 0, 12);
    ui_chrome_footer_set_text(s_footer,
                              s_count > 0 ? "DOWN Dumps   OK Type message   BACK"
                                          : "OK Type message   BACK Exit");
    return;
  }

  const int dump_index = selected_dump_index();
  s_card_panel = nfc_ui_card_panel(s_body, &s_dumps[dump_index].card);
  lv_obj_align(s_card_panel, LV_ALIGN_TOP_MID, 0, 4);

  s_status = lv_label_create(s_body);
  lv_label_set_text_fmt(s_status, "SAVED DUMP %d/%d", dump_index + 1, s_count);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_status, current_theme.border_accent, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 134);

  s_detail = lv_label_create(s_body);
  lv_label_set_text(s_detail, "47 data blocks | UID and keys protected");
  lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_detail, current_theme.text_secondary, 0);
  lv_obj_align(s_detail, LV_ALIGN_TOP_MID, 0, 154);

  ui_chrome_footer_set_text(s_footer, "UP/DOWN Source   OK Write dump   BACK");
}

static bool cancel_cb(void *ctx) {
  (void)ctx;
  bool cancel;
  portENTER_CRITICAL(&s_mux);
  cancel = s_cancel;
  portEXIT_CRITICAL(&s_mux);
  return cancel;
}

static void progress_cb(const nfc_mfc_write_progress_t *progress, void *ctx) {
  (void)ctx;
  portENTER_CRITICAL(&s_mux);
  s_write_progress = *progress;
  portEXIT_CRITICAL(&s_mux);
}

static void write_task(void *arg) {
  (void)arg;
  nfc_mfc_write_result_t result = {0};

  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t cfg = HIGHBOY_NFC_CONFIG_DEFAULT();
    if (highboy_nfc_init(&cfg) != ESP_OK) {
      result.err = HB_NFC_ERR_INTERNAL;
      goto done;
    }
  }

  if (selected_is_text()) {
    result.err = nfc_mfc_write_text_1k(s_message, cancel_cb, progress_cb, NULL, &result);
  } else {
    result.err = nfc_mfc_write_data_1k(
        s_dumps[selected_dump_index()].path, cancel_cb, progress_cb, NULL, &result);
  }

done:
  portENTER_CRITICAL(&s_mux);
  s_write_result = result;
  s_result_ready = true;
  s_task = NULL;
  portEXIT_CRITICAL(&s_mux);
  vTaskDelete(NULL);
}

static void build_write_view(void) {
  if (s_body != NULL) {
    lv_obj_del(s_body);
    s_body = NULL;
  }
  s_card_panel = NULL;

  s_body = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(
      s_body, ui_screen_w() - 16, ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_body, 0, 0);
  lv_obj_set_style_pad_all(s_body, 8, 0);
  lv_obj_set_style_pad_row(s_body, 12, 0);
  lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  s_status = lv_label_create(s_body);
  lv_label_set_text(s_status,
                    selected_is_text() ? "Place target MIFARE Classic 1K"
                                       : "Place target MIFARE Classic 1K");
  lv_obj_set_width(s_status, lv_pct(100));
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_status, current_theme.text_main, 0);

  s_detail = lv_label_create(s_body);
  lv_label_set_text(s_detail,
                    selected_is_text() ? "Preparing MAD + NDEF text..." : "Finding target keys...");
  lv_obj_set_style_text_font(s_detail, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_detail, current_theme.text_secondary, 0);

  s_progress = lv_bar_create(s_body);
  lv_obj_set_size(s_progress, 200, 14);
  lv_bar_set_range(s_progress, 0, s_progress_max);
  lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_progress, lv_color_hex(0x202028), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_progress, current_theme.border_accent, LV_PART_INDICATOR);

  ui_chrome_footer_set_text(s_footer, "Keep tag still   BACK Cancel");
}

static void start_write(void) {
  if (s_task != NULL || (selected_is_text() && s_message[0] == '\0'))
    return;
  s_progress_max =
      selected_is_text() ? nfc_mfc_ndef_text_blocks_required(s_message) : NFC_MFC_WRITE_DATA_BLOCKS;
  if (s_progress_max <= 0)
    return;
  portENTER_CRITICAL(&s_mux);
  s_cancel = false;
  s_result_ready = false;
  memset(&s_write_progress, 0, sizeof(s_write_progress));
  memset(&s_write_result, 0, sizeof(s_write_result));
  portEXIT_CRITICAL(&s_mux);

  s_state = WRITE_STATE_RUNNING;
  build_write_view();
  if (xTaskCreatePinnedToCore(write_task,
                              "nfc_write",
                              WRITE_TASK_STACK,
                              NULL,
                              SYS_PRIO_SERVICE_HI,
                              &s_task,
                              SYS_CORE_RADIO) != pdPASS) {
    s_task = NULL;
    s_state = WRITE_STATE_ERROR;
    lv_label_set_text(s_status, "Could not start writer task");
    ui_chrome_footer_set_text(s_footer, "OK Retry   BACK Exit");
  }
}

static void confirm_cb(bool confirmed) {
  if (confirmed)
    start_write();
}

static void ask_confirmation(void) {
  char message[180];
  if (selected_is_text()) {
    snprintf(message,
             sizeof(message),
             "Write NDEF text to %d data blocks? Existing data in those blocks will be replaced. "
             "UID and keys stay unchanged.",
             nfc_mfc_ndef_text_blocks_required(s_message));
  } else {
    snprintf(message,
             sizeof(message),
             "Write 47 data blocks from this dump? UID and keys stay unchanged.");
  }
  msgbox_open_ex(LV_SYMBOL_WARNING, message, "Write", "Cancel", confirm_cb, false);
}

static void message_submit(const char *text, void *ctx) {
  (void)ctx;
  if (text == NULL || text[0] == '\0') {
    if (s_status != NULL)
      lv_label_set_text(s_status, "Message cannot be empty");
    return;
  }
  const size_t len = strlen(text);
  if (len > NFC_MFC_NDEF_TEXT_MAX) {
    if (s_status != NULL)
      lv_label_set_text(s_status, "Message is too long");
    return;
  }
  memcpy(s_message, text, len + 1U);
  rebuild_source();
  ask_confirmation();
}

static void execute_selected(void) {
  if (selected_is_text()) {
    keyboard_open(NULL, message_submit, NULL);
  } else {
    ask_confirmation();
  }
}

static void show_result(void) {
  if (s_write_result.err == HB_NFC_OK) {
    const int expected =
        s_write_result.blocks_expected > 0 ? s_write_result.blocks_expected : s_progress_max;
    s_state = WRITE_STATE_DONE;
    lv_label_set_text(s_status,
                      selected_is_text() ? "NDEF message written + verified"
                                         : "Write verified successfully");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_label_set_text_fmt(
        s_detail, "%d/%d blocks read back and matched", s_write_result.blocks_verified, expected);
    lv_bar_set_value(s_progress, expected, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(UI_COL_SUCCESS), LV_PART_INDICATOR);
    ui_chrome_footer_set_text(s_footer, "OK Write again   BACK Exit");
    ui_feedback(UI_FB_WRITE);
    nfc_ui_play_sound(NFC_SND_SAVE);
    return;
  }

  s_state = WRITE_STATE_ERROR;
  lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFB300), 0);
  if (s_write_result.cancelled) {
    lv_label_set_text(s_status, "Write cancelled safely");
  } else if (s_write_result.target.uid_len == 0) {
    lv_label_set_text(s_status, "No target tag found");
  } else if (s_write_result.target.sak != 0x08 && s_write_result.target.sak != 0x88) {
    lv_label_set_text(s_status, "Target is not MIFARE Classic 1K");
  } else {
    lv_label_set_text_fmt(s_status, "Partial write: block %d failed", s_write_result.failed_block);
  }
  lv_label_set_text_fmt(s_detail,
                        "%d/%d written | %d verified | %d sectors unlocked",
                        s_write_result.blocks_written,
                        s_write_result.blocks_expected,
                        s_write_result.blocks_verified,
                        s_write_result.sectors_unlocked);
  ui_chrome_footer_set_text(s_footer, "OK Retry   BACK Exit");
}

static void refresh_cb(lv_timer_t *timer) {
  (void)timer;
  if (s_screen == NULL || lv_screen_active() != s_screen)
    return;
  if (s_state != WRITE_STATE_RUNNING)
    return;

  bool ready;
  nfc_mfc_write_progress_t progress;
  portENTER_CRITICAL(&s_mux);
  ready = s_result_ready;
  if (ready)
    s_result_ready = false;
  progress = s_write_progress;
  portEXIT_CRITICAL(&s_mux);

  if (ready) {
    show_result();
    return;
  }

  const char *phase = progress.phase == NFC_MFC_WRITE_PHASE_FIND_KEY ? "Unlocking"
                      : progress.phase == NFC_MFC_WRITE_PHASE_WRITE  ? "Writing"
                                                                     : "Verifying";
  lv_label_set_text_fmt(
      s_status, "%s sector %02d block %02d", phase, progress.sector, progress.block);
  lv_label_set_text_fmt(s_detail,
                        "%d written | %d verified | %d sectors",
                        progress.blocks_written,
                        progress.blocks_verified,
                        progress.sectors_unlocked);
  lv_bar_set_value(s_progress, progress.blocks_verified, LV_ANIM_OFF);
}

static void write_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  const bool press = event->action == INPUT_ACTION_PRESS;
  const bool nav = press || event->action == INPUT_ACTION_REPEAT;

  if (event->button == INPUT_BTN_BACK && press) {
    if (s_state == WRITE_STATE_RUNNING) {
      portENTER_CRITICAL(&s_mux);
      s_cancel = true;
      portEXIT_CRITICAL(&s_mux);
      lv_label_set_text(s_status, "Stopping after current RF command...");
      return;
    }
    ui_switch_screen(SCREEN_NFC_MENU);
    return;
  }

  if (s_state == WRITE_STATE_RUNNING)
    return;
  if ((s_state == WRITE_STATE_DONE || s_state == WRITE_STATE_ERROR) &&
      (event->button == INPUT_BTN_OK || event->button == INPUT_BTN_RIGHT) && press) {
    execute_selected();
    return;
  }
  if (s_state != WRITE_STATE_SELECT)
    return;

  if (event->button == INPUT_BTN_DOWN && nav) {
    s_selected = (s_selected + 1) % source_count();
    rebuild_source();
    ui_feedback(UI_FB_NAV);
  } else if (event->button == INPUT_BTN_UP && nav) {
    s_selected = (s_selected + source_count() - 1) % source_count();
    rebuild_source();
    ui_feedback(UI_FB_NAV);
  } else if ((event->button == INPUT_BTN_OK || event->button == INPUT_BTN_RIGHT) && press) {
    execute_selected();
  }
}

void ui_nfc_write_stop(void) {
  portENTER_CRITICAL(&s_mux);
  s_cancel = true;
  portEXIT_CRITICAL(&s_mux);
  while (s_task != NULL)
    vTaskDelay(pdMS_TO_TICKS(10));
  if (s_timer != NULL) {
    lv_timer_del(s_timer);
    s_timer = NULL;
  }
  if (keyboard_is_open())
    keyboard_close();
  if (msgbox_is_open())
    msgbox_close();
}

void ui_nfc_write_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }

  s_count = scan_dumps();
  s_selected = 0;
  s_state = WRITE_STATE_SELECT;
  s_message[0] = '\0';

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  ui_chrome_header(s_screen, "WRITE MFC / NDEF", WRITE_ICON);
  s_footer = ui_chrome_footer(s_screen, "BACK Exit");
  rebuild_source();

  ui_input_set_screen_handler(write_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  s_timer = lv_timer_create(refresh_cb, WRITE_REFRESH_MS, NULL);
  ESP_LOGI(TAG, "NDEF text writer ready with %d saved source dumps", s_count);
}
