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

#include "rf_bench_ui.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"
#include "led_control.h"
#include "menu_component_ui.h"
#include "power_policy.h"
#include "rf_passive.h"
#include "rf_tag_test.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define REFRESH_MS     100U
#define MARGIN         10
#define GAP            8
#define PATH_COUNT     4U
#define UID_LINE_BYTES 5U
#define DIAGRAM_H      94
#define SWITCH_H       22
#define BRANCH_Y       42
#define BRANCH_GAP     6
#define VIEW_MENU      0
#define VIEW_SWITCH    1
#define VIEW_NFC       2
#define VIEW_RFID      3
#define VIEW_EXIT      4
static const char *TAG = "RF_BENCH_UI";
static const char *BRANCHES[] = {"L16 / 51 nH", "L17 / 16 nH", "L18 / 1.8 nH", "L19 / 1.5 nH"};
static const char *TECHS[] = {"NFC-A", "NFC-B", "NFC-F", "NFC-V"};
static lv_obj_t *s_screen;
static lv_obj_t *s_body;
static lv_obj_t *s_title;
static lv_obj_t *s_detail;
static lv_obj_t *s_footer;
static lv_obj_t *s_diagram;
static lv_obj_t *s_paths[PATH_COUNT];
static lv_obj_t *s_cards[PATH_COUNT];
static lv_point_precise_t s_path_points[PATH_COUNT][3];
static const char *PARTS[] = {"L16\n51 nH", "L17\n16 nH", "L18\n1.8 nH", "L19\n1.5 nH"};
static int s_drawn_path = -1;
static bool s_drawn_confirmed;
static lv_timer_t *s_timer;
static menu_component_t s_menu;
static unsigned s_view;
static bool s_wait_back;
static bool s_started;
static esp_err_t s_error;

