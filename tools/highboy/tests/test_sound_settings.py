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

"""Compile the actual sound input handler against UI/audio fakes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "firmware_p4/components/Applications/ui/screens/settings/sound_settings_ui.c"
s = SOURCE.read_text()
start = s.index("static int vol_level_from_pct(")
end = s.index("void ui_sound_settings_open(")
code = s[start:end]
fakes = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define VOL_STEPS 10
#define ROW_VOLUME 0
#define ROW_UI_SOUNDS 1
#define ESP_OK 0
#define NOTIFY_SAVED 1
#define NOTIFY_WARNING 2
#define SCREEN_SETTINGS 1
#define TOS_PATH_CONFIG_SYSTEM "system"
#define UI_FB_NAV 0
#define INPUT_ACTION_PRESS 0
#define INPUT_ACTION_REPEAT 1
#define INPUT_BTN_DOWN 0
#define INPUT_BTN_UP 1
#define INPUT_BTN_OK 2
#define INPUT_BTN_LEFT 3
#define INPUT_BTN_RIGHT 4
#define INPUT_BTN_BACK 5
typedef void lv_obj_t;
typedef struct {int action, button;} input_event_t;
typedef struct {bool has_toggle[2],has_intensity[2]; int level;} menu_component_t;
static struct {int volume; bool ui_sound_fx;} g_config_system;
static int gain, previews, saved, target;
static void menu_component_next(menu_component_t *m) {(void)m;}
static void menu_component_prev(menu_component_t *m) {(void)m;}
static int menu_component_get_selected(menu_component_t *m) {(void)m;return 0;}
static void menu_component_toggle_item(menu_component_t *m,int i) {(void)m;(void)i;}
static bool menu_component_get_toggle(menu_component_t *m,int i) {(void)m;(void)i;return true;}
static void menu_component_intensity_dec(menu_component_t *m,int i) {(void)i;if(m->level>0)m->level--;}
static void menu_component_intensity_inc(menu_component_t *m,int i) {(void)i;if(m->level<10)m->level++;}
static int menu_component_get_intensity(menu_component_t *m,int i) {(void)i;return m->level;}
static void audio_i2s_set_volume(uint8_t v) {gain=v;assert(g_config_system.volume==v);}
static void ui_feedback(int k) {(void)k;previews++;}
static int tos_config_save(const char *p,const char *m) {(void)p;(void)m;saved=g_config_system.volume;return 0;}
static void notify(int n,const char *s) {(void)n;(void)s;}
static void ui_switch_screen(int t) {target=t;}
"""
tests = r"""
int main(void) {
 s_screen=NULL;
 s_menu.has_intensity[0]=true;
 g_config_system.volume=50;s_menu.level=vol_level_from_pct(50);
 input_event_t e={INPUT_ACTION_PRESS,INPUT_BTN_LEFT};
 sound_settings_input(&e,NULL);
 assert(gain==40 && g_config_system.volume==40 && previews==1);
 e.button=INPUT_BTN_RIGHT;sound_settings_input(&e,NULL);
 assert(gain==50 && g_config_system.volume==50 && previews==2);
 // A quick-panel change while Sound is open must survive Back/save.
 g_config_system.volume=70;audio_i2s_set_volume(70);
 e.button=INPUT_BTN_BACK;sound_settings_input(&e,NULL);
 assert(saved==70 && gain==70 && target==SCREEN_SETTINGS);
 // Muting and raising the settings control again restores the live gain.
 s_menu.level=0;g_config_system.volume=0;audio_i2s_set_volume(0);
 e.button=INPUT_BTN_RIGHT;sound_settings_input(&e,NULL);
 assert(gain==10 && g_config_system.volume==10);
 puts("PASS: live volume, preview, unmute and shared quick-panel value on Back");
}
"""
with tempfile.TemporaryDirectory(prefix="sound-settings-") as temporary:
    work = Path(temporary)
    c = work / "test.c"
    c.write_text(fakes + code + tests)
    binary = work / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
