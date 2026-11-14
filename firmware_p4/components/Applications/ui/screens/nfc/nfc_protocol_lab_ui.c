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

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "nfc_archive.h"
#include "lvgl.h"
#include "esp_log.h"
#include "keyboard_ui.h"

#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define REFRESH_MS        100
#define DESFIRE_PAGE_TEXT 448U

static screen_id_t s_return = SCREEN_NFC_MENU;

static const char *TAG = "NFC_PROTOCOL_LAB_UI";

static char s_archive_selected[NFC_ARCHIVE_PATH_SIZE];
static bool s_write_pending;
static bool s_t4t_create;
static size_t s_desfire_page;
static size_t s_ntag4xx_page;
static size_t s_felica_page;
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
    lv_label_set_text(
        s_status,
        status->operation == NFC_PROTOCOL_OP_T4T_CREATE        ? "Creating NDEF application..."
        : status->operation == NFC_PROTOCOL_OP_T4T_WRITE       ? "Writing and verifying NDEF..."
        : status->operation == NFC_PROTOCOL_OP_T4T_LOAD        ? "Loading Type 4 file..."
        : status->operation == NFC_PROTOCOL_OP_FELICA_EMULATE  ? "Emulating saved FeliCa"
        : status->operation == NFC_PROTOCOL_OP_DESFIRE_EMULATE ? "Emulating DESFire identity only"
        : status->operation == NFC_PROTOCOL_OP_T4T_EMULATE     ? "Emulating saved Type 4 NDEF"
        : status->operation == NFC_PROTOCOL_OP_NTAG4XX_EMULATE ? "Emulating NTAG4xx identity only"
        : status->operation == NFC_PROTOCOL_OP_TEXKOM_EMULATE  ? "Emulating Texkom"
        : status->operation == NFC_PROTOCOL_OP_V_EMULATE       ? "Emulating saved NFC-V / SLIX"
        : (status->operation == NFC_PROTOCOL_OP_ST25TB_WRITE ||
           status->operation == NFC_PROTOCOL_OP_PLUS_WRITE ||
           status->operation == NFC_PROTOCOL_OP_UL_AES_WRITE ||
           status->operation == NFC_PROTOCOL_OP_SLIX_WRITE ||
           status->operation == NFC_PROTOCOL_OP_T4T_WRITE ||
           status->operation == NFC_PROTOCOL_OP_T4T_CREATE)
            ? "Writing tag..."
        : status->operation == NFC_PROTOCOL_OP_NTAG4XX_LOAD ? "Loading saved NTAG4xx..."
        : s_operation == NFC_PROTOCOL_OP_ARCHIVE            ? "Opening archive..."
                                                            : "Reading physical tag...");
    lv_obj_set_style_text_color(s_status, current_theme.border_accent, 0);
    ui_chrome_footer_set_text(s_footer,
                              (status->operation == NFC_PROTOCOL_OP_TEXKOM_EMULATE ||
                               status->operation == NFC_PROTOCOL_OP_V_EMULATE ||
                               status->operation == NFC_PROTOCOL_OP_DESFIRE_EMULATE ||
                               status->operation == NFC_PROTOCOL_OP_FELICA_EMULATE ||
                               (status->operation == NFC_PROTOCOL_OP_NTAG4XX_EMULATE ||
                                status->operation == NFC_PROTOCOL_OP_T4T_EMULATE))
                                  ? "Bring reader close   BACK stop"
                                  : "Keep tag still   BACK cancel");
    return;
  }
  if (status->state == NFC_PROTOCOL_STATE_DONE) {
    lv_label_set_text(s_status,
                      status->operation == NFC_PROTOCOL_OP_T4T_CREATE  ? "Application created"
                      : status->operation == NFC_PROTOCOL_OP_T4T_WRITE ? "NDEF write verified"
                      : (status->operation == NFC_PROTOCOL_OP_FELICA_EMULATE ||
                         (status->operation == NFC_PROTOCOL_OP_NTAG4XX_EMULATE ||
                          status->operation == NFC_PROTOCOL_OP_T4T_EMULATE))
                          ? "Emulation stopped"
                      : s_operation == NFC_PROTOCOL_OP_ARCHIVE
                          ? (status->operation == NFC_PROTOCOL_OP_ARCHIVE_EXPORT
                                 ? "Archive copy saved"
                                 : "Archive opened")
                      : (status->operation == NFC_PROTOCOL_OP_ST25TB_WRITE ||
                         status->operation == NFC_PROTOCOL_OP_PLUS_WRITE ||
                         status->operation == NFC_PROTOCOL_OP_UL_AES_WRITE ||
                         status->operation == NFC_PROTOCOL_OP_SLIX_WRITE ||
                         status->operation == NFC_PROTOCOL_OP_T4T_WRITE ||
                         status->operation == NFC_PROTOCOL_OP_T4T_CREATE)
                          ? status->operation != NFC_PROTOCOL_OP_ST25TB_WRITE
                                ? "Write finished; see acknowledged count"
                                : "Write verified"
                          : "Read complete");
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_label_set_text(s_heading, status->result.heading[0] ? status->result.heading : "Result");
    for (int i = 0; i < NFC_PROTOCOL_RESULT_LINES; i++)
      lv_label_set_text(s_lines[i], i < status->result.line_count ? status->result.lines[i] : "");
    ui_feedback(UI_FB_READ);
  } else if (status->state == NFC_PROTOCOL_STATE_CANCELLED) {
    lv_label_set_text(s_status,
                      (status->operation == NFC_PROTOCOL_OP_ST25TB_WRITE ||
                       status->operation == NFC_PROTOCOL_OP_PLUS_WRITE ||
                       status->operation == NFC_PROTOCOL_OP_UL_AES_WRITE ||
                       status->operation == NFC_PROTOCOL_OP_SLIX_WRITE ||
                       status->operation == NFC_PROTOCOL_OP_T4T_WRITE ||
                       status->operation == NFC_PROTOCOL_OP_T4T_CREATE)
                          ? "Write stopped; rescan to verify"
                          : "Read cancelled");
    lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);
  } else if (status->state == NFC_PROTOCOL_STATE_ERROR) {
    if (status->error == HB_NFC_ERR_NO_CARD || status->error == HB_NFC_ERR_NOT_FOUND)
      snprintf(
          text, sizeof(text), "No compatible %s tag", nfc_protocol_operation_name(s_operation));
    else
      snprintf(text,
               sizeof(text),
               "%s error 0x%X",
               (status->operation == NFC_PROTOCOL_OP_ST25TB_WRITE ||
                status->operation == NFC_PROTOCOL_OP_PLUS_WRITE ||
                status->operation == NFC_PROTOCOL_OP_UL_AES_WRITE ||
                status->operation == NFC_PROTOCOL_OP_SLIX_WRITE ||
                status->operation == NFC_PROTOCOL_OP_T4T_WRITE ||
                status->operation == NFC_PROTOCOL_OP_T4T_CREATE)
                   ? "Write"
                   : "Read",
               (unsigned)status->error);
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(UI_COL_DANGER), 0);
    clear_result();
    for (int i = 0; i < status->result.line_count; i++)
      lv_label_set_text(s_lines[i], status->result.lines[i]);
  } else {
    lv_label_set_text(s_status, "Ready");
  }
  ui_chrome_footer_set_text(
      s_footer,
      s_operation == NFC_PROTOCOL_OP_FELICA ? "UP/DN view  LEFT emulate  RIGHT file/key"
      : (s_operation == NFC_PROTOCOL_OP_DESFIRE || s_operation == NFC_PROTOCOL_OP_NTAG4XX)
          ? "UP/DN view  LEFT UID  RIGHT file  OK read"
      : s_operation == NFC_PROTOCOL_OP_TEXKOM     ? "OK read  RIGHT emulate  BACK exit"
      : s_operation == NFC_PROTOCOL_OP_ARCHIVE    ? "UP/DOWN file  OK open  RIGHT copy"
      : s_operation == NFC_PROTOCOL_OP_SLIX       ? "RIGHT password  DOWN write  UP clear"
      : s_operation == NFC_PROTOCOL_OP_ULTRALIGHT ? "RIGHT key  DOWN write  UP clear"
      : s_operation == NFC_PROTOCOL_OP_PLUS       ? "RIGHT key  DOWN write  UP clear key"
      : s_operation == NFC_PROTOCOL_OP_ST25TB     ? "OK read  RIGHT write  BACK exit"
                                                  : "OK rescan   BACK exit");
  if (s_operation == NFC_PROTOCOL_OP_NDEF && !status->running)
    ui_chrome_footer_set_text(s_footer, "OK read  RIGHT file  LEFT emulate  DOWN write  UP create");
}

