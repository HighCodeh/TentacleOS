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

#include "c5_status_ui.h"

#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st7789.h"
#include "sys_prio.h"

#include "c5_console_ui.h"
#include "c5_flasher.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "ota_version.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define MX           8
#define CONTENT_W    (ui_screen_w() - 2 * MX)
#define INFO_Y       50
#define INFO_H       98
#define INFO_ROW_GAP 26
#define ACT_Y        158
#define ROW_H        36
#define ROW_GAP      6
#define ROW_STEP     (ROW_H + ROW_GAP)

#define COL_RAISE 0x170A28

#define HDR_ICON  "/assets/icons/developer_board.bin"
#define HDR_TITLE "C5 STATUS"

#define C5_VER_TIMEOUT_MS 500
#define C5_VER_BUF_LEN    24

#define C5_OP_TASK_STACK 4096
#define C5_OP_TASK_PRIO  SYS_PRIO_SERVICE_HI

#define C5_REBOOT_HOLD_MS    10000
#define C5_REBOOT_LABEL      LV_SYMBOL_REFRESH "  Rebooting C5..."
#define C5_REBOOT_TEXT_COLOR 0x8A8594

enum {
  ACT_CONSOLE = 0,
  ACT_DOWNLOAD,
  ACT_REBOOT,
  ACT_COUNT,
};

typedef struct {
  const char *sym;
  const char *name;
} action_def_t;

static const action_def_t ACTIONS[ACT_COUNT] = {
    {LV_SYMBOL_LIST, "C5 Console"},
    {LV_SYMBOL_DOWNLOAD, "Enter Download"},
    {LV_SYMBOL_REFRESH, "Reboot C5"},
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_act_row[ACT_COUNT];
static lv_obj_t *s_act_icon[ACT_COUNT];
static lv_obj_t *s_act_name[ACT_COUNT];

static int s_sel = 0;
static bool s_is_busy = false;
static int s_pending = -1;
static bool s_is_reboot_ok = true;

static lv_obj_t *info_row(lv_obj_t *card, int index, const char *tag_txt, const char *val_txt) {
  lv_obj_t *tag = lv_label_create(card);
  lv_label_set_text(tag, tag_txt);
  lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(tag, current_theme.text_main, 0);
  lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 0, index * INFO_ROW_GAP);

  lv_obj_t *val = lv_label_create(card);
  lv_label_set_text(val, val_txt);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(val, current_theme.text_secondary, 0);
  lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, index * INFO_ROW_GAP);
  return val;
}

static void build_info_card(void) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(card, CONTENT_W, INFO_H);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, INFO_Y);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_hor(card, 14, 0);
  lv_obj_set_style_pad_ver(card, 8, 0);
  lv_obj_set_style_shadow_width(card, 16, 0);
  lv_obj_set_style_shadow_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);

  bool alive = spi_bridge_is_alive();
  char cver[C5_VER_BUF_LEN] = "?";
  if (alive) {
    spi_header_t hdr;
    char ver[C5_VER_BUF_LEN] = {0};
    if (spi_bridge_send_command(
            SPI_ID_SYSTEM_VERSION, NULL, 0, &hdr, (uint8_t *)ver, sizeof(ver), C5_VER_TIMEOUT_MS) ==
            ESP_OK &&
        ver[0] != '\0')
      strlcpy(cver, ver, sizeof(cver));
  }

  lv_obj_t *link_val = info_row(card, 0, "Link", alive ? "ALIVE" : "DOWN");
  if (alive)
    lv_obj_set_style_text_color(link_val, lv_color_hex(UI_COL_SUCCESS), 0);
  info_row(card, 1, "C5 FW", cver);
  info_row(card, 2, "Expected", FIRMWARE_VERSION);
}

static lv_obj_t *make_action_row(lv_obj_t *parent, int i) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, CONTENT_W, ROW_H);
  lv_obj_align(
      row,
      LV_ALIGN_TOP_MID,
      0,
      LV_MIN(ACT_Y, ui_screen_h() - UI_CHROME_FOOTER_H - ((ACT_COUNT - 1) * ROW_STEP + ROW_H)) +
          i * ROW_STEP);
  lv_obj_set_style_radius(row, 9, 0);
  lv_obj_set_style_bg_color(row, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 2, 0);
  lv_obj_set_style_pad_left(row, 12, 0);
  lv_obj_set_style_pad_right(row, 12, 0);
  lv_obj_set_style_pad_top(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *ic = lv_label_create(row);
  lv_label_set_text(ic, ACTIONS[i].sym);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_16, 0);
  lv_obj_set_width(ic, 22);
  lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, ACTIONS[i].name);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_left(name, 10, 0);
  lv_obj_set_flex_grow(name, 1);

  s_act_icon[i] = ic;
  s_act_name[i] = name;
  return row;
}

