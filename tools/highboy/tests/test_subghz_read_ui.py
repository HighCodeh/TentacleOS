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

"""Exercise the complete Sub-GHz acquisition with host LVGL after test_keyboard_ui.py."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
APPS = ROOT / "firmware_p4/components/Applications/SubGhz"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "subghz-read"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "screens/subghz/subghz_read_ui.c").read_text().split("#include", 1)[0]
for name in ("ui_chrome.h", "ui_metrics.h", "ui_feedback.h", "ui_manager.h", "ui_theme.h",
             "assets_manager.h", "led_control.h", "notify_ui.h", "octobit_ui.h", "input_manager.h", "keyboard_ui.h"):
    (WORK / name).write_text(NOTICE + "#pragma once\n")
(WORK / "esp_attr.h").write_text(NOTICE + "#define EXT_RAM_BSS_ATTR\n")
(WORK / "esp_err.h").write_text(NOTICE + """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_INVALID_ARG 0x102
static const char *esp_err_to_name(int error) { (void)error; return "failure"; }
""")
(WORK / "esp_log.h").write_text(NOTICE + "#define ESP_LOGE(tag, ...) ((void)(tag))\n#define ESP_LOGD(tag, ...) ((void)(tag))\n#define ESP_LOGI(tag, ...) ((void)(tag))\n")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "subghz_receiver.h"
#include "subghz_settings.h"
#include "subghz_send_ui.h"
#include "subghz_storage.h"
#define SCREEN_SUBGHZ_MENU 1
#define SCREEN_SUBGHZ_SEND 2
#define UI_FB_READ 2
#define UI_FB_WRITE 3
#define NOTIFY_SAVED 2
#define NOTIFY_INFO 3
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
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
static unsigned stops, finishes;
static bool fail_load;
static bool ready, running;
static subghz_rx_progress_t fixture_progress;
static subghz_rx_result_t fixture_result;
static bool input_is_down(int button) { assert(button == INPUT_BTN_OK); return false; }
static int32_t pulses[] = {350, -1000};
static lv_image_dsc_t *assets_get(const char *path) { (void)path; return NULL; }
static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
static void ui_feedback(int event) { (void)event; }
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
static void ui_switch_screen(int screen) { assert(screen == SCREEN_SUBGHZ_MENU || screen == SCREEN_SUBGHZ_SEND); }
static void led_signal_info(void) {}
static void led_signal_error(void) { assert(fail_load); }
static void notify(int type, const char *message) { assert(type == NOTIFY_SAVED); }
static void octobit_create(lv_obj_t *root, const char *text) { (void)root; (void)text; }
static void ui_chrome_footer_set_text(lv_obj_t *footer, const char *text) { lv_label_set_text(footer, text); }
subghz_settings_t subghz_settings_get(void) { return (subghz_settings_t){.freq=315000000}; }
static subghz_mode_t expected_mode = SUBGHZ_MODE_SCAN;
esp_err_t subghz_receiver_start_configured(subghz_mode_t mode, const cc1101_radio_config_t *config, uint32_t freq) {
  assert(mode == expected_mode && freq == 315000000);
  running = !fail_load; return fail_load ? ESP_FAIL : ESP_OK;
}
void subghz_receiver_stop(void) { running = false; ++stops; }
void subghz_receiver_finish(void) { assert(running); ++finishes; }
esp_err_t subghz_receiver_get_error(void) { return ESP_OK; }
bool subghz_receiver_get_progress(subghz_rx_progress_t *out) { *out = fixture_progress; return true; }
bool subghz_receiver_get_result(subghz_rx_result_t *out) {
  if (!ready) return false;
  *out = fixture_result; out->data.protocol_name = out->name_buf; return true;
}
void ui_subghz_send_select(const char *name) { assert(name[0]); }
static unsigned saves, prepared;
esp_err_t subghz_receiver_save(uint32_t seq, const char *name) { ++saves; return ESP_OK; }
void subghz_receiver_discard(void) {}
esp_err_t ui_subghz_send_capture(uint32_t seq) { ++prepared; return ESP_OK; }
static void keyboard_open_filename(const char *prompt, const char *name, size_t max, void (*cb)(const char *, void *)) {
  (void)prompt; (void)name; (void)max; (void)cb;
}

"""
code += f'\n#define TAG CAPTURE_TAG\n#include "{UI / "components/capture_result/capture_result_ui.c"}"\n#undef TAG\n'
code += f'\n#include "{UI / "screens/subghz/subghz_read_ui.c"}"\n'
code += r"""
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *buffer) {
  (void)area; (void)buffer; lv_display_flush_ready(display);
}
static void check_live(void) {
  lv_obj_update_layout(s_screen);
  lv_obj_t *objects[] = {s_status, s_freq, s_graph, s_stats};
  for (unsigned i = 0; i < 4; ++i) {
    lv_area_t area; lv_obj_get_coords(objects[i], &area);
    assert(area.x1 >= 0 && area.x2 < ui_screen_w());
    assert(area.y1 >= UI_CHROME_HEADER_H && area.y2 < ui_screen_h() - UI_CHROME_FOOTER_H);
  }
  assert(lv_obj_get_content_height(s_graph) > 10);
  for (unsigned i = 0; i < fixture_progress.rssi_count; ++i) {
    assert(s_trace_points[i].x >= 0 && s_trace_points[i].x < lv_obj_get_content_width(s_graph));
    assert(s_trace_points[i].y >= 0 && s_trace_points[i].y < lv_obj_get_content_height(s_graph));
  }
  assert(lv_color_eq(lv_obj_get_style_bg_color(s_graph, 0), current_theme.bg_secondary));
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
      ready = false; fixture_progress = (subghz_rx_progress_t){.frequency_hz=315000000};
      ui_subghz_read_open(); assert(running); read_tick_cb(NULL); check_live();
      fixture_progress.rejected_bursts = 1;
      read_tick_cb(NULL); check_live();
      assert(strstr(lv_label_get_text(s_status), "Noise filtered"));
      fixture_progress.phase = SUBGHZ_RX_PHASE_CAPTURING;
      fixture_progress.rssi_count = SUBGHZ_RX_RSSI_SAMPLES;
      fixture_progress.pulse_count = 8192; fixture_progress.frame_count = 128;
      fixture_progress.elapsed_ms = 10000;
      for (unsigned i = 0; i < SUBGHZ_RX_RSSI_SAMPLES; ++i) fixture_progress.rssi_dbm[i] = -130 + i * 2;
      read_tick_cb(NULL); check_live(); assert(strstr(lv_label_get_text(s_status), "Receiving"));
      fixture_progress.pulse_count = 1310720; fixture_progress.frame_count = 160;
      fixture_progress.elapsed_ms = 7200000;
      read_tick_cb(NULL); check_live();
      fixture_progress.phase = SUBGHZ_RX_PHASE_ANALYZING; read_tick_cb(NULL); check_live();
      assert(strstr(lv_label_get_text(s_status), "Analyzing"));
      fixture_result = (subghz_rx_result_t){.decoded=true, .freq=315000000, .pulse_count=8192, .is_limited=true};
      strcpy(fixture_result.name_buf, "Princeton"); strcpy(fixture_result.save_name, "capture");
      fixture_result.data.raw_value = 0xabcdef; fixture_result.data.bit_count = 24;
      unsigned old_saves = saves;
      ready = true; read_tick_cb(NULL); assert(s_options && !running && saves == old_saves);
      lv_obj_update_layout(s_screen);
      assert(strstr(s_card_sub, "ABCDEF") && strstr(s_card_value, "315.00"));
      for (int i = 0; i < CAP_ACT_MAX; ++i) {
        lv_obj_scroll_to_view(s_cr.rows[s_cr.sel], LV_ANIM_OFF); lv_obj_update_layout(s_screen);
        lv_area_t area; lv_obj_get_coords(s_cr.rows[s_cr.sel], &area);
        assert(area.y1 >= UI_CHROME_HEADER_H && area.y2 < ui_screen_h() - UI_CHROME_FOOTER_H);
        capture_result_next(&s_cr);
      }
      send_current(); assert(prepared > 0 && saves == old_saves);
      save_name_submit("Chosen name", NULL); assert(saves == old_saves + 1 && s_saved);
      ui_subghz_read_stop(); lv_screen_load(blank); lv_obj_delete(s_screen); assert(s_screen == NULL);
      ready = false; fail_load = true; ui_subghz_read_open(); assert(!running && s_has_rx_error);
      fixture_progress.rssi_count = 0; check_live();
      ui_subghz_read_stop(); lv_screen_load(blank); lv_obj_delete(s_screen); fail_load = false;
    }
  }
  expected_mode = SUBGHZ_MODE_RAW;
  ui_subghz_read_set_raw(true); ready = false;
  ui_subghz_read_open(); assert(running);
  assert(strstr(lv_label_get_text(s_hint), "OK finish"));
  input_event_t finish = {.action=INPUT_ACTION_PRESS, .button=INPUT_BTN_OK};
  subghz_read_input(&finish, NULL); assert(finishes == 1 && running);
  fixture_result = (subghz_rx_result_t){.freq=315000000};
  strcpy(fixture_result.save_name, "RAW_capture"); ready = true;
  read_tick_cb(NULL); assert(s_options && !running);
  ui_subghz_read_stop(); lv_screen_load(blank); lv_obj_delete(s_screen);
  expected_mode = SUBGHZ_MODE_SCAN; ui_subghz_read_set_raw(false);
  ready = false; ui_subghz_read_open(); assert(running);
  ui_subghz_read_stop(); lv_screen_load(blank); lv_obj_delete(s_screen);
  puts("PASS: real Read RSSI graph, phase transitions, limits/result actions, cleanup and startup failure; both orientations/themes");
}

"""
test = WORK / "test.c"
test.write_text(NOTICE + code)
command = ["cc", "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(WORK), "-I" + str(BASE)]
for folder in (UI / "screens/subghz/include", UI / "components/capture_result/include", APPS / "include", APPS / "protocols/include",
               ROOT / "firmware_p4/components/Drivers/cc1101/include",
               ROOT / "firmware_p4/managed_components/lvgl__lvgl"):
    command += ["-I" + str(folder)]
command += [str(test), str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True, timeout=60)
subprocess.run([str(WORK / "test")], check=True, timeout=30)