static void timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() != s_screen || s_write_pending)
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

static void aes_key_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_ul_aes_key(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Use 0:KEY plain or 1:KEY CMAC; 32 hex");
    return;
  }
  start_read();
}

static void password_submit(const char *text, void *context) {
  (void)context;
  if (nfc_protocol_service_slix_password(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Use r:/w:/p:/d:/e: and 8 hex digits");
    return;
  }
  lv_label_set_text(s_status, "Password set; OK read, RIGHT add another");
}

static void write_submit(const char *text, void *context) {
  (void)context;
  if (text == NULL)
    return;
  if (nfc_protocol_service_st25tb_write(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Read first; use BB:DDDDDDDD (hex)");
    return;
  }
  s_write_pending = true;
  lv_label_set_text(s_status, "Confirm block write to last-read tag");
  lv_label_set_text(s_heading, text);
  ui_chrome_footer_set_text(s_footer, "OK write   BACK cancel");
}

static void plus_key_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_plus_key(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "AES key: exactly 32 hex digits");
    return;
  }
  start_read();
}

static void plus_write_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_plus_write(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Enter dump filename, or read a card first");
    return;
  }
  s_write_pending = true;
  lv_label_set_text(s_status, "Write data back to matching UID?");
  lv_label_set_text(s_heading, *text ? text : "Last saved Plus read");
  ui_chrome_footer_set_text(s_footer, "OK write   BACK cancel");
}

