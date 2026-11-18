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

"""Exercise the actual battery renderer with qualified and unqualified telemetry."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/'firmware_p4/components/Applications/ui/screens/settings/battery_settings_ui.c').read_text()
def function(signature):
 start=source.index(signature);pos=source.index('{',start)+1;depth=1
 while depth:
  depth+=(source[pos]=='{')-(source[pos]=='}');pos+=1
 return source[start:pos]
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define POWER_PROFILE_CAPACITY_MAH 2475U
#define BATTERY_MV_PER_V 1000
#define BATTERY_MV_PER_CENTIVOLT 10
#define UI_COL_SUCCESS 1
#define UI_COL_WARNING 2
#define LV_PART_INDICATOR 1
#define LV_SYMBOL_USB "USB"
#define CHARGE_STATUS_CHARGE_DONE 3
typedef int lv_color_t;
typedef int lv_timer_t;
typedef struct {void *target;} lv_event_t;
static void *lv_event_get_target(lv_event_t *e){return e->target;}
typedef struct {char text[100];int value;} lv_obj_t;
typedef struct {int soc,vbat_mv,chg;bool present,charging,vbus_present,low;} battery_snapshot_t;
typedef struct {int vbat_mv;bool power_good;int vbus_mv;} charger_t;
typedef struct {bool requested,current_valid;int battery_current_ma;const char *status;charger_t charger;} battery_charge_snapshot_t;
static struct {int text_main,text_secondary;} current_theme;
static battery_snapshot_t snap;
static battery_charge_snapshot_t ch;
static bool valid=true,charge_valid=true;
static lv_obj_t nodes[10];
static lv_obj_t *s_screen=nodes,*s_pct=nodes+1,*s_source=nodes+2,*s_state=nodes+3,
 *s_current=nodes+4,*s_reason=nodes+5,*s_note=nodes+6,*s_caption=nodes+7,*s_footer=nodes+8;
static lv_timer_t timer,*s_refresh_timer=&timer;
static int deletions;
static bool battery_service_get(battery_snapshot_t *out){*out=snap;return valid;}
static bool battery_service_get_charge(battery_charge_snapshot_t *out){*out=ch;return charge_valid;}
static int lv_color_hex(int c){return c;}
#define lv_obj_set_style_arc_color(o,c,p) ((void)(c))
#define lv_obj_set_style_text_color(o,c,p) ((void)(c))
static void lv_label_set_text(lv_obj_t *o,const char *s){snprintf(o->text,sizeof(o->text),"%s",s);}
static void lv_label_set_text_fmt(lv_obj_t *o,const char *f,...){va_list a;va_start(a,f);vsnprintf(o->text,sizeof(o->text),f,a);va_end(a);}
static void lv_timer_delete(lv_timer_t *t){assert(t);++deletions;}
static void ui_chrome_footer_set_text(lv_obj_t *o,const char *s){lv_label_set_text(o,s);}
"""
code+=function('static void refresh_cb(')+function('void ui_battery_settings_close(')+function('static void screen_delete_cb(')
code+=r"""
int main(void){
 snap=(battery_snapshot_t){.soc=-1,.vbat_mv=3004,.present=true,.vbus_present=true};
 ch=(battery_charge_snapshot_t){.requested=true,.current_valid=true,.status="Battery voltage too low",.charger={3004,true,5100}};
 refresh_cb(NULL);assert(!strcmp(s_pct->text,"3.00 V"));
 assert(strstr(s_reason->text,"safe recovery") && !strcmp(s_state->text,"Not charging"));
 assert(!strcmp(s_current->text,"+0 mA") && !strcmp(s_caption->text,"Percentage unavailable"));
 snap.charging=true;snap.vbat_mv=3600;ch.charger.vbat_mv=3600;ch.status="Charging";ch.battery_current_ma=350;
 refresh_cb(NULL);assert(!strcmp(s_state->text,"Charging") && !strcmp(s_pct->text,"3.60 V"));
 snap.soc=45;refresh_cb(NULL);assert(!strcmp(s_pct->text,"45%") && !strcmp(s_caption->text,"State of charge"));
 snap.charging=false;snap.chg=3;refresh_cb(NULL);assert(!strcmp(s_state->text,"Fully charged"));
 snap.chg=0;ch.requested=false;refresh_cb(NULL);assert(!strcmp(s_state->text,"Charge paused") && strstr(s_footer->text,"Resume"));
 valid=false;charge_valid=false;refresh_cb(NULL);assert(!strcmp(s_pct->text,"--"));
 lv_event_t old={nodes+9},active={s_screen};
 screen_delete_cb(&old);assert(deletions==0 && s_refresh_timer);
 ui_battery_settings_close();screen_delete_cb(&active);assert(deletions==1 && !s_refresh_timer);
 puts("PASS: battery card voltage/percent/source/current, charging/full/paused/fault, correct action and stale-root/central timer cleanup");
}
"""
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
