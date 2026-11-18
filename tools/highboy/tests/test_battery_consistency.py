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

"""Execute production renderers against contradictory charger/gauge inputs."""
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / 'firmware_p4/components/Applications/ui'


def function(path, signature):
    source = (UI / path).read_text()
    match = re.search(re.escape(signature).replace(r"\ ", r"\s+"), source)
    assert match is not None, signature
    start = match.start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define BATTERY_LOW_PERCENT 20
#define BATTERY_MID_PERCENT 45
#define BATTERY_FULL_PERCENT 100
#define BATTERY_LAST_PARTIAL_FRAME 2
#define BATTERY_FULL_FRAME 3
#define BATTERY_TEXT_SIZE 12
#define BATTERY_MV_PER_V 1000
#define BATTERY_MV_PER_CENTIVOLT 10
#define LV_OPA_TRANSP 0
#define LV_OPA_COVER 255
#define LV_SYMBOL_CHARGE "CHARGE"
#define LV_SYMBOL_USB "USB"
#define LV_SYMBOL_WARNING "WARN"
#define LV_SYMBOL_LIST "LIST"
#define LV_SYMBOL_SETTINGS "SETTINGS"
#define ESP_OK 0
#define CHARGE_STATUS_CHARGE_DONE 3
#define ACT_CHARGE 0
#define ACT_SCAN 1
#define ACT_REGS 2
#define ACT_OFF 3
typedef struct {int soc,vbat_mv;bool valid,present,charging,vbus_present;} battery_snapshot_t;
typedef struct {int soc,vbat_mv,vbus_mv,vbus,chg;bool power_good,charging;} bq25896_telem_t;
typedef struct {bool gauge_valid,requested;int soc;bq25896_telem_t charger;} battery_charge_snapshot_t;
typedef struct {char text[96];int frame,width,tint;} lv_obj_t;
static struct {int text_secondary;} current_theme;
static int frames[]={0,1,2,3};
static int *battery_dscs[]={frames,frames+1,frames+2,frames+3};
static lv_obj_t icon,symbol,label,pct,volt,src,state,row,fill;
static lv_obj_t *s_pct=&pct,*s_volt=&volt,*s_src=&src,*s_state=&state;
static lv_obj_t *s_row_val[]={&row},*bat_chip_val=&label,*bat_chip_fill=&fill;
static battery_snapshot_t battery;
static battery_charge_snapshot_t charge;
static bool available=true,request_failed,request_value;
static int requests,errors;
static char s_scan_msg[96];
static bool battery_service_get(battery_snapshot_t *out){*out=battery;return available;}
static bool battery_service_get_charge(battery_charge_snapshot_t *out){*out=charge;return available;}
static int battery_service_request_charge(bool enable){++requests;request_value=enable;return request_failed;}
static void lv_image_set_src(lv_obj_t *o,int *frame){o->frame=*frame;}
static const void *lv_image_get_src(lv_obj_t *o){return battery_dscs[o->frame];}
static void lv_obj_set_style_image_recolor(lv_obj_t *o,int c,int p){(void)o;(void)c;(void)p;}
static void lv_obj_set_style_image_recolor_opa(lv_obj_t *o,int c,int p){o->tint=c;(void)p;}
static void lv_label_set_text(lv_obj_t *o,const char *s){snprintf(o->text,sizeof(o->text),"%s",s);}
static const char *lv_label_get_text(lv_obj_t *o){return o->text;}
static void lv_label_set_text_fmt(lv_obj_t *o,const char *f,...){va_list a;va_start(a,f);vsnprintf(o->text,sizeof(o->text),f,a);va_end(a);}
static int lv_pct(int n){return n;}
static void lv_obj_set_width(lv_obj_t *o,int w){o->width=w;}
static void style_state_chip(bool charging){(void)charging;}
static const char *vbus_name(int vbus){(void)vbus;return "USB";}
static void i2c_scan_fill(void){}
static int bq25896_reg_raw(int r){(void)r;return 0;}
static void power_policy_show_shutdown_dialog(void){}
static void msgbox_open(const char *icon,const char *message,void *a,void *b,void *c){
 (void)icon;(void)a;(void)b;(void)c;if(!strcmp(message,"Charge request failed"))++errors;
}
'''
for path, signatures in (
    ('components/header/header_ui.c', ['static int battery_level_frame(', 'static void battery_render(']),
    ('components/dropdown/dropdown_ui.c', ['static void battery_tick(void) {']),
    ('screens/power/power_ui.c', ['static void refresh_telem(', 'static void do_action(']),
):
    for signature in signatures:
        code += function(path, signature)
code += r'''
int main(void) {
 battery=(battery_snapshot_t){.soc=-1,.vbat_mv=3844,.valid=true,.present=true,.charging=true,.vbus_present=true};
 charge=(battery_charge_snapshot_t){.soc=-1,.requested=true,.charger={.soc=64,.vbat_mv=3844,.vbus_mv=5100,.power_good=true,.charging=true}};
 battery_render(&battery,&icon,&symbol,&label);
 assert(!strcmp(label.text,"3.84V") && icon.frame==0 && icon.tint==255);
 assert(!strcmp(symbol.text,"CHARGE"));
 battery_tick();assert(!strcmp(label.text,"CHARGE 3.84V") && fill.width==1);
 refresh_telem();assert(!strcmp(pct.text,"--%") && !strcmp(volt.text,"3.84 V"));
 assert(!strcmp(state.text,"Charging"));
 do_action(ACT_CHARGE);assert(requests==1 && !request_value);
 charge.requested=false;charge.charger.charging=false;battery.charging=false;
 refresh_telem();assert(!strcmp(state.text,"Charge paused") && !strcmp(row.text,"OFF"));
 do_action(ACT_CHARGE);assert(requests==2 && request_value);
 request_failed=true;do_action(ACT_CHARGE);assert(errors==1);
 battery_render(&battery,&icon,&symbol,&label);assert(!strcmp(symbol.text,"USB"));
 battery.soc=45;charge.gauge_valid=true;charge.soc=45;battery.charging=true;
 battery_render(&battery,&icon,&symbol,&label);assert(!strcmp(label.text,"45%") && icon.frame==2);
 battery_tick();assert(fill.width==45);battery_tick();assert(fill.width==45);
 refresh_telem();assert(!strcmp(pct.text,"45%"));
 battery.soc=100;battery_render(&battery,&icon,&symbol,&label);assert(icon.frame==3);
 battery.valid=false;battery_render(&battery,&icon,&symbol,&label);
 assert(!strcmp(label.text,"--") && !strcmp(symbol.text,"?") && icon.tint==255);
 available=false;refresh_telem();assert(!strcmp(pct.text,"--%") && !strcmp(volt.text,"-- V"));
 do_action(ACT_CHARGE);assert(requests==3 && errors==2);
 puts("PASS: consistent gauge/unknown/USB/charge display, static level, cached Power telemetry and queued charge control");
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