static void aes_write_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_ul_aes_write(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Dump filename, or read an AES tag first");
    return;
  }
  s_write_pending = true;
  lv_label_set_text(s_status,
                    strncmp(text, "key:", 4) == 0 ? "Write user pages AND replace DataProtKey?"
                                                  : "Write AES user pages; keep target keys?");
  lv_label_set_text(s_heading, *text ? text : "Last saved AES read");
  ui_chrome_footer_set_text(s_footer, "OK write presented tag   BACK cancel");
}

static void slix_write_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_slix_write(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Dump filename, or read a SLIX tag first");
    return;
  }
  s_write_pending = true;
  lv_label_set_text(s_status, "Restore data to matching UID? Keys unchanged");
  lv_label_set_text(s_heading, *text ? text : "Last saved SLIX read");
  ui_chrome_footer_set_text(s_footer, "OK write   BACK cancel");
}

static void select_archive(bool is_previous) {
  nfc_protocol_status_t status;
  nfc_protocol_service_get_status(&status);
  s_last_state = status.state;
  DIR *directory = opendir(NFC_ARCHIVE_DIRECTORY);
  char selected[NFC_ARCHIVE_PATH_SIZE] = {0};
  char wrap[NFC_ARCHIVE_PATH_SIZE] = {0};
  if (directory) {
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
      const char *name = entry->d_name;
      size_t length = strlen(name);
      if (length < 4 || strcmp(name + length - 4, ".nfc") ||
          length + sizeof(NFC_ARCHIVE_DIRECTORY) + 1 >= sizeof(selected))
        continue;
      int relative = strcmp(name, s_archive_selected);
      if (!wrap[0] || (is_previous ? strcmp(name, wrap) > 0 : strcmp(name, wrap) < 0))
        strlcpy(wrap, name, sizeof(wrap));
      if ((is_previous ? relative < 0 : relative > 0) &&
          (!selected[0] || (is_previous ? strcmp(name, selected) > 0 : strcmp(name, selected) < 0)))
        strlcpy(selected, name, sizeof(selected));
    }
    closedir(directory);
  }
  strlcpy(s_archive_selected, selected[0] ? selected : wrap, sizeof(s_archive_selected));
  clear_result();
  lv_label_set_text(s_heading,
                    s_archive_selected[0] ? s_archive_selected : "No .nfc files in NFC dumps");
  lv_label_set_text(s_status, "UP/DOWN select; OK open; RIGHT export copy");
}