static void build(unsigned view);
static void refresh(lv_timer_t *timer);
/* Functional view of U10's shunt branches, not four separate antennas. */
static lv_obj_t *diagram_box(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, w, h);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_style_radius(box, 6, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, current_theme.text_secondary, 0);
  lv_obj_set_style_bg_color(box, current_theme.screen_base, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return box;
}
static void diagram_create(lv_obj_t *parent) {
  int width = ui_screen_w() - MARGIN * 2;
  int card_w = (width - BRANCH_GAP * (PATH_COUNT - 1)) / PATH_COUNT;
  s_diagram = diagram_box(parent, 0, 0, width, DIAGRAM_H);
  lv_obj_set_style_border_width(s_diagram, 0, 0);
  lv_obj_set_style_bg_opa(s_diagram, LV_OPA_TRANSP, 0);
  lv_obj_t *chip = diagram_box(s_diagram, (width - 116) / 2, 0, 116, SWITCH_H);
  lv_obj_t *label = lv_label_create(chip);
  lv_label_set_text(label, "U10  PE613050");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  lv_obj_center(label);
  for (unsigned i = 0; i < PATH_COUNT; i++) {
    int x = i * (card_w + BRANCH_GAP);
    s_path_points[i][0] = (lv_point_precise_t){width / 2, SWITCH_H};
    s_path_points[i][1] = (lv_point_precise_t){x + card_w / 2, BRANCH_Y - GAP};
    s_path_points[i][2] = (lv_point_precise_t){x + card_w / 2, BRANCH_Y};
    s_paths[i] = lv_line_create(s_diagram);
    lv_line_set_points(s_paths[i], s_path_points[i], 3);
    lv_obj_set_style_line_rounded(s_paths[i], true, 0);
    s_cards[i] = diagram_box(s_diagram, x, BRANCH_Y, card_w, DIAGRAM_H - BRANCH_Y);
    label = lv_label_create(s_cards[i]);
    lv_label_set_text_fmt(label, "RF%u\n%s", i + 1, PARTS[i]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, current_theme.text_main, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
  }
  s_drawn_path = -1;
}
static void diagram_update(uint8_t actual, bool confirmed) {
  if (s_diagram == NULL || (s_drawn_path == actual && s_drawn_confirmed == confirmed))
    return;
  s_drawn_path = actual;
  s_drawn_confirmed = confirmed;
  lv_color_t accent = lv_color_hex(confirmed ? UI_COL_SUCCESS : UI_COL_WARNING);
  for (unsigned i = 0; i < PATH_COUNT; i++) {
    bool selected = actual == i + 1;
    lv_obj_set_style_line_color(s_paths[i], selected ? accent : current_theme.text_secondary, 0);
    lv_obj_set_style_line_width(s_paths[i], selected ? 3 : 1, 0);
    lv_obj_set_style_line_opa(s_paths[i], selected ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_border_color(s_cards[i], selected ? accent : current_theme.text_secondary, 0);
    lv_obj_set_style_border_width(s_cards[i], selected ? 2 : 1, 0);
    lv_obj_set_style_border_opa(s_cards[i], selected ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_bg_color(s_cards[i],
                              selected ? lv_color_mix(accent, current_theme.screen_base, 30)
                                       : current_theme.screen_base,
                              0);
  }
}
static void report_error(esp_err_t error) {
  s_error = error;
  ESP_LOGE(TAG, "Request: %s", esp_err_to_name(error));
  led_signal_error();
}
void ui_rf_bench_close(void) {
  rf_tag_test_stop();
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  ui_rf_bench_close();
  s_screen = s_body = s_title = s_detail = s_footer = s_diagram = NULL;
}
static void input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool press = event->action == INPUT_ACTION_PRESS;
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (s_wait_back)
    return;
  if (s_view == VIEW_MENU) {
    if (nav && event->button == INPUT_BTN_UP)
      menu_component_prev(&s_menu);
    if (nav && event->button == INPUT_BTN_DOWN)
      menu_component_next(&s_menu);
    if (press && event->button == INPUT_BTN_OK)
      build(menu_component_get_selected(&s_menu) + 1);
    if (press && event->button == INPUT_BTN_BACK)
      build(VIEW_EXIT);
    return;
  }
  if (nav && s_body != NULL && (event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN))
    lv_obj_scroll_by(s_body, 0, event->button == INPUT_BTN_UP ? 30 : -30, LV_ANIM_OFF);
  if (!press)
    return;
  if (event->button == INPUT_BTN_BACK) {
    if (rf_tag_test_busy()) {
      rf_tag_test_stop();
      s_wait_back = true;
    } else
      build(VIEW_MENU);
  } else if (s_view == VIEW_SWITCH &&
             (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT)) {
    rf_passive_status_t status = {0};
    rf_passive_snapshot(&status);
    int direction = event->button == INPUT_BTN_LEFT ? -1 : 1;
    uint8_t path = (status.path - 1 + PATH_COUNT + direction) % PATH_COUNT + 1;
    esp_err_t error = rf_passive_select(path);
    if (error != ESP_OK)
      report_error(error);
    else
      s_error = ESP_OK;
  } else if ((s_view == VIEW_NFC || s_view == VIEW_RFID) && event->button == INPUT_BTN_OK) {
    if (rf_tag_test_busy())
      rf_tag_test_stop();
    else if (s_timer == NULL)
      report_error(ESP_ERR_NO_MEM);
    else {
      esp_err_t error = rf_tag_test_start(s_view == VIEW_NFC ? RF_TAG_TEST_NFC : RF_TAG_TEST_LF);
      if (error != ESP_OK)
        report_error(error);
      else {
        s_error = ESP_OK;
        s_started = true;
      }
    }
  } else if (s_view == VIEW_EXIT && event->button == INPUT_BTN_OK && !rf_tag_test_busy()) {
    rf_passive_status_t status = {0};
    rf_passive_snapshot(&status);
    esp_err_t error = rf_passive_save_boot(false, status.path);
    if (error == ESP_OK)
      esp_restart();
    else
      report_error(error);
  }
  refresh(NULL);
}
static void refresh(lv_timer_t *timer) {
  (void)timer;
  if (s_screen == NULL || s_view == VIEW_MENU)
    return;
  rf_tag_test_status_t scan;
  rf_tag_test_snapshot(&scan);
  if (s_wait_back && !scan.busy) {
    s_wait_back = false;
    build(VIEW_MENU);
    return;
  }
  rf_passive_status_t bench = {0};
  rf_passive_snapshot(&bench);
  char text[420];
  if (s_view == VIEW_SWITCH) {
    bool valid = bench.actual_path >= 1 && bench.actual_path <= PATH_COUNT;
    lv_label_set_text_fmt(s_title,
                          "RF%u  %s",
                          bench.path,
                          bench.path >= 1 && bench.path <= PATH_COUNT ? BRANCHES[bench.path - 1]
                                                                      : "Invalid path");
    diagram_update(bench.actual_path,
                   valid && bench.actual_path == bench.path && bench.error == ESP_OK);
    if (valid)
      snprintf(text,
               sizeof(text),
               "Readback: RF%u | %s\nGPIO17 / V1: %u   GPIO18 / V2: %u",
               bench.actual_path,
               bench.error == ESP_OK ? "Selection saved" : esp_err_to_name(bench.error),
               (bench.actual_path - 1) >> 1,
               (bench.actual_path - 1) & 1);
    else
      snprintf(text, sizeof(text), "Readback unavailable\n%s", esp_err_to_name(bench.error));
  } else if (s_view == VIEW_EXIT) {
    lv_label_set_text(s_title, "Return to full firmware");
    snprintf(text,
             sizeof(text),
             "OK clears the lab boot profile\nand restarts normally.\n\nBACK keeps the lab open.");
  } else {
    const char *state = !s_started                            ? "Ready - field OFF"
                        : scan.phase == RF_TAG_TEST_FOUND     ? "TAG READ - field OFF"
                        : scan.phase == RF_TAG_TEST_TIMEOUT   ? "No valid tag - field OFF"
                        : scan.phase == RF_TAG_TEST_CANCELLED ? "Stopped - field OFF"
                        : scan.phase == RF_TAG_TEST_ERROR     ? "Read error - field OFF"
                        : scan.phase == RF_TAG_TEST_HELD      ? "OFF NOT CONFIRMED"
                        : s_wait_back                         ? "Stopping field..."
                                                              : "Searching - active field";
    lv_label_set_text(s_title, state);
    lv_obj_set_style_text_color(s_title,
                                s_started && scan.phase == RF_TAG_TEST_FOUND
                                    ? lv_color_hex(UI_COL_SUCCESS)
                                    : current_theme.text_main,
                                0);
    if (!s_started) {
      snprintf(text,
               sizeof(text),
               "%s\n\nPlace one tag on the antenna.\nOK starts a 30-second search.\nOther radios "
               "remain idle.\nBACK returns to the lab.",
               s_view == VIEW_NFC ? "NFC A / B / F / V: read identity"
                                  : "RFID 125 kHz: EM4100 RF/64\nGPIO2: carrier | GPIO11: low");
    } else if (scan.phase == RF_TAG_TEST_FOUND) {
      char uid[RF_TAG_TEST_UID_MAX * 3 + 2] = {0};
      size_t used = 0;
      for (uint8_t i = 0; i < scan.uid_len && i < RF_TAG_TEST_UID_MAX; i++)
        used += snprintf(uid + used,
                         sizeof(uid) - used,
                         "%02X%s",
                         scan.uid[i],
                         i + 1 == scan.uid_len           ? ""
                         : (i + 1) % UID_LINE_BYTES == 0 ? "\n"
                                                         : " ");
      snprintf(text,
               sizeof(text),
               "%s\nUID / ID:\n%s\n\n%s\nOK: read another tag",
               scan.kind == RF_TAG_TEST_LF ? "EM4100 RF/64" : TECHS[scan.tech % PATH_COUNT],
               uid,
               scan.kind == RF_TAG_TEST_LF ? "Parity OK; 3 matching frames"
                                           : "Identity read; memory not dumped");
    } else if (s_view == VIEW_RFID) {
      snprintf(text,
               sizeof(text),
               "CL: %lu Hz measured\nDA: %lu edges | lost: %lu\nInvalid pulses: %lu\nAttempts: %lu "
               "| %lu / 30 s\n\n%s\n%s",
               (unsigned long)scan.cl_hz,
               (unsigned long)scan.da_edges,
               (unsigned long)scan.dropped,
               (unsigned long)scan.invalid_pulses,
               (unsigned long)scan.attempts,
               (unsigned long)(scan.elapsed_ms / 1000),
               scan.da_edges == 0 ? "No DA data received yet" : "Waiting for a valid EM4100 ID",
               scan.cleanup_error != ESP_OK ? esp_err_to_name(scan.cleanup_error)
               : scan.error != ESP_OK       ? esp_err_to_name(scan.error)
                                            : "GPIO2 carrier / GPIO11 low");
    } else {
      snprintf(text,
               sizeof(text),
               "Polling: %s\nAttempts: %lu | %lu / 30 s\n\nKeep the tag on the antenna.\n%s",
               TECHS[scan.tech % PATH_COUNT],
               (unsigned long)scan.attempts,
               (unsigned long)(scan.elapsed_ms / 1000),
               scan.cleanup_error != ESP_OK ? esp_err_to_name(scan.cleanup_error)
               : scan.error != ESP_OK       ? esp_err_to_name(scan.error)
                                            : "Reading tag identity");
    }
    ui_chrome_footer_set_text(
        s_footer, scan.busy ? "OK Stop   BACK Stop / return" : "OK Start   BACK Return");
  }
  if (s_error != ESP_OK)
    snprintf(text,
             sizeof(text),
             "Request failed\n%s\n\nBACK returns; OK retries",
             esp_err_to_name(s_error));
  lv_label_set_text(s_detail, text);
}
static void build(unsigned view) {
  s_view = view;
  s_wait_back = false;
  s_started = false;
  s_error = ESP_OK;
  s_body = s_title = s_detail = s_footer = s_diagram = NULL;
  lv_obj_clean(s_screen);
  if (view == VIEW_MENU) {
    s_menu = menu_component_create(s_screen, "RF LAB", "/assets/icons/tune.bin");
    menu_component_add_item(&s_menu, "/assets/icons/tune.bin", "Manual RF switch");
    menu_component_add_item(&s_menu, "/assets/icons/contactless.bin", "Read NFC identity");
    menu_component_add_item(&s_menu, "/assets/icons/sensors.bin", "Read RFID 125 kHz");
    menu_component_add_item(&s_menu, "/assets/icons/settings.bin", "Exit lab / restart");
    menu_component_set_hint(&s_menu, "UP/DOWN Select   OK Open");
    return;
  }
  const char *title = view == VIEW_SWITCH ? "MANUAL RF SWITCH"
                      : view == VIEW_NFC  ? "NFC READ"
                      : view == VIEW_RFID ? "RFID READ"
                                          : "EXIT RF LAB";
  ui_chrome_header(s_screen, title, "/assets/icons/contactless.bin");
  lv_obj_t *body = lv_obj_create(s_screen);
  s_body = body;
  lv_obj_set_pos(body, MARGIN, UI_CHROME_HEADER_H + GAP);
  lv_obj_set_size(body,
                  ui_screen_w() - MARGIN * 2,
                  ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H - GAP * 2);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, GAP, 0);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  s_title = lv_label_create(body);
  if (view == VIEW_SWITCH)
    diagram_create(body);
  s_detail = lv_label_create(body);
  lv_obj_t *labels[] = {s_title, s_detail};
  for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
    lv_obj_set_width(labels[i], LV_PCT(100));
    lv_obj_set_style_text_font(labels[i], &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(labels[i], current_theme.text_main, 0);
  }
  s_footer = ui_chrome_footer(s_screen,
                              view == VIEW_SWITCH ? LV_SYMBOL_LEFT " Switch " LV_SYMBOL_RIGHT
                                                                   "   BACK Return"
                                                  : "OK Apply   BACK Return");
  refresh(NULL);
}
void ui_rf_bench_open(void) {
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  build(VIEW_MENU);
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  power_policy_set_display_hold(s_screen, true);
  s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
  if (s_timer == NULL)
    report_error(ESP_ERR_NO_MEM);
}
