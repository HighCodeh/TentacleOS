# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
#

"""Exercise the complete Sub-GHz sender with host LVGL after test_keyboard_ui.py."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
APPS = ROOT / "firmware_p4/components/Applications/SubGhz"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "subghz-send"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "screens/subghz/subghz_send_ui.c").read_text().split("#include", 1)[0]
for name in ("ui_chrome.h", "ui_metrics.h", "ui_feedback.h", "ui_manager.h", "ui_theme.h",
             "assets_manager.h", "led_control.h", "notify_ui.h", "octobit_ui.h", "input_manager.h"):
    (WORK / name).write_text(NOTICE + "#pragma once\n")
(WORK / "esp_attr.h").write_text(NOTICE + "#define EXT_RAM_BSS_ATTR\n")
(WORK / "esp_err.h").write_text(NOTICE + """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 0x106
static const char *esp_err_to_name(int error) { (void)error; return "failure"; }
""")
(WORK / "esp_log.h").write_text(NOTICE + "#define ESP_LOGE(tag, ...) ((void)(tag))\n")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "subghz_replay.h"
#include "subghz_storage.h"
#define SCREEN_SUBGHZ_MENU 1
#define UI_FB_NAV 1
#define NOTIFY_WARNING 1
#define UI_CHROME_HEADER_H 46
#define UI_CHROME_FOOTER_H 22
enum { INPUT_ACTION_PRESS, INPUT_ACTION_REPEAT, INPUT_ACTION_RELEASE, INPUT_ACTION_LONG_PRESS };
enum { INPUT_BTN_BACK, INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT, INPUT_BTN_OK };
typedef struct { int action, button; } input_event_t;
static struct {
  lv_color_t screen_base, bg_primary, bg_secondary, border_accent;
  lv_color_t border_inactive, text_main, text_secondary;
} current_theme;
static unsigned sends, stops, loads, frees;
static bool fail_load;
static bool input_is_down(int button) { assert(button == INPUT_BTN_OK); return false; }
static int32_t pulses[] = {350, -1000};
static lv_image_dsc_t *assets_get(const char *path) { (void)path; return NULL; }
static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
static void ui_feedback(int event) { assert(event == UI_FB_NAV); }
static lv_obj_t *ui_chrome_screen_create(void) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(screen, 0, 0); lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE); return screen;
}
static void ui_chrome_header(lv_obj_t *root, const char *title, const char *icon) {
  (void)icon; lv_obj_t *label = lv_label_create(root); lv_label_set_text(label, title);
}
static lv_obj_t *ui_chrome_footer(lv_obj_t *root, const char *text) {
  lv_obj_t *label = lv_label_create(root); lv_label_set_text(label, text);
  lv_obj_set_width(label, ui_screen_w()); lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0); return label;
}
static void ui_input_set_screen_handler(void (*cb)(const input_event_t *, void *), void *ctx) {
  assert(cb != NULL && ctx == NULL);
}
static void clear_slot(lv_event_t *event) {
  lv_obj_t **slot = lv_event_get_user_data(event);
  if (*slot == lv_event_get_target(event)) *slot = NULL;
}
static void ui_screen_load_owned(lv_obj_t **slot, lv_obj_t *root) {
  *slot = root; lv_obj_add_event_cb(root, clear_slot, LV_EVENT_DELETE, slot); lv_screen_load(root);
}
static void ui_switch_screen(int screen) { assert(screen == SCREEN_SUBGHZ_MENU); }
static void led_signal_error(void) { assert(fail_load); }
static void notify(int type, const char *message) { (void)type; (void)message; assert(false); }
static void octobit_create(lv_obj_t *root, const char *text) { (void)root; (void)text; }
esp_err_t subghz_replay_load(const char *name, subghz_replay_t *out) {
  assert(strlen(name) != 0); ++loads;
  *out = (subghz_replay_t){0};
  if (fail_load) return ESP_ERR_NOT_SUPPORTED;
  out->pulses = pulses; out->count = 2; out->config.frequency_hz = 315000000;
  strcpy(out->protocol, "Princeton"); strcpy(out->key, "00 00 00 00 AB CD EF 01"); return ESP_OK;
}
static unsigned raw_refs;
esp_err_t subghz_receiver_acquire_raw(uint32_t sequence, subghz_raw_file_t **out) {
  if (sequence != 2) { *out=NULL; return ESP_ERR_NOT_SUPPORTED; }
  *out=(subghz_raw_file_t *)1; ++raw_refs; return ESP_OK;
}
void subghz_raw_file_release(subghz_raw_file_t *file) { if (file) { assert(raw_refs); --raw_refs; } }
void subghz_replay_from_raw(subghz_raw_file_t *file, subghz_replay_t *out) {
  ++raw_refs; *out=(subghz_replay_t){.raw_file=file}; out->config.frequency_hz=315000000;
  strcpy(out->protocol,"RAW");
}
esp_err_t subghz_receiver_copy_capture(uint32_t sequence, char **out) {
  *out = strdup("capture"); return ESP_OK;
}
esp_err_t subghz_replay_prepare(const char *text, subghz_replay_t *out) {
  return subghz_replay_load(text, out);
}
void subghz_replay_free(subghz_replay_t *replay) {
  if (replay->pulses) ++frees;
  subghz_raw_file_release(replay->raw_file);
  *replay = (subghz_replay_t){0};
}
esp_err_t subghz_replay_send(const subghz_replay_t *replay) {
  assert(replay->pulses == pulses || replay->raw_file); ++sends; return ESP_OK;
}
void subghz_tx_stop(void) { ++stops; }
void subghz_tx_get_status(subghz_tx_status_t *out) { *out = (subghz_tx_status_t){0}; }
int subghz_storage_list(subghz_storage_entry_t *out, int capacity) {
  assert(capacity > 0); *out = (subghz_storage_entry_t){.frequency=315000000};
  strcpy(out->name, "Garage remote 315MHz"); strcpy(out->protocol, "Princeton"); return 1;
}
"""
code += f'\n#include "{UI / "screens/subghz/subghz_send_ui.c"}"\n'
code += r"""
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *buffer) {
  (void)area; (void)buffer; lv_display_flush_ready(display);
}
static void check_detail(void) {
  lv_obj_update_layout(s_screen);
  lv_obj_t *content = lv_obj_get_parent(s_status_label);
  for (uint32_t i = 0; i < lv_obj_get_child_count(content); ++i) {
    lv_obj_t *label = lv_obj_get_child(content, i);
    lv_area_t area; lv_obj_get_coords(label, &area);
    assert(area.x1 >= 0 && area.x2 < ui_screen_w());
    assert(area.y1 >= UI_CHROME_HEADER_H && area.y2 < ui_screen_h() - UI_CHROME_FOOTER_H);
  }
  assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen, 0), current_theme.screen_base));
  assert(lv_color_eq(lv_obj_get_style_text_color(s_status_label, 0), current_theme.border_accent));
}
int main(void) {
  lv_init(); lv_display_t *display = lv_display_create(240, 320);
  _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320 * 20 * 2];
  lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  lv_obj_t *blank = lv_screen_active();
  for (unsigned wide = 0; wide < 2; ++wide) {
    lv_display_set_resolution(display, wide ? 320 : 240, wide ? 240 : 320);
    for (unsigned theme = 0; theme < 2; ++theme) {
      current_theme.screen_base = lv_color_hex(theme ? 0xffffff : 0x101010);
      current_theme.bg_primary = current_theme.bg_secondary = current_theme.screen_base;
      current_theme.text_main = current_theme.text_secondary = lv_color_hex(theme ? 0x101010 : 0xffffff);
      current_theme.border_accent = current_theme.text_main;
      ui_subghz_send_select("Long signal name 12345678901234567890123");
      ui_subghz_send_open(); assert(s_view == SUBGHZ_SEND_DETAIL); check_detail();
      unsigned before = sends;
      input_event_t event = {INPUT_ACTION_PRESS, INPUT_BTN_OK}; subghz_send_input(&event, NULL);
      event.action = INPUT_ACTION_RELEASE; subghz_send_input(&event, NULL);
      refresh_tick_cb(NULL); assert(sends == before + 1); check_detail();
      ui_subghz_send_stop(); lv_screen_load(blank); delete_screen();
      ui_subghz_send_open(); assert(s_view == SUBGHZ_SEND_LIST);
      event.action = INPUT_ACTION_PRESS; subghz_send_input(&event, NULL);
      assert(s_view == SUBGHZ_SEND_DETAIL && sends == before + 1); check_detail();
      ui_subghz_send_stop(); lv_screen_load(blank); delete_screen();
      fail_load = true; ui_subghz_send_select("Unsupported"); ui_subghz_send_open(); check_detail();
      subghz_send_input(&event, NULL); assert(sends == before + 1);
      ui_subghz_send_stop(); lv_screen_load(blank); delete_screen(); fail_load = false;
    }
  }
  assert(frees == 8 && loads == 12);
  assert(ui_subghz_send_capture(1) == ESP_OK);
  unsigned prepared_loads = loads, previous_sends = sends;
  ui_subghz_send_open();
  assert(s_view == SUBGHZ_SEND_DETAIL && s_pending_replay.pulses == NULL);
  assert(s_replay.pulses != NULL && loads == prepared_loads && sends == previous_sends);
  ui_subghz_send_stop(); lv_screen_load(blank); delete_screen();
  prepared_loads = loads; previous_sends = sends;
  assert(ui_subghz_send_capture(2) == ESP_OK && raw_refs == 1);
  ui_subghz_send_open(); check_detail();
  assert(s_replay.raw_file && !s_replay.pulses && !s_pending_replay.raw_file);
  assert(loads == prepared_loads && sends == previous_sends);
  start_frame(); assert(sends == previous_sends + 1);
  ui_subghz_send_stop(); lv_screen_load(blank); delete_screen();
  assert(raw_refs == 0);
  puts("PASS: pending SD RAW handoff, stream-only Send and final reference release");
  puts("PASS: complete send screen, names/metadata, no send on entry, cleanup and errors; light/dark, portrait/landscape");
}
"""
test = WORK / "test.c"
test.write_text(NOTICE + code)
command = ["cc", "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(WORK), "-I" + str(BASE)]
for folder in (UI / "screens/subghz/include", APPS / "include", APPS / "protocols/include",
               ROOT / "firmware_p4/components/Drivers/cc1101/include",
               ROOT / "firmware_p4/managed_components/lvgl__lvgl"):
    command += ["-I" + str(folder)]
command += [str(test), str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True, timeout=60)
subprocess.run([str(WORK / "test")], check=True, timeout=30)
