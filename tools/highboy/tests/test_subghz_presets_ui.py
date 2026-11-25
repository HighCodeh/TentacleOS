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

"""Exercise the real preset screen/menu with host LVGL after test_keyboard_ui.py."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
APPS = ROOT / "firmware_p4/components/Applications/SubGhz"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "subghz-presets"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "screens/subghz/subghz_config_ui.c").read_text().split("#include", 1)[0]


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
static const char *esp_err_to_name(int error) { (void)error; return "failure"; }
""")
write("freertos/FreeRTOS.h", """typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
""")
write("esp_log.h", "#define ESP_LOGI(tag, ...) ((void)(tag))\n#define ESP_LOGE(tag, ...) ((void)(tag))\n")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#define SCREEN_SUBGHZ_MENU 1
#define UI_CHROME_HEADER_H 46
#define UI_CHROME_FOOTER_H 22
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
static lv_obj_t *gradient_objects[64];
static int gradient_heights[64];
static unsigned gradient_count;
static void ui_chrome_gradient(lv_obj_t *obj, lv_color_t a, lv_color_t b, int h) {
  unsigned i = 0;
  while (i < gradient_count && gradient_objects[i] != obj) ++i;
  assert(i < 64); if (i == gradient_count) ++gradient_count;
  gradient_objects[i] = obj; gradient_heights[i] = h;
  (void)b; lv_obj_set_style_bg_color(obj, a, 0);
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
static void ui_switch_screen(int screen) { assert(screen == SCREEN_SUBGHZ_MENU); ++backs; }
static void led_signal_info(void) { ++notices; }
static void led_signal_error(void) { assert(false); }
static void notify(int type, const char *message) { (void)type; (void)message; assert(false); }
"""
for path in (UI / "components/toggle/toggle_ui.c",
             UI / "components/intensity_bar/intensity_bar_ui.c",
             UI / "components/menu_component/menu_component_ui.c",
             UI / "screens/subghz/subghz_config_ui.c"):
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
  assert(s_menu.item_count == 9);
  unsigned gradient = 0;
  while (gradient < gradient_count && gradient_objects[gradient] != s_menu.items[0]) ++gradient;
  assert(gradient < gradient_count);
  assert(gradient_heights[gradient] == lv_obj_get_height(s_menu.items[0]));
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
  lv_screen_load(blank); lv_obj_delete(s_screen); assert(s_screen == NULL);
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
      current_theme.bg_primary = current_theme.screen_base;
      current_theme.bg_secondary = current_theme.screen_base;
      current_theme.text_main = lv_color_hex(theme ? 0x101010 : 0xffffff);
      current_theme.border_accent = current_theme.text_main;
      subghz_settings_set_freq(433920000); subghz_settings_set_preset(CC1101_PRESET_OOK_270KHZ);
      ui_subghz_config_open(); check_layout(); send(INPUT_BTN_DOWN);
      const cc1101_preset_t expected[] = {
        CC1101_PRESET_OOK_650KHZ, CC1101_PRESET_2FSK_2KHZ, CC1101_PRESET_2FSK_47KHZ,
        CC1101_PRESET_OOK_800KHZ, CC1101_PRESET_2FSK_95KHZ, CC1101_PRESET_OOK_270KHZ};
      const char *labels[] = {"AM650", "FM238", "FM476", "AM812", "FM952", "AM270"};
      for (unsigned i = 0; i < 6; ++i) {
        send(INPUT_BTN_RIGHT); check_layout();
        assert(subghz_settings_get().preset == expected[i]);
        assert(subghz_settings_get().freq == 433920000);
        assert(strstr(lv_label_get_text(s_menu.val_labels[1]), labels[i]) != NULL);
      }
      send(INPUT_BTN_LEFT); assert(subghz_settings_get().preset == CC1101_PRESET_2FSK_95KHZ);
      send(INPUT_BTN_OK); assert(subghz_settings_get().preset == CC1101_PRESET_OOK_270KHZ);
      send(INPUT_BTN_RIGHT);
      menu_component_select(&s_menu, FIELD_LNA_GAIN); send(INPUT_BTN_RIGHT); check_layout();
      assert(subghz_settings_get().has_custom_radio);
      subghz_settings_t changed = subghz_settings_get();
      assert(cc1101_config_value(&changed.radio, CC1101_CONFIG_LNA_GAIN) == 1);
      assert(strstr(lv_label_get_text(s_menu.val_labels[1]), "AM650*") != NULL);
      menu_component_select(&s_menu, FIELD_BANDWIDTH); send(INPUT_BTN_RIGHT); check_layout();
      menu_component_select(&s_menu, FIELD_DATA_RATE); send(INPUT_BTN_RIGHT); check_layout();
      menu_component_select(&s_menu, FIELD_DVGA_GAIN); send(INPUT_BTN_RIGHT); check_layout();
      menu_component_select(&s_menu, FIELD_RESET); send(INPUT_BTN_OK); check_layout();
      assert(!subghz_settings_get().has_custom_radio);
      assert(strstr(lv_label_get_text(s_menu.val_labels[FIELD_BANDWIDTH]), "650.000") != NULL);
      menu_component_select(&s_menu, FIELD_PRESET); send(INPUT_BTN_RIGHT);
      assert(strstr(lv_label_get_text(s_menu.val_labels[FIELD_DATA_RATE]), "4.798") != NULL);
      menu_component_select(&s_menu, FIELD_DEVIATION); send(INPUT_BTN_RIGHT); check_layout();
      assert(subghz_settings_get().has_custom_radio);
      menu_component_select(&s_menu, FIELD_PRESET); send(INPUT_BTN_LEFT);
      assert(!subghz_settings_get().has_custom_radio);
      menu_component_select(&s_menu, FIELD_DEVIATION); send(INPUT_BTN_RIGHT);
      assert(!subghz_settings_get().has_custom_radio);
      menu_component_select(&s_menu, FIELD_LNA_GAIN); send(INPUT_BTN_RIGHT);
      menu_component_select(&s_menu, FIELD_FREQUENCY);
      send(INPUT_BTN_OK);
      assert(subghz_settings_get().freq == 433920000);
      size_t count;
      const uint32_t *frequencies = subghz_settings_get_frequencies(&count);
      assert(count == 53);
      for (size_t i = 0; i < count; ++i) {
        uint32_t hz = subghz_settings_get().freq;
        assert(hz == 0 || (hz >= 300000000 && hz <= 348000000) ||
               (hz >= 387000000 && hz <= 464000000) || (hz >= 779000000 && hz <= 928000000));
        assert(subghz_settings_get().has_custom_radio);
        check_layout();
        lv_obj_t *objects[] = {s_frequency_value, s_frequency_unit, s_frequency_ruler};
        for (size_t j = 0; j < sizeof(objects)/sizeof(objects[0]); ++j) {
          lv_area_t area; lv_obj_get_coords(objects[j], &area);
          lv_obj_get_transformed_area(objects[j], &area, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
          assert(area.x1 >= 0 && area.x2 < ui_screen_w());
          assert(area.y1 >= UI_CHROME_HEADER_H && area.y2 < ui_screen_h());
        }
        send(INPUT_BTN_RIGHT);
      }
      assert(subghz_settings_get().freq == 433920000);
      send(INPUT_BTN_LEFT); assert(subghz_settings_get().freq == 433889000);
      send(INPUT_BTN_LEFT); assert(subghz_settings_get().freq == 433657070);
      assert(strcmp(lv_label_get_text(s_frequency_value), "433.65707") == 0);
      for (size_t i = 0; i < count && subghz_settings_get().freq != 0; ++i)
        send(INPUT_BTN_LEFT);
      assert(subghz_settings_get().freq == 0);
      input_event_t release = {INPUT_ACTION_RELEASE, INPUT_BTN_RIGHT}; input(&release, NULL);
      assert(subghz_settings_get().freq == 0);
      send(INPUT_BTN_BACK); close_screen(blank);
      ui_subghz_config_open(); check_layout();
      assert(strstr(lv_label_get_text(s_menu.val_labels[1]), "AM650*") != NULL);
      assert(subghz_settings_get().has_custom_radio);
      assert(strstr(lv_label_get_text(s_menu.val_labels[0]), "Hop bands") != NULL);
      close_screen(blank);
    }
  }
  assert(backs == 4 && notices == 4);
  puts("PASS: inline frequency dial, all 53 entries, immediate tuning, wrap, overrides and layout in both orientations/themes");
}
"""
test = WORK / "test.c"
test.write_text(NOTICE + code)
command = ["cc", "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(WORK), "-I" + str(BASE)]
for folder in ("screens/subghz", "components/menu_component", "components/toggle", "components/intensity_bar"):
    command += ["-I" + str(UI / folder / "include")]
command += ["-I" + str(ROOT / "firmware_p4/components/Service/frequency_analyzer/include"),
            "-I" + str(APPS / "include"),
            "-I" + str(ROOT / "firmware_p4/components/Drivers/cc1101/include"),
            "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"), str(test),
            str(ROOT / "firmware_p4/components/Drivers/cc1101/cc1101_config.c"),
            str(APPS / "subghz_settings.c"),
            str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True, timeout=60)
subprocess.run([str(WORK / "test")], check=True, timeout=30)