static void desfire_load_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_desfire_file(text) != HB_NFC_OK) {
    ESP_LOGW(TAG, "Invalid DESFire filename or worker busy");
    lv_label_set_text(s_status, "Enter a DESFire .nfc filename from dumps");
    return;
  }
  s_last_state = (nfc_protocol_state_t)-1;
  s_desfire_page = 0;
  if (nfc_protocol_service_start(NFC_PROTOCOL_OP_DESFIRE_LOAD) != HB_NFC_OK)
    lv_label_set_text(s_status, "Could not load DESFire file");
}

static void desfire_page(bool previous) {
  char text[DESFIRE_PAGE_TEXT];
  size_t pages = 0;
  if (nfc_protocol_service_desfire_page(s_desfire_page, text, sizeof(text), &pages) != ESP_OK)
    return;
  s_desfire_page = previous ? (s_desfire_page + pages - 1) % pages : (s_desfire_page + 1) % pages;
  if (nfc_protocol_service_desfire_page(s_desfire_page, text, sizeof(text), &pages) != ESP_OK)
    return;
  clear_result();
  char heading[64];
  snprintf(heading,
           sizeof(heading),
           "DESFire details %u/%u",
           (unsigned)s_desfire_page + 1,
           (unsigned)pages);
  lv_label_set_text(s_heading, heading);
  char *line = text;
  for (size_t i = 0; i < NFC_PROTOCOL_RESULT_LINES && line; i++) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = 0;
    lv_label_set_text(s_lines[i], line);
    line = next;
  }
}

static void t4t_load_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_t4t_file(text) != HB_NFC_OK) {
    lv_label_set_text(s_status, "Enter a Type 4 .nfc filename from dumps");
    return;
  }
  s_last_state = (nfc_protocol_state_t)-1;
  if (nfc_protocol_service_start(NFC_PROTOCOL_OP_T4T_LOAD) != HB_NFC_OK)
    lv_label_set_text(s_status, "Could not load Type 4 file");
}

static void ntag4xx_load_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (nfc_protocol_service_ntag4xx_file(text) != HB_NFC_OK) {
    ESP_LOGW(TAG, "Invalid NTAG4xx filename or worker busy");
    lv_label_set_text(s_status, "Enter a NTAG4xx .nfc filename from dumps");
    return;
  }
  s_last_state = (nfc_protocol_state_t)-1;
  s_ntag4xx_page = 0;
  if (nfc_protocol_service_start(NFC_PROTOCOL_OP_NTAG4XX_LOAD) != HB_NFC_OK)
    lv_label_set_text(s_status, "Could not load NTAG4xx file");
}

