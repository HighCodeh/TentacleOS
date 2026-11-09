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

"""Exercise menu animation gating and decoder lifetime with host fakes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "firmware_p4/components/Applications/ui/screens/menu/menu_ui.c").read_text()

def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#define MENU_ITEM_COUNT 2
#define MENU_ITEM_FRAME_COUNT 3
#define PRELOAD_FRAME_COUNT 6
#define LV_RESULT_OK 0
#define SCREEN_HOME 0
#define LV_EVENT_KEY 1
#define LV_EVENT_SHORT_CLICKED 2
#define LV_EVENT_LONG_PRESSED 3
#define LV_KEY_ESC 4
#define LV_KEY_RIGHT 5
#define LV_KEY_LEFT 6
#define LV_KEY_DOWN 7
#define UI_FB_SELECT 0
#define UI_FB_NAV 1
#define ESP_LOGW(...) ((void)0)
typedef int lv_obj_t;
typedef int lv_timer_t;
typedef int lv_image_dsc_t;
typedef int lv_event_code_t;
typedef struct {int code, key;} lv_event_t;
typedef struct {const void *src; void *decoded;} lv_image_decoder_dsc_t;
static lv_obj_t objects[4], *s_base_imgs[]={objects,objects+1};
static lv_obj_t *s_icon_imgs[]={objects+2,objects+3};
static lv_obj_t *active_animation;
static lv_timer_t timer, *s_preload_timer;
static lv_image_decoder_dsc_t *s_decoders;
static size_t s_decoder_count, s_preload_frame;
static unsigned s_selected;
static int opened, closed, deleted, updated, selected, home, toggled, fail_open;
static lv_image_dsc_t images[7];
static struct {const lv_image_dsc_t *base_dscs[3], *icon_dscs[3]; int target;} s_menu_data[2];
static void *lv_anim_get(void *obj, void *cb) {(void)cb; return obj==active_animation ? obj : NULL;}
static void lv_timer_delete(lv_timer_t *t) {assert(t); ++deleted;}
static void heap_caps_free(void *p) {free(p);}
static int lv_image_decoder_open(lv_image_decoder_dsc_t *d,const void *s,void *a) {
 (void)a; if(fail_open) return 1; d->src=s;d->decoded=d;++opened;return 0;
}
static void lv_image_decoder_close(lv_image_decoder_dsc_t *d) {assert(d->decoded);++closed;}
static void update_view(bool anim) {(void)anim;++updated;}
static void load_item_frame(size_t i,int f) {(void)i;(void)f;}
#define lv_label_set_text_fmt(...) ((void)s_label)
static int lv_event_get_code(lv_event_t *e) {return e->code;}
static int lv_event_get_key(lv_event_t *e) {return e->key;}
static void ui_switch_screen(int s) {assert(s==SCREEN_HOME);++home;}
static void menu_open_selected(void) {++selected;}
static void favorites_toggle(int s) {(void)s;++toggled;}
static void ui_feedback(int f) {(void)f;}
"""
for signature in ("static bool is_animating(void) {", "static void release_preload(void) {",
                  "static bool pin_image(", "static void preload_cb(",
                  "static void menu_event_cb(lv_event_t *e) {"):
    code += function(signature) + "\n"
code += r"""
int main(void) {
 lv_event_t right={LV_EVENT_KEY,LV_KEY_RIGHT}, click={LV_EVENT_SHORT_CLICKED,0};
 lv_event_t back={LV_EVENT_KEY,LV_KEY_ESC};
 active_animation=s_base_imgs[0];
 for(int i=0;i<100;++i) menu_event_cb(&right);
 assert(s_selected==0 && updated==0);
 active_animation=s_icon_imgs[1];menu_event_cb(&right);menu_event_cb(&click);
 assert(s_selected==0 && selected==0);
 menu_event_cb(&back);assert(home==1);
 active_animation=NULL;menu_event_cb(&right);assert(s_selected==1 && updated==1);
 s_preload_timer=&timer;menu_event_cb(&right);assert(s_selected==0);
 menu_event_cb(&back);assert(home==2);
 s_decoders=calloc(12,sizeof(*s_decoders));
 for(int i=0;i<2;++i) for(int f=0;f<3;++f) {
  s_menu_data[i].base_dscs[f]=&images[0];
  s_menu_data[i].icon_dscs[f]=&images[1+i*3+f];
 }
 int view_updates=updated;
 active_animation=s_icon_imgs[0];preload_cb(&timer);assert(s_preload_frame==0);
 active_animation=NULL;
 for(int i=0;i<6;++i) preload_cb(&timer);
 assert(!s_preload_timer && s_decoder_count==7 && opened==7 && closed==0);
 assert(updated==view_updates && s_selected==0);
 release_preload();assert(closed==7 && !s_decoders && !s_decoder_count);
 release_preload();assert(closed==7);
 s_decoders=calloc(12,sizeof(*s_decoders));s_preload_timer=&timer;s_preload_frame=0;
 preload_cb(&timer);assert(s_decoder_count==2);
 fail_open=1;preload_cb(&timer);
 assert(!s_preload_timer && !s_decoders && closed==opened);
 fail_open=0;s_decoders=calloc(12,sizeof(*s_decoders));s_preload_timer=&timer;s_preload_frame=0;
 preload_cb(&timer);release_preload();assert(closed==opened);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    src = Path(directory) / "test.c"
    exe = Path(directory) / "test"
    src.write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Menu animation gating, preload deduplication and cleanup passed")
