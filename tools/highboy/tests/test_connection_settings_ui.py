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

"""Exercise connection settings drafts, apply results and lifecycle with real LVGL."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
SERVICE = ROOT / "firmware_p4/components/Service"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "connections"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "screens/connection_settings/connection_radio_ui.c").read_text().split("#include", 1)[0]


def write(name, text):
    path = WORK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(NOTICE + "#pragma once\n" + text)
    return path


for name in ("st7789.h", "ui_chrome.h", "ui_metrics.h", "ui_feedback.h", "ui_manager.h",
             "ui_theme.h", "assets_manager.h", "led_control.h", "notify_ui.h"):
    write(name, "")
write("esp_err.h", """typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_FAIL -1
static const char *esp_err_to_name(int error) { (void)error; return "failure"; }
""")
write("freertos/FreeRTOS.h", """typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
""")
write("esp_log.h", "#define ESP_LOGI(tag, ...) ((void)(tag))\n#define ESP_LOGE(tag, ...) ((void)(tag))\n")
write("esp_attr.h", "#define EXT_RAM_BSS_ATTR\n")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#define SCREEN_CONNECTION_SETTINGS 1
#define SCREEN_CONNECT_WIFI 2
#define UI_FB_NAV 1
#define NOTIFY_WARNING 1
enum { INPUT_ACTION_PRESS, INPUT_ACTION_REPEAT, INPUT_ACTION_RELEASE };
enum { INPUT_BTN_BACK, INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT, INPUT_BTN_OK };
typedef struct { int action, button; } input_event_t;
static struct {
  lv_color_t screen_base, bg_primary, bg_secondary, border_accent, border_interface;
  lv_color_t border_inactive, text_main, bg_item_top, bg_item_bot;
} current_theme;
static unsigned backs, notices;
static lv_image_dsc_t *assets_get(const char *path) { (void)path; return NULL; }
static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
static void ui_feedback(int event) { assert(event == UI_FB_NAV); }
static void ui_chrome_gradient(lv_obj_t *obj, lv_color_t a, lv_color_t b, int h) {
  (void)b; (void)h; lv_obj_set_style_bg_color(obj, a, 0);
}
static lv_obj_t *ui_chrome_light_title(lv_obj_t *root, const char *title) {
  lv_obj_t *label = lv_label_create(root); lv_label_set_text(label, title); return label;
}
static lv_obj_t *ui_chrome_footer(lv_obj_t *root, const char *text) {
  lv_obj_t *footer = lv_obj_create(root);
  lv_obj_set_size(footer, lv_pct(100), 22);
  lv_obj_set_style_pad_all(footer, 0, 0); lv_obj_set_style_border_width(footer, 0, 0);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_t *label = lv_label_create(footer); lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_center(label); return footer;
}
static void ui_chrome_set_breadcrumb_root(const char *title) { (void)title; }
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
static void ui_switch_screen(int screen) { assert(screen == SCREEN_CONNECTION_SETTINGS || screen == SCREEN_CONNECT_WIFI); ++backs; }
static void led_signal_info(void) { ++notices; }
static void led_signal_error(void) { ++notices; }
static void notify(int type, const char *message) { (void)type; (void)message; ++notices; }
"""
code += r"""
#include "connection_settings.h"
#include "keyboard_ui.h"
static connection_settings_status_t mock_status;
static tos_config_wifi_t mock_wifi;
static tos_config_ble_t mock_ble;
static unsigned requests;
static bool keyboard_visible;
static keyboard_config_t editor;
static keyboard_submit_cb_t submit_cb;
static void *submit_context;
void keyboard_open_config(const keyboard_config_t *config, keyboard_submit_cb_t cb, void *context) {
  editor = *config; submit_cb = cb; submit_context = context; keyboard_visible = true;
}
bool keyboard_is_open(void) { return keyboard_visible; }
void keyboard_close(void) { keyboard_visible = false; }
void connection_settings_get_status(connection_settings_status_t *out) { *out = mock_status; }
void connection_settings_get_config(tos_config_wifi_t *wifi, tos_config_ble_t *ble) {
  if (wifi) *wifi = mock_wifi; if (ble) *ble = mock_ble;
}
esp_err_t connection_settings_request_wifi(const tos_config_wifi_t *config) {
  assert(!mock_status.is_busy); ++requests; mock_wifi = *config;
  mock_status.is_busy = true; mock_status.kind = CONNECTION_SETTINGS_WIFI; return ESP_OK;
}
esp_err_t connection_settings_request_ble(const tos_config_ble_t *config) {
  assert(!mock_status.is_busy); ++requests; mock_ble = *config;
  mock_status.is_busy = true; mock_status.kind = CONNECTION_SETTINGS_BLE; return ESP_OK;
}
static lv_obj_t *ui_chrome_screen_create(void) {
  lv_obj_t *root = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(root, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(root, 0, 0); return root;
}
"""
for path in (UI / "components/toggle/toggle_ui.c",
             UI / "components/intensity_bar/intensity_bar_ui.c",
             UI / "components/menu_component/menu_component_ui.c",
             UI / "screens/connection_settings/connection_radio_ui.c"):
    code += f'\n#include "{path}"\n'