static void ntag4xx_page(bool previous) {
  char text[DESFIRE_PAGE_TEXT];
  size_t pages = 0;
  if (nfc_protocol_service_ntag4xx_page(s_ntag4xx_page, text, sizeof(text), &pages) != ESP_OK)
    return;
  s_ntag4xx_page = previous ? (s_ntag4xx_page + pages - 1) % pages : (s_ntag4xx_page + 1) % pages;
  if (nfc_protocol_service_ntag4xx_page(s_ntag4xx_page, text, sizeof(text), &pages) != ESP_OK)
    return;
  clear_result();
  char heading[64];
  snprintf(heading,
           sizeof(heading),
           "NTAG4xx details %u/%u",
           (unsigned)s_ntag4xx_page + 1,
           (unsigned)pages);
  lv_label_set_text(s_heading, heading);
  char *line = text;
  for (size_t i = 0; i < NFC_PROTOCOL_RESULT_LINES && line; i++) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = 0;
    lv_label_set_text(s_lines[i], line);
    line = next;
  }
}

static void felica_load_submit(const char *text, void *context) {
  (void)context;
  if (!text)
    return;
  if (!strncmp(text, "key:", 4U)) {
    if (nfc_protocol_service_felica_key(text + 4U) != ESP_OK)
      lv_label_set_text(s_status, "Use key: followed by 32 hex digits");
    else
      lv_label_set_text(
          s_status, text[4] ? "Lite key set; OK reads with authentication" : "Lite key cleared");
    return;
  }
  if (nfc_protocol_service_felica_file(text) != HB_NFC_OK) {
    ESP_LOGW(TAG, "Invalid FeliCa filename or worker busy");
    lv_label_set_text(s_status, "Enter a FeliCa .nfc filename from dumps");
    return;
  }
  s_last_state = (nfc_protocol_state_t)-1;
  s_felica_page = 0;
  if (nfc_protocol_service_start(NFC_PROTOCOL_OP_FELICA_LOAD) != HB_NFC_OK)
    lv_label_set_text(s_status, "Could not load FeliCa file");
}

static void felica_page(bool previous) {
  char text[DESFIRE_PAGE_TEXT];
  size_t pages = 0;
  if (nfc_protocol_service_felica_page(s_felica_page, text, sizeof(text), &pages) != ESP_OK)
    return;
  s_felica_page = previous ? (s_felica_page + pages - 1) % pages : (s_felica_page + 1) % pages;
  if (nfc_protocol_service_felica_page(s_felica_page, text, sizeof(text), &pages) != ESP_OK)
    return;
  clear_result();
  char heading[64];
  snprintf(heading,
           sizeof(heading),
           "FeliCa details %u/%u",
           (unsigned)s_felica_page + 1,
           (unsigned)pages);
  lv_label_set_text(s_heading, heading);
  char *line = text;
  for (size_t i = 0; i < NFC_PROTOCOL_RESULT_LINES && line; i++) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = 0;
    lv_label_set_text(s_lines[i], line);
    line = next;
  }
}

