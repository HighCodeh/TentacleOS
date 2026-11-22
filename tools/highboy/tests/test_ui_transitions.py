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

"""Regression checks for carousel animation guards and grid navigation."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
NOTICE = (UI / "screens/menu/menu_ui.c").read_text().split("#include", 1)[0]
def function(path, name):
    source = path.read_text()
    match = re.search(r"(?:static )?void " + name + r"\([^;]*?\)\s*\{", source)
    start = match.start(); end = match.end(); depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]
def run(name, code):
    with tempfile.TemporaryDirectory(prefix=name) as tmp:
        path=Path(tmp); (path/"test.c").write_text(NOTICE + code)
        subprocess.run([os.environ.get("CC","cc"),"-std=c11","-Wall","-Wextra","-Werror",
                        "-fsanitize=undefined",str(path/"test.c"),"-o",str(path/"test")],check=True)
        subprocess.run([str(path/"test")],check=True)
common = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define UI_FB_NAV 0
#define UI_FB_SELECT 1
static __attribute__((unused)) void ui_feedback(int code) {(void)code;}
"""
menu = r"""
typedef struct {int code;uint32_t key;} lv_event_t;
typedef int lv_event_code_t;
enum {LV_EVENT_KEY,LV_EVENT_SHORT_CLICKED,LV_EVENT_LONG_PRESSED};
enum {LV_KEY_ESC,LV_KEY_LEFT,LV_KEY_RIGHT,LV_KEY_DOWN};
#define SCREEN_SETTINGS 999
#define SCREEN_HOME 1
#define MENU_ITEM_COUNT 12
static unsigned s_selected;
#define MENU_HAPTIC_MS 70
static struct {bool vibration;} g_config_system={true};
static unsigned pulses;
static int haptic_feedback(unsigned ms) {assert(ms==70);pulses++;return 0;}
static int opened=-1, route=-1, toggled=-1, updated;
static struct {int target;} s_menu_data[MENU_ITEM_COUNT];
static int lv_event_get_code(lv_event_t *e) {return e->code;}
static uint32_t lv_event_get_key(lv_event_t *e) {return e->key;}
static void ui_switch_screen(int id) {route=id;}
static void menu_open_selected(void) {opened=s_selected;}
static void favorites_toggle(int id) {toggled=id;}
static bool animating;
static void update_view(bool anim) {animating=anim;updated++;}
static bool is_animating(void) {return animating;}
"""
checks = r"""
int main(void) {
 for(int i=0;i<MENU_ITEM_COUNT;i++) s_menu_data[i].target=100+i;
 lv_event_t e={LV_EVENT_KEY,LV_KEY_RIGHT};
 menu_event_cb(&e);assert(s_selected==1 && updated==1 && animating && pulses==1);
 for(int i=0;i<100;i++)menu_event_cb(&e);
 e.key=LV_KEY_LEFT;menu_event_cb(&e);
 e.key=LV_KEY_DOWN;menu_event_cb(&e);
 e.code=LV_EVENT_SHORT_CLICKED;menu_event_cb(&e);
 e.code=LV_EVENT_LONG_PRESSED;menu_event_cb(&e);
 assert(s_selected==1 && updated==1 && opened==-1 && toggled==-1 && animating && pulses==1);
 e.code=LV_EVENT_KEY;e.key=LV_KEY_ESC;menu_event_cb(&e);assert(route==SCREEN_HOME);
 animating=false;e.key=LV_KEY_LEFT;menu_event_cb(&e);
 assert(s_selected==0 && updated==2 && animating);
 animating=false;e.key=LV_KEY_DOWN;menu_event_cb(&e);assert(opened==0);
 e.code=LV_EVENT_SHORT_CLICKED;menu_event_cb(&e);assert(opened==0);
 e.code=LV_EVENT_LONG_PRESSED;menu_event_cb(&e);assert(toggled==100 && updated==3 && pulses==2);
 e.code=LV_EVENT_KEY;e.key=LV_KEY_LEFT;menu_event_cb(&e);
 assert(s_selected==MENU_ITEM_COUNT-1 && animating);
 for(int i=0;i<100;i++){
  unsigned previous=s_selected;
  menu_event_cb(&e);assert(s_selected==previous);
  animating=false;menu_event_cb(&e);
  assert(s_selected==(previous==0 ? MENU_ITEM_COUNT-1 : previous-1));
 }
 unsigned before=pulses;g_config_system.vibration=false;animating=false;
 menu_event_cb(&e);assert(pulses==before && animating);
 return 0;
}
"""
run("carousel-motion-",common+menu+function(UI/"screens/menu/menu_ui.c","menu_event_cb")+checks)
header=(ROOT/"firmware_p4/components/Drivers/input_manager/include/input_manager.h").read_text()
types=header[header.index("typedef enum"):header.index("/**",header.index("} input_event_t;"))]
grid=r"""
static int s_sel_idx, s_grid_cols, s_card_count, s_custom_idx, s_attack_count, s_spam_mode;
static int route;
enum {SCREEN_BLE_MENU,SCREEN_BLE_SPAM_NAMES,SCREEN_BLE_SPAM};
static void select_card(int i) {assert(i>=0 && i<s_card_count);s_sel_idx=i;}
static void ui_switch_screen(int id) {route=id;}
"""
checks=r"""
int main(void) {
 for(s_card_count=1;s_card_count<=13;s_card_count++) {
  s_attack_count=s_custom_idx=s_card_count-1;
  for(s_grid_cols=2;s_grid_cols<=3;s_grid_cols++) {
   for(int start=0;start<s_card_count;start++) {
    for(int key=INPUT_BTN_UP;key<=INPUT_BTN_RIGHT;key++) {
     s_sel_idx=start;input_event_t e={key,INPUT_ACTION_PRESS,0};
     for(int i=0;i<100;i++)ble_spam_select_input(&e,NULL);
    }
    s_sel_idx=start;input_event_t e={INPUT_BTN_RIGHT,INPUT_ACTION_PRESS,0};
    for(int i=0;i<s_card_count;i++)ble_spam_select_input(&e,NULL);
    assert(s_sel_idx==start);
   }
  }
 }
 input_event_t e={INPUT_BTN_BACK,INPUT_ACTION_PRESS,0};
 ble_spam_select_input(&e,NULL);assert(route==SCREEN_BLE_MENU);
 return 0;
}
"""
run("grid-motion-",common+types+grid+function(UI/"screens/bluetooth/ble_spam_ui.c","ble_spam_select_input")+checks)
print("PASS: carousel waits for animation completion, discards repeated input, keeps Back available; partial grid rows and wrap")

