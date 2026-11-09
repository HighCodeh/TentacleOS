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

"""Test charge frame limits and live battery refresh with host UI fakes."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
header = (ROOT / "firmware_p4/components/Applications/ui/components/header/header_ui.c").read_text()
screen = (ROOT / "firmware_p4/components/Applications/ui/screens/settings/battery_settings_ui.c").read_text()
def function(source, signature):
    start=source.index(signature); opening=source.index("{",start); end=opening+1; depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}"); end+=1
    return source[start:end]
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define BATTERY_LOW_PERCENT 20
#define BATTERY_MID_PERCENT 45
#define BATTERY_FULL_PERCENT 100
#define BATTERY_FULL_FRAME 3
#define BATTERY_LAST_PARTIAL_FRAME 2
#define VOLTAGE_MV_PER_V 1000
#define VOLTAGE_CENTIVOLT_MV 10
#define LV_OPA_COVER 255
#define LV_OPA_50 128
#define LV_PART_INDICATOR 1
#define UI_COL_SUCCESS 1
#define CHARGE_STATUS_PRECHARGE 1
#define LV_SYMBOL_CHARGE "+"
#define LV_SYMBOL_BATTERY_FULL "B"
typedef struct {char text[80];int value,opa;const void *src;} lv_obj_t;
typedef int lv_timer_t;
typedef int lv_event_t;
typedef int lv_color_t;
typedef struct {int soc,vbat_mv,chg;bool present,charging,vbus_present;} battery_snapshot_t;
static battery_snapshot_t snapshot;
static bool valid=true;
static lv_obj_t nodes[9];
static lv_obj_t *battery_img=&nodes[0], *power_img=&nodes[1];
static lv_obj_t *s_arc=&nodes[2], *s_pct=&nodes[3], *s_voltage=&nodes[4],
 *s_source=&nodes[5], *s_status=&nodes[6], *s_eta=&nodes[7], *s_chg=&nodes[8];
static int frames[4], battery_frame, s_batt_soc_idx_last;
static int *battery_dscs[]={frames,frames+1,frames+2,frames+3};
static bool s_batt_present_last=true,s_batt_charging_last=true;
static lv_timer_t timer,*battery_charge_timer=&timer,*s_refresh_timer=&timer;
static int deletions;
static struct {int text_main;} current_theme;
static bool lv_obj_is_valid(lv_obj_t *o) {return o!=NULL;}
static void lv_timer_delete(lv_timer_t *t) {assert(t);++deletions;}
static void lv_image_set_src(lv_obj_t *o,const void *src) {o->src=src;}
static int lv_obj_get_style_opa(lv_obj_t *o,int sel) {(void)sel;return o->opa;}
static void lv_obj_set_style_opa(lv_obj_t *o,int v,int sel) {(void)sel;o->opa=v;}
static bool battery_service_get(battery_snapshot_t *bs) {*bs=snapshot;return valid;}
static void arc_anim_exec_cb(void *o,int32_t v) {(void)o;(void)v;}
static void lv_anim_delete(void *o,void (*cb)(void*,int32_t)) {(void)o;(void)cb;}
static void lv_arc_set_value(lv_obj_t *o,int v) {o->value=v;}
static int level_color(int soc) {return soc;}
static int lv_color_hex(int v) {return v;}
#define lv_obj_set_style_arc_color(obj,color,sel) ((void)(color))
#define lv_obj_set_style_text_color(obj,color,sel) ((void)(color))
#define lv_obj_set_style_bg_color(obj,color,sel) ((void)(color))
static void lv_label_set_text(lv_obj_t *o,const char *s) {snprintf(o->text,sizeof(o->text),"%s",s);}
static void lv_label_set_text_fmt(lv_obj_t *o,const char *fmt,...) {
 va_list args;va_start(args,fmt);vsnprintf(o->text,sizeof(o->text),fmt,args);va_end(args);
}
"""
for src, sig in ((header,"static int battery_level_frame("),
                 (header,"static void battery_charge_anim_cb(lv_timer_t *timer) {"),
                 (screen,"static void refresh_cb("), (screen,"static void screen_delete_cb(")):
    code+=function(src,sig)+"\n"
code+=r"""
int main(void) {
 for(int soc=0;soc<=100;++soc) {
  s_batt_soc_idx_last=battery_level_frame(soc);battery_frame=s_batt_soc_idx_last;
  for(int tick=0;tick<12;++tick) {
   battery_charge_anim_cb(&timer);
   assert(battery_frame>=s_batt_soc_idx_last);
   assert(soc==100 ? battery_frame==3 : battery_frame<=3);
   if(soc>=45 && soc<100) assert(battery_frame==(tick%2==0 ? 3 : 2));
  }
 }
 assert(battery_level_frame(19)==0 && battery_level_frame(20)==1);
 assert(battery_level_frame(44)==1 && battery_level_frame(45)==2);
 snapshot=(battery_snapshot_t){.soc=40,.vbat_mv=3780,.present=true};
 refresh_cb(&timer);assert(!strcmp(s_pct->text,"40%") && s_arc->value==40);
 assert(!strcmp(s_source->text,"BATT") && !strcmp(s_voltage->text,"3.78"));
 snapshot.soc=60;snapshot.charging=true;snapshot.vbus_present=true;
 refresh_cb(&timer);assert(!strcmp(s_pct->text,"60%") && !strcmp(s_status->text,"FAST"));
 snapshot.charging=false;refresh_cb(&timer);
 assert(!strcmp(s_eta->text,"On USB; not charging"));
 snapshot.soc=100;refresh_cb(&timer);assert(!strcmp(s_status->text,"FULL"));
 valid=false;refresh_cb(&timer);assert(!strcmp(s_pct->text,"--") && !strcmp(s_status->text,"--"));
 screen_delete_cb(NULL);screen_delete_cb(NULL);assert(deletions==1 && !s_refresh_timer);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    src=Path(directory)/"test.c"; exe=Path(directory)/"test"
    src.write_text(code)
    subprocess.run(["cc","-std=c11","-Wall","-Wextra",str(src),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print("PASS: charge frames 0–100%, live readings, USB state, missing data and timer cleanup")
