#!/usr/bin/env python3
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

"""Exercise target-picker screen reconstruction without invoking radio attacks."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from test_rf_ownership import function

ROOT = Path(__file__).resolve().parents[2]


def main():
    source = (ROOT / "firmware_p4/components/Applications/ui/screens/wifi/wifi_attack_ui.c").read_text()
    callback = function(source, "static void ap_pick_done_cb(void *unused)")
    notice = source.split("#include", 1)[0]
    harness = notice + r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct { bool alive; } lv_obj_t;
static lv_obj_t pool[128], *s_screen, *active;
static unsigned created, builds, loads, feedback, errors, handlers;
static bool fail_alloc;
static int current, s_ap_count;
static const char *TAG = "TEST";
static struct { int screen_base; } current_theme;
#define SCREEN_WIFI_ATTACK_MENU 1
#define SCREEN_WIFI_MENU 2
#define LV_OPA_COVER 255
#define LV_OBJ_FLAG_SCROLLABLE 1
#define UI_FB_READ 1
#define ESP_LOGE(tag, ...) ((void)(tag))
static int ui_current_screen(void) { return current; }
static lv_obj_t *lv_obj_create(void *parent) {
  assert(parent == NULL);
  if (fail_alloc) return NULL;
  assert(created < 128);
  pool[created].alive = true;
  return &pool[created++];
}
static void lv_obj_set_style_bg_color(lv_obj_t *o, int c, int s) {
  assert(o && o->alive); (void)c; (void)s;
}
static void lv_obj_set_style_bg_opa(lv_obj_t *o, int c, int s) {
  assert(o && o->alive); (void)c; (void)s;
}
static void lv_obj_remove_flag(lv_obj_t *o, int f) { assert(o && o->alive); (void)f; }
static void led_signal_error(void) { errors++; }
static void ui_switch_screen(int screen) { current = screen; }
static void build_pick_list(void) { assert(s_screen && s_screen->alive); builds++; }
static void ui_screen_load_owned(lv_obj_t **owner, lv_obj_t *screen) {
  assert(owner == &s_screen && *owner == screen && screen->alive);
  active = screen; loads++;
}
static void wifi_attack_input(void) {}
static void ui_input_set_screen_handler(void (*fn)(void), void *ctx) {
  assert(fn == wifi_attack_input && ctx == NULL); handlers++;
}
static void ui_feedback(int f) { assert(f == UI_FB_READ); feedback++; }
""" + callback + r"""
int main(void) {
  for (unsigned i = 0; i < 100; i++) {
    current = SCREEN_WIFI_ATTACK_MENU;
    s_screen = NULL; /* live scan deleted the previous owned screen */
    s_ap_count = i % 21;
    unsigned old_created = created, old_loads = loads;
    ap_pick_done_cb(NULL);
    assert(created == old_created + 1 && loads == old_loads + 1);
    assert(active == s_screen && s_screen->alive);
    for (unsigned page = 0; page < 7; page++) ap_pick_done_cb(NULL);
    assert(created == old_created + 1 && loads == old_loads + 1);
    s_screen->alive = false;
    s_screen = NULL;
  }
  assert(builds == 800 && handlers == builds && feedback > 0);
  fail_alloc = true;
  ap_pick_done_cb(NULL);
  assert(current == SCREEN_WIFI_MENU && errors == 1 && builds == 800);
  fail_alloc = false;
  current = SCREEN_WIFI_ATTACK_MENU;
  ap_pick_done_cb(NULL); /* allocation retry succeeds */
  assert(s_screen && s_screen->alive && builds == 801);
  current = SCREEN_WIFI_MENU;
  ap_pick_done_cb(NULL); /* stale completion cannot replace another screen */
  assert(builds == 801);
  return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="highboy-scan-return-") as tmp:
        path = Path(tmp)
        (path / "test.c").write_text(harness)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True, timeout=30)
    print("PASS: scan return, empty/populated results, paging, repeated entry, allocation failure/retry and stale completion")


if __name__ == "__main__":
    main()