media=r"""
typedef void lv_obj_t;
typedef struct {void *target;} lv_event_t;
static void *s_screen,*s_cover_image,*s_png_blob;
static bool s_thumb_valid,s_png_valid;
static struct {int dsc;} s_thumb;
static int s_png_dsc,deleted,dropped,freed;
static void *lv_event_get_target(lv_event_t *e){return e->target;}
static void lv_obj_delete(void *p){assert(p==s_cover_image);deleted++;}
static void lv_image_cache_drop(void *p){assert(deleted==1);assert(p==&s_thumb.dsc||p==&s_png_dsc);dropped++;}
static void media_thumb_free(void *p){assert(p==&s_thumb&&dropped==1);freed++;}
static void fake_free(void *p){assert(p==s_png_blob&&dropped==2);freed++;}
#define free fake_free
"""
checks=r"""
int main(void){
 s_screen=(void*)3;s_cover_image=(void*)1;s_png_blob=(void*)2;s_thumb_valid=s_png_valid=true;
 lv_event_t old={(void*)4};screen_delete_cb(&old);assert(deleted==0&&freed==0);
 ui_mp4_player_stop();assert(deleted==1&&dropped==2&&freed==2);
 assert(!s_thumb_valid&&!s_png_valid&&s_png_blob==NULL&&s_cover_image==NULL);
 ui_mp4_player_stop();assert(freed==2&&deleted==1);
 lv_event_t current={s_screen};screen_delete_cb(&current);assert(s_screen==NULL&&freed==2);
 return 0;
}
"""
p=UI/'screens/video/mp4_player_ui.c'
run('mp4-cover-lifetime-',common+media+function(p,'free_thumb')+function(p,'ui_mp4_player_stop')+function(p,'screen_delete_cb')+checks)
print('PASS: MP4 cover detach, cache drop, idempotent close and delayed old-root deletion')