code += r"""
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *buffer) {
  (void)area; (void)buffer; lv_display_flush_ready(display);
}
static void send(int button) {
  input_event_t event = {INPUT_ACTION_PRESS, button}; input(&event, NULL);
}
static void check_layout(void) {
  lv_obj_update_layout(s_screen);
  assert(s_menu.item_count == (s_kind == CONNECTION_SETTINGS_WIFI ? 7 : 6));
  lv_obj_t *labels[] = {s_menu.items[s_menu.selected], s_menu.hint_label};
  lv_obj_scroll_to_view(s_menu.items[s_menu.selected], LV_ANIM_OFF);
  lv_obj_update_layout(s_screen);
  for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
    assert(labels[i] != NULL);
    lv_area_t area; lv_obj_get_coords(labels[i], &area);
    assert(area.x1 >= 0 && area.x2 < ui_screen_w());
    assert(area.y1 >= 46 && area.y2 < ui_screen_h());
  }
  assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen, 0), current_theme.screen_base));
  assert(lv_color_eq(lv_obj_get_style_text_color(s_menu.val_labels[1], 0), current_theme.border_accent));
}
static void close_screen(lv_obj_t *blank) {
  ui_connection_radio_close(); assert(s_timer == NULL && !keyboard_visible);
  lv_screen_load(blank); lv_obj_delete(s_screen); assert(s_screen == NULL);
}
static void set_text(const char *text) {
  assert(keyboard_visible); keyboard_close(); submit_cb(text, submit_context);
}
static void result(bool applied, int error) {
  mock_status.is_busy = false; mock_status.has_result = true;
  mock_status.is_applied = applied; mock_status.error = error; ++mock_status.revision; refresh(NULL);
}
static void select_row(unsigned row) { menu_component_select(&s_menu, row); check_layout(); }
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
      current_theme.bg_primary = current_theme.screen_base;
      current_theme.bg_secondary = current_theme.screen_base;
      current_theme.text_main = lv_color_hex(theme ? 0x101010 : 0xffffff);
      current_theme.border_accent = current_theme.text_main;
      mock_wifi = (tos_config_wifi_t){.enabled=false, .ap={.ssid="Highboy", .password="original", .max_conn=8}};
      mock_ble = (tos_config_ble_t){.name="Highboy",.max_conn=3,.advertising=true};
      ui_connection_radio_wifi_open(); check_layout();
      unsigned before = requests; send(INPUT_BTN_OK); assert(s_wifi.enabled && !mock_wifi.enabled);
      select_row(CONNECTION_RADIO_LIMIT); send(INPUT_BTN_RIGHT); assert(s_wifi.ap.max_conn == 1);
      send(INPUT_BTN_LEFT); assert(s_wifi.ap.max_conn == 8);
      select_row(CONNECTION_RADIO_NAME); send(INPUT_BTN_OK);
      assert(editor.mode == KEYBOARD_TEXT && editor.max_chars == 32); set_text("12345678901234567890123456789012");
      assert(strlen(s_wifi.ap.ssid) == 32); check_layout();
      select_row(CONNECTION_RADIO_OPTION); send(INPUT_BTN_OK);
      assert(editor.mode == KEYBOARD_PASSWORD && editor.min_chars == 0); set_text("short");
      assert(!strcmp(s_wifi.ap.password, "original"));
      send(INPUT_BTN_OK); set_text("another-password");
      assert(strstr(lv_label_get_text(s_menu.val_labels[CONNECTION_RADIO_OPTION]), "another") == NULL);
      assert(requests == before); select_row(CONNECTION_RADIO_APPLY); send(INPUT_BTN_OK);
      assert(requests == before + 1 && mock_status.is_busy);
      select_row(CONNECTION_RADIO_POWER); send(INPUT_BTN_OK); assert(s_wifi.enabled);
      result(false, ESP_FAIL); assert(strstr(lv_label_get_text(s_status_label), "Failed"));
      select_row(CONNECTION_RADIO_APPLY); send(INPUT_BTN_OK); result(true, ESP_FAIL);
      assert(s_is_dirty && strstr(lv_label_get_text(s_status_label), "save failed"));
      select_row(CONNECTION_RADIO_STATUS); check_layout();
      lv_obj_update_layout(s_screen); assert(lv_obj_get_height(s_status_label) <= 20);
      select_row(CONNECTION_RADIO_APPLY); send(INPUT_BTN_OK); result(true, ESP_OK);
      assert(!s_is_dirty && !strcmp(lv_label_get_text(s_status_label), "Applied and saved"));
      send(INPUT_BTN_BACK); close_screen(blank);
      ui_connection_radio_wifi_open(); assert(s_wifi.enabled); select_row(CONNECTION_RADIO_JOIN); send(INPUT_BTN_OK);
      close_screen(blank); ui_connection_radio_ble_open(); check_layout();
      select_row(CONNECTION_RADIO_LIMIT); send(INPUT_BTN_RIGHT); assert(s_ble.max_conn == 1);
      select_row(CONNECTION_RADIO_OPTION); send(INPUT_BTN_OK); assert(!s_ble.advertising);
      select_row(CONNECTION_RADIO_NAME); send(INPUT_BTN_OK); assert(editor.max_chars == 29);
      set_text("My Highboy"); select_row(CONNECTION_RADIO_APPLY); send(INPUT_BTN_OK);
      send(INPUT_BTN_BACK); close_screen(blank); result(true, ESP_OK);
      ui_connection_radio_ble_open(); assert(!strcmp(s_ble.name, "My Highboy"));
      select_row(CONNECTION_RADIO_NAME); send(INPUT_BTN_OK); close_screen(blank);
      assert(!keyboard_visible); ui_connection_radio_ble_open();
      assert(!strcmp(s_ble.name, "My Highboy"));
      for (unsigned row = 0; row < (unsigned)s_menu.item_count; ++row) select_row(row);
      close_screen(blank);
    }
  }
  puts("PASS: real LVGL connection settings, drafts, password masking, failures/retry, background completion, exit/re-entry, themes and orientations");
}
"""
test = WORK / "test.c"
test.write_text(NOTICE + code)
command = [os.environ.get("CC", "cc"), "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(WORK), "-I" + str(BASE)]
for folder in ("screens/connection_settings", "components/keyboard", "components/menu_component", "components/toggle", "components/intensity_bar"):
    command += ["-I" + str(UI / folder / "include")]
command += ["-I" + str(SERVICE / "connection_settings/include"),
            "-I" + str(SERVICE / "storage_api/include"),
            "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"), str(test),
            str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True, timeout=60)
subprocess.run([str(WORK / "test")], check=True, timeout=30)