static void input_cb(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (keyboard_is_open() || event->action != INPUT_ACTION_PRESS)
    return;
  nfc_protocol_status_t status;
  nfc_protocol_service_get_status(&status);
  if (s_operation == NFC_PROTOCOL_OP_NDEF) {
    if (s_write_pending) {
      if (event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_LEFT) {
        s_write_pending = false;
        render(&status);
      } else if (event->button == INPUT_BTN_OK) {
        s_write_pending = false;
        s_last_state = (nfc_protocol_state_t)-1;
        if (nfc_protocol_service_start(s_t4t_create ? NFC_PROTOCOL_OP_T4T_CREATE
                                                    : NFC_PROTOCOL_OP_T4T_WRITE) != HB_NFC_OK)
          lv_label_set_text(s_status, "Could not start Type 4 operation");
      }
    } else if (event->button == INPUT_BTN_BACK) {
      if (status.running)
        nfc_protocol_service_cancel();
      else
        ui_switch_screen(s_return);
    } else if (!status.running) {
      if (event->button == INPUT_BTN_OK)
        start_read();
      else if (event->button == INPUT_BTN_RIGHT)
        keyboard_open(NULL, t4t_load_submit, NULL);
      else if (event->button == INPUT_BTN_LEFT) {
        s_last_state = (nfc_protocol_state_t)-1;
        if (nfc_protocol_service_start(NFC_PROTOCOL_OP_T4T_EMULATE) != HB_NFC_OK)
          lv_label_set_text(s_status, "Read or load Type 4 first");
      } else if (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN) {
        s_t4t_create = event->button == INPUT_BTN_UP;
        s_write_pending = true;
        lv_label_set_text(s_status,
                          s_t4t_create ? "Create/complete NDEF on DESFire?"
                                       : "Replace target NDEF with loaded message?");
        clear_result();
        lv_label_set_text(s_lines[0],
                          s_t4t_create ? "DESFire only; no erase or key changes"
                                       : "Place writable Type 4 tag");
        lv_label_set_text(s_lines[1],
                          s_t4t_create ? "Missing files only; existing data kept"
                                       : "Current NDEF will be replaced");
        ui_chrome_footer_set_text(s_footer, "OK confirm  BACK cancel");
      }
    }
    return;
  }
  if (s_operation == NFC_PROTOCOL_OP_DESFIRE) {
    if (event->button == INPUT_BTN_BACK) {
      if (status.running)
        nfc_protocol_service_cancel();
      else
        ui_switch_screen(s_return);
    } else if (!status.running) {
      if (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)
        desfire_page(event->button == INPUT_BTN_UP);
      else if (event->button == INPUT_BTN_RIGHT)
        keyboard_open(NULL, desfire_load_submit, NULL);
      else if (event->button == INPUT_BTN_LEFT) {
        s_last_state = (nfc_protocol_state_t)-1;
        if (nfc_protocol_service_start(NFC_PROTOCOL_OP_DESFIRE_EMULATE) != HB_NFC_OK)
          lv_label_set_text(s_status, "Read or load DESFire first");
      } else if (event->button == INPUT_BTN_OK) {
        s_desfire_page = 0;
        start_read();
      }
    }
    return;
  }
  if (s_operation == NFC_PROTOCOL_OP_NTAG4XX) {
    if (event->button == INPUT_BTN_BACK) {
      if (status.running)
        nfc_protocol_service_cancel();
      else
        ui_switch_screen(s_return);
    } else if (!status.running) {
      if (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)
        ntag4xx_page(event->button == INPUT_BTN_UP);
      else if (event->button == INPUT_BTN_RIGHT)
        keyboard_open(NULL, ntag4xx_load_submit, NULL);
      else if (event->button == INPUT_BTN_LEFT) {
        s_last_state = (nfc_protocol_state_t)-1;
        if (nfc_protocol_service_start(NFC_PROTOCOL_OP_NTAG4XX_EMULATE) != HB_NFC_OK)
          lv_label_set_text(s_status, "Read or load NTAG4xx first");
      } else if (event->button == INPUT_BTN_OK) {
        s_ntag4xx_page = 0;
        start_read();
      }
    }
    return;
  }
  if (s_operation == NFC_PROTOCOL_OP_FELICA) {
    if (event->button == INPUT_BTN_BACK) {
      if (status.running)
        nfc_protocol_service_cancel();
      else
        ui_switch_screen(s_return);
    } else if (!status.running) {
      if (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)
        felica_page(event->button == INPUT_BTN_UP);
      else if (event->button == INPUT_BTN_RIGHT)
        keyboard_open(NULL, felica_load_submit, NULL);
      else if (event->button == INPUT_BTN_LEFT) {
        s_last_state = (nfc_protocol_state_t)-1;
        if (nfc_protocol_service_start(NFC_PROTOCOL_OP_FELICA_EMULATE) != HB_NFC_OK)
          lv_label_set_text(s_status, "Read or load FeliCa first");
      } else if (event->button == INPUT_BTN_OK) {
        s_felica_page = 0;
        start_read();
      }
    }
    return;
  }
  if (s_operation == NFC_PROTOCOL_OP_ARCHIVE) {
    if (event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_LEFT) {
      if (status.running)
        nfc_protocol_service_cancel();
      else
        ui_switch_screen(s_return);
    } else if (!status.running) {
      if (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)
        select_archive(event->button == INPUT_BTN_UP);
      else if (event->button == INPUT_BTN_OK || event->button == INPUT_BTN_RIGHT) {
        if (nfc_protocol_service_archive(s_archive_selected) == HB_NFC_OK) {
          s_last_state = (nfc_protocol_state_t)-1;
          if (nfc_protocol_service_start(event->button == INPUT_BTN_RIGHT
                                             ? NFC_PROTOCOL_OP_ARCHIVE_EXPORT
                                             : NFC_PROTOCOL_OP_ARCHIVE) != HB_NFC_OK)
            lv_label_set_text(s_status, "Could not open archive");
        }
      }
    }
    return;
  }
  if (s_write_pending) {
    if (event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_LEFT) {
      s_write_pending = false;
      (void)nfc_protocol_service_st25tb_write(NULL);
      (void)nfc_protocol_service_plus_write(NULL);
      (void)nfc_protocol_service_ul_aes_write(NULL);
      (void)nfc_protocol_service_slix_write(NULL);
      render(&status);
    } else if (event->button == INPUT_BTN_OK) {
      s_write_pending = false;
      s_last_state = (nfc_protocol_state_t)-1;
      if (nfc_protocol_service_start(
              s_operation == NFC_PROTOCOL_OP_PLUS         ? NFC_PROTOCOL_OP_PLUS_WRITE
              : s_operation == NFC_PROTOCOL_OP_ULTRALIGHT ? NFC_PROTOCOL_OP_UL_AES_WRITE
              : s_operation == NFC_PROTOCOL_OP_SLIX       ? NFC_PROTOCOL_OP_SLIX_WRITE
                                                    : NFC_PROTOCOL_OP_ST25TB_WRITE) != HB_NFC_OK) {
        (void)nfc_protocol_service_st25tb_write(NULL);
        (void)nfc_protocol_service_plus_write(NULL);
        (void)nfc_protocol_service_ul_aes_write(NULL);
        (void)nfc_protocol_service_slix_write(NULL);
        lv_label_set_text(s_status, "Could not start write");
      }
    }
    return;
  }
  if (event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_LEFT) {
    if (status.running)
      nfc_protocol_service_cancel();
    else
      ui_switch_screen(s_return);
  } else if (event->button == INPUT_BTN_RIGHT && !status.running &&
             s_operation == NFC_PROTOCOL_OP_TEXKOM) {
    s_last_state = (nfc_protocol_state_t)-1;
    if (nfc_protocol_service_start(NFC_PROTOCOL_OP_TEXKOM_EMULATE) != HB_NFC_OK)
      lv_label_set_text(s_status, "Could not start emulation");
  } else if (event->button == INPUT_BTN_RIGHT && !status.running &&
             s_operation == NFC_PROTOCOL_OP_ULTRALIGHT) {
    lv_label_set_text(s_status, "DataProtKey: 0:KEY plain, 1:KEY CMAC");
    keyboard_open(NULL, aes_key_submit, NULL);
  } else if (event->button == INPUT_BTN_UP && !status.running &&
             s_operation == NFC_PROTOCOL_OP_ULTRALIGHT) {
    (void)nfc_protocol_service_ul_aes_key(NULL);
    lv_label_set_text(s_status, "AES key cleared");
  } else if (event->button == INPUT_BTN_RIGHT && !status.running &&
             s_operation == NFC_PROTOCOL_OP_SLIX) {
    lv_label_set_text(s_status, "r:read w:write p:privacy d:destroy e:EAS");
    keyboard_open(NULL, password_submit, NULL);
  } else if (event->button == INPUT_BTN_RIGHT && !status.running &&
             s_operation == NFC_PROTOCOL_OP_ST25TB) {
    lv_label_set_text(s_status, "Enter BB:DDDDDDDD in hex");
    keyboard_open(NULL, write_submit, NULL);
  } else if (event->button == INPUT_BTN_DOWN && !status.running &&
             s_operation == NFC_PROTOCOL_OP_ULTRALIGHT) {
    lv_label_set_text(s_status, "Filename; key:filename also copies Data key");
    keyboard_open(NULL, aes_write_submit, NULL);
  } else if (event->button == INPUT_BTN_DOWN && !status.running &&
             s_operation == NFC_PROTOCOL_OP_SLIX) {
    lv_label_set_text(s_status, "Dump filename; empty = last saved read");
    keyboard_open(NULL, slix_write_submit, NULL);
  } else if (event->button == INPUT_BTN_UP && !status.running &&
             s_operation == NFC_PROTOCOL_OP_SLIX) {
    (void)nfc_protocol_service_slix_password(NULL);
    lv_label_set_text(s_status, "SLIX passwords cleared");
  } else if (event->button == INPUT_BTN_RIGHT && !status.running &&
             s_operation == NFC_PROTOCOL_OP_PLUS) {
    keyboard_open(NULL, plus_key_submit, NULL);
  } else if (event->button == INPUT_BTN_DOWN && !status.running &&
             s_operation == NFC_PROTOCOL_OP_PLUS) {
    lv_label_set_text(s_status, "Dump filename; empty = last saved read");
    keyboard_open(NULL, plus_write_submit, NULL);
  } else if (event->button == INPUT_BTN_UP && !status.running &&
             s_operation == NFC_PROTOCOL_OP_PLUS) {
    (void)nfc_protocol_service_plus_key(NULL);
    lv_label_set_text(s_status, "Plus AES key cleared");
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
  if (keyboard_is_open())
    keyboard_close();
  nfc_protocol_service_stop();
  if (s_operation == NFC_PROTOCOL_OP_DESFIRE)
    nfc_protocol_service_desfire_clear();
  if (s_operation == NFC_PROTOCOL_OP_NTAG4XX)
    nfc_protocol_service_ntag4xx_clear();
  if (s_operation == NFC_PROTOCOL_OP_FELICA)
    nfc_protocol_service_felica_clear();
  (void)nfc_protocol_service_st25tb_write(NULL);
  (void)nfc_protocol_service_ul_aes_key(NULL);
  s_write_pending = false;
  (void)nfc_protocol_service_plus_key(NULL);
  (void)nfc_protocol_service_plus_write(NULL);
  (void)nfc_protocol_service_ul_aes_write(NULL);
  (void)nfc_protocol_service_slix_write(NULL);
  (void)nfc_protocol_service_slix_password(NULL);
}

void ui_nfc_protocol_lab_open(nfc_protocol_op_t operation, const char *title) {
  const screen_id_t from = ui_current_screen();
  if (from == SCREEN_NFC_READ || from == SCREEN_NFC_SAVED)
    s_return = from;
  else if (from == SCREEN_NFC_MENU || from == SCREEN_HOME || from == SCREEN_MENU)
    s_return = SCREEN_NFC_MENU;
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_write_pending = false;
  s_operation = operation;
  s_desfire_page = 0;
  s_ntag4xx_page = 0;
  s_felica_page = 0;
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

void ui_nfc_st25tb_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_ST25TB, "ST25TB");
}

void ui_nfc_slix_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_SLIX, "ICODE SLIX");
}

void ui_nfc_ntag4xx_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_NTAG4XX, "NTAG4xx");
}

void ui_nfc_texkom_emulate_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_TEXKOM_EMULATE, "Texkom emulation");
}

void ui_nfc_texkom_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_TEXKOM, "Texkom");
}

void ui_nfc_v_emulate_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_V_EMULATE, "NFC-V / SLIX emulation");
}

void ui_nfc_plus_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_PLUS, "MIFARE Plus");
}

void ui_nfc_archive_open(void) {
  ui_nfc_protocol_lab_open(NFC_PROTOCOL_OP_ARCHIVE, "NFC files");
  s_archive_selected[0] = 0;
  select_archive(false);
}