static void refresh_selection(void) {
  const lv_color_t accent = current_theme.border_accent;
  const lv_color_t dim = current_theme.text_secondary;
  for (int i = 0; i < ACT_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_border_color(s_act_row[i], sel ? accent : current_theme.border_inactive, 0);
    lv_obj_set_style_border_opa(s_act_row[i], sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(
        s_act_row[i], sel ? lv_color_hex(COL_RAISE) : current_theme.bg_secondary, 0);
    lv_obj_set_style_shadow_width(s_act_row[i], sel ? 14 : 0, 0);
    lv_obj_set_style_shadow_color(s_act_row[i], accent, 0);
    lv_obj_set_style_shadow_spread(s_act_row[i], sel ? -3 : 0, 0);
    lv_obj_set_style_text_color(s_act_icon[i], sel ? accent : dim, 0);
    lv_obj_set_style_text_color(s_act_name[i], sel ? current_theme.text_main : dim, 0);
  }
}

static void c5_op_done(void *data) {
  esp_err_t r = (esp_err_t)(intptr_t)data;
  s_is_busy = false;
  spi_bridge_set_alive(false);
  if (s_pending == ACT_REBOOT) {
    s_is_reboot_ok = (r == ESP_OK);
    return;
  }
  if (r != ESP_OK) {
    notify(NOTIFY_WARNING, "C5 download failed");
    return;
  }
  msgbox_open_info(NULL,
                   "C5 IN DOWNLOAD MODE",
                   "Flash it externally now (RELEASE UART / FLASH C5 ROM). It stays down "
                   "until re-flashed.",
                   current_theme.border_accent);
}

static void c5_op_task(void *arg) {
  (void)arg;
  esp_err_t r = (s_pending == ACT_REBOOT) ? c5_flasher_reboot() : c5_flasher_enter_download();
  ui_async_call(c5_op_done, (void *)(intptr_t)r);
  vTaskDelete(NULL);
}

static void c5_reboot_input(const input_event_t *ev, void *ctx) {
  (void)ev;
  (void)ctx;
}

static void c5_reboot_overlay_show(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, C5_REBOOT_LABEL);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(C5_REBOOT_TEXT_COLOR), 0);
  lv_obj_center(lbl);

  ui_input_set_screen_handler(c5_reboot_input, NULL);
  ui_screen_load(scr);
}

static void c5_reboot_done(lv_timer_t *timer) {
  lv_timer_delete(timer);
  ui_switch_screen(SCREEN_C5_STATUS);
  notify(s_is_reboot_ok ? NOTIFY_SAVED : NOTIFY_WARNING,
         s_is_reboot_ok ? "C5 rebooted" : "C5 reboot failed");
}

static void on_op_confirm(bool confirm) {
  if (!confirm || s_is_busy)
    return;
  s_is_busy = true;
  if (xTaskCreatePinnedToCore(
          c5_op_task, "c5_op", C5_OP_TASK_STACK, NULL, C5_OP_TASK_PRIO, NULL, SYS_CORE_RADIO) !=
      pdPASS) {
    s_is_busy = false;
    notify(NOTIFY_WARNING, "C5 busy, try again");
    return;
  }
  if (s_pending == ACT_REBOOT) {
    s_is_reboot_ok = true;
    c5_reboot_overlay_show();
    lv_timer_t *timer = lv_timer_create(c5_reboot_done, C5_REBOOT_HOLD_MS, NULL);
    lv_timer_set_repeat_count(timer, 1);
  }
}

static void c5_status_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (ev->button) {
    case INPUT_BTN_BACK:
      if (press)
        ui_switch_screen(SCREEN_SETTINGS_DEV);
      break;
    case INPUT_BTN_DOWN:
      if (nav) {
        s_sel = (s_sel + 1) % ACT_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav) {
        s_sel = (s_sel - 1 + ACT_COUNT) % ACT_COUNT;
        refresh_selection();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
      if (press) {
        ui_feedback(UI_FB_SELECT);
        if (s_sel == ACT_CONSOLE) {
          ui_switch_screen(SCREEN_C5_CONSOLE);
        } else if (s_sel == ACT_DOWNLOAD) {
          s_pending = ACT_DOWNLOAD;
          msgbox_open(NULL,
                      "Put the C5 in ROM download mode?\nIt stops running until re-flashed.",
                      "Enter",
                      "Cancel",
                      on_op_confirm);
        } else {
          s_pending = ACT_REBOOT;
          msgbox_open(NULL, "Reboot the C5 now?", "Reboot", "Cancel", on_op_confirm);
        }
      }
      break;
    default:
      break;
  }
}

void ui_c5_status_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  build_info_card();
  for (int i = 0; i < ACT_COUNT; i++)
    s_act_row[i] = make_action_row(s_screen, i);
  refresh_selection();

  ui_chrome_footer(s_screen, "UP/DOWN select   OK run   BACK exit");

  ui_input_set_screen_handler(c5_status_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}
