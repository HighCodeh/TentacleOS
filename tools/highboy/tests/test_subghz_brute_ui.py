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

"""Exercise sweep UI progress, errors, lifecycle and both display orientations."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[3]
BASE = Path(sys.argv[1]).resolve()
UI = ROOT / "firmware_p4/components/Applications/ui"
WORK = BASE / "subghz-brute"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "screens/subghz/subghz_brute_ui.c").read_text().split("#include", 1)[0]
for name in ("notify_ui.h", "ui_chrome.h", "ui_feedback.h", "ui_manager.h", "ui_metrics.h", "ui_semantic.h", "ui_theme.h"):
    (WORK / name).write_text(NOTICE + "#pragma once\n")
(WORK / "esp_log.h").write_text(NOTICE + "#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n")
(WORK / "esp_err.h").write_text(NOTICE + "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NO_MEM 0x101\nstatic const char *esp_err_to_name(int e) { return e ? \"ESP_FAIL\" : \"ESP_OK\"; }\n")
code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "subghz_brute.h"
#define UI_CHROME_FOOTER_H 22
#define UI_FB_WRITE 1
#define NOTIFY_WARNING 1
#define SCREEN_SUBGHZ_MENU 1
#define INPUT_ACTION_PRESS 1
#define INPUT_BTN_BACK 1
typedef struct { int action, button; } input_event_t;
static struct { lv_color_t bg_secondary, border_accent, border_inactive, text_main, text_secondary; } current_theme;
static unsigned stops, starts;
static subghz_brute_status_t result;
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
static lv_obj_t *ui_chrome_screen_create(void) {
  lv_obj_t *root = lv_obj_create(NULL); lv_obj_set_style_pad_all(root, 0, 0); return root;
}
static void ui_chrome_header(lv_obj_t *root, const char *title, const char *icon) {}
static lv_obj_t *ui_chrome_footer(lv_obj_t *root, const char *text) {
  lv_obj_t *label = lv_label_create(root); lv_label_set_text(label, text);
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, 0); return label;
}
static void ui_chrome_footer_set_text(lv_obj_t *label, const char *text) { lv_label_set_text(label, text); }
static void ui_feedback(int event) {}
static void notify(int level, const char *text) {}
static void ui_input_set_screen_handler(void (*cb)(const input_event_t *, void *), void *ctx) {}
static void ui_screen_load_owned(lv_obj_t **slot, lv_obj_t *root) { *slot = root; lv_screen_load(root); }
static void ui_switch_screen(int screen) {}
esp_err_t subghz_brute_start(const char *protocol, uint8_t bits, uint32_t freq) {
  ++starts; return ESP_OK;
}
void subghz_brute_stop(void) { ++stops; }
bool subghz_brute_get_status(subghz_brute_status_t *out) { *out = result; return true; }
#include "subghz_brute_ui.c"
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *buffer) { lv_display_flush_ready(display); }
int main(void) {
  lv_init(); lv_display_t *display = lv_display_create(240, 320);
  static uint8_t buffer[320 * 20 * 2];
  lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  for (unsigned wide = 0; wide < 2; ++wide) {
    lv_display_set_resolution(display, wide ? 320 : 240, wide ? 240 : 320);
    for (unsigned theme = 0; theme < 2; ++theme) {
      current_theme.bg_secondary = lv_color_hex(theme ? 0xffffff : 0x101010);
      current_theme.border_accent = current_theme.text_main = lv_color_hex(theme ? 0x101010 : 0xffffff);
      ui_subghz_brute_open(); lv_obj_update_layout(s_screen);
      lv_area_t scope, card, codes, frequency;
      lv_obj_get_coords(s_scope, &scope); lv_obj_get_coords(s_card, &card);
      lv_obj_get_coords(s_codes, &codes); lv_obj_get_coords(s_freq, &frequency);
      assert(scope.y1 > frequency.y2 && card.y1 > scope.y2);
      assert(codes.y1 >= card.y1 && codes.y2 <= card.y2);
      assert(card.y2 < ui_screen_h() - UI_CHROME_FOOTER_H);
      assert(lv_color_eq(lv_obj_get_style_bg_color(s_scope, 0), current_theme.bg_secondary));
      result = (subghz_brute_status_t){.sent = 7, .total = 4096, .done = true, .error = ESP_FAIL};
      brute_tick_cb(s_tick_timer);
      assert(lv_bar_get_value(s_bar) == 7);
      assert(strstr(lv_label_get_text(s_codes), "7 / 4096"));
      assert(!strcmp(lv_label_get_text(s_status), "ESP_FAIL"));
      unsigned before = stops; ui_subghz_brute_stop();
      assert(stops == before + 1 && s_scope_timer == NULL && s_tick_timer == NULL);
    }
  }
  assert(starts == 4);
  puts("PASS: sweep UI partial/error counts, close hooks, themes and non-overlapping portrait/landscape");
}
'''
(WORK / "test.c").write_text(NOTICE + code)
command = ["cc", "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all", "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE"]
for folder in (WORK, BASE, UI / "screens/subghz", UI / "screens/subghz/include", ROOT / "firmware_p4/components/Applications/SubGhz/include", ROOT / "firmware_p4/managed_components/lvgl__lvgl"):
    command += ["-I" + str(folder)]
command += [str(WORK / "test.c"), str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True)
subprocess.run([str(WORK / "test")], check=True, timeout=30)
