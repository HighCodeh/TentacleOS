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
typedef struct {int soc,vbat_mv;bool valid,present,charging,vbus_present,low,is_soc_estimated;} battery_snapshot_t;
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
static bool s_low_last,s_crit_last;
static unsigned s_shutdown_count,shutdowns;
#define NOTIFY_WARNING 1
static void power_manager_set_external_power(bool present){(void)present;}
static void notify(int kind,const char *message){(void)kind;(void)message;}
static void led_signal_warning(void) {}
static void begin_shutdown(const char *reason){(void)reason;shutdowns++;}
static int requests,errors;
static int animation_deletes, animation_starts;
static bool animation_fail;
static const char *TAG="test";
#define ESP_LOGE(tag,...) ((void)(tag))
#define LV_ANIM_REPEAT_INFINITE 0xffff
static void led_signal_error(void) {}
typedef struct {void *var;void (*exec)(void*,int32_t);int from,to,duration,repeat;} lv_anim_t;
static lv_anim_t active_animation;
static lv_obj_t *battery_cont=&icon,*battery_img=&icon,*power_img=&symbol,*s_battery_text=&label;
static bool s_battery_animation_failed;
static bool lv_obj_is_valid(void *o){return o!=NULL;}
static void lv_anim_init(lv_anim_t *a){memset(a,0,sizeof(*a));}
static void lv_anim_set_var(lv_anim_t *a,void *var){a->var=var;}
static void lv_anim_set_exec_cb(lv_anim_t *a,void (*cb)(void*,int32_t)){a->exec=cb;}
static void lv_anim_set_values(lv_anim_t *a,int from,int to){a->from=from;a->to=to;}
static void lv_anim_set_duration(lv_anim_t *a,int duration){a->duration=duration;}
static void lv_anim_set_repeat_count(lv_anim_t *a,int repeat){a->repeat=repeat;}
static lv_anim_t *lv_anim_start(lv_anim_t *a){animation_starts++;if(animation_fail)return NULL;active_animation=*a;a->exec(a->var,a->from);return &active_animation;}
static lv_anim_t *lv_anim_get(void *var,void (*cb)(void*,int32_t)){return active_animation.var==var && active_animation.exec==cb?&active_animation:NULL;}
static void lv_anim_delete(void *var,void (*cb)(void*,int32_t)){if(lv_anim_get(var,cb)){memset(&active_animation,0,sizeof(active_animation));animation_deletes++;}}

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
header=(UI/'components/header/header_ui.c').read_text()
code='\n'.join(line for line in header.splitlines() if line.startswith('#define BATTERY_'))+'\n'+code
policy=(UI/'components/power_policy/power_policy.c').read_text()
code+='\n'.join(line for line in policy.splitlines() if line.startswith('#define BATT_'))+'\n#define CONFIG_HIGHBOY_BOARD_V203 1\n'
for path, signatures in (
    ('components/power_policy/power_policy.c', ['static void check_battery(void) {']),
    ('components/header/header_ui.c', ['static int battery_level_frame(', 'static void battery_charge_anim_cb(', 'static void battery_render(', 'static void battery_apply(void) {']),
    ('components/dropdown/dropdown_ui.c', ['static void battery_tick(void) {']),
    ('screens/power/power_ui.c', ['static void refresh_telem(', 'static void do_action(']),
):
    for signature in signatures:
        code += function(path, signature)
code += r'''
int main(void) {
 const int levels[]={0,24,25,49,50,74,75,99,100};
 const int expected[]={0,0,1,1,2,2,3,3,3};
 for(unsigned i=0;i<sizeof(levels)/sizeof(levels[0]);i++)assert(battery_level_frame(levels[i])==expected[i]);
 battery=(battery_snapshot_t){.soc=-1,.vbat_mv=3844,.valid=true,.present=true,.charging=true,.vbus_present=true};
 charge=(battery_charge_snapshot_t){.soc=-1,.requested=true,.charger={.soc=64,.vbat_mv=3844,.vbus_mv=5100,.power_good=true,.charging=true}};
 battery_render(&battery,&icon,&symbol,&label);
 assert(!strcmp(label.text,"--") && icon.frame==0 && icon.tint==255);
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
 battery_render(&battery,&icon,&symbol,&label);assert(!strcmp(label.text,"45%") && icon.frame==1);
 battery_tick();assert(fill.width==45);battery_tick();assert(fill.width==45);
 refresh_telem();assert(!strcmp(pct.text,"45%"));
 battery.is_soc_estimated=true;charge.gauge_valid=false;
 battery_render(&battery,&icon,&symbol,&label);assert(!strcmp(label.text,"~45%") && icon.frame==1);
 battery_tick();assert(!strcmp(label.text,"CHARGE ~45%") && fill.width==45);
 refresh_telem();assert(!strcmp(pct.text,"~45%"));
 battery.soc=100;battery_render(&battery,&icon,&symbol,&label);assert(icon.frame==3);
 battery.charging=true;battery_apply();assert(animation_starts==1);
 assert(active_animation.duration==1400 && active_animation.to==1399);
 for(int frame=0;frame<4;frame++){
  active_animation.exec(active_animation.var,frame*350);assert(icon.frame==frame);
  battery_apply();assert(icon.frame==frame && animation_starts==1);
 }
 battery_charge_anim_cb(&icon,-1);battery_charge_anim_cb(&icon,1400);assert(icon.frame==3);
 lv_obj_t overlay_icon={0},overlay_symbol={0},overlay_label={0};
 battery_render(&battery,&overlay_icon,&overlay_symbol,&overlay_label);
 assert(overlay_icon.frame==3 && animation_starts==1);
 battery.charging=false;battery_apply();assert(animation_deletes==1 && icon.frame==3);
 assert(!strcmp(symbol.text,"USB"));
 battery.charging=true;battery_apply();battery.valid=false;battery_apply();
 assert(animation_deletes==2 && !active_animation.var && !strcmp(symbol.text,"?"));
 battery.valid=true;battery.charging=true;animation_fail=true;
 battery_apply();int failed_starts=animation_starts;
 battery_apply();assert(animation_starts==failed_starts && !active_animation.var);
 battery.charging=false;battery_apply();animation_fail=false;battery.charging=true;
 battery_apply();assert(active_animation.var==&icon);
 battery.present=false;battery_apply();assert(!active_animation.var);
 battery.present=true;
 battery.valid=false;battery_render(&battery,&icon,&symbol,&label);
 assert(!strcmp(label.text,"--") && !strcmp(symbol.text,"?") && icon.tint==255);
 available=false;refresh_telem();assert(!strcmp(pct.text,"--%") && !strcmp(volt.text,"-- V"));
 do_action(ACT_CHARGE);assert(requests==3 && errors==2);
 available=true;
 battery=(battery_snapshot_t){.soc=0,.vbat_mv=3700,.valid=true,.present=true,.is_soc_estimated=true};
 for(int i=0;i<BATT_SHUTDOWN_SAMPLES;i++)check_battery();
 assert(!s_crit_last && shutdowns==0);
 battery.soc=50;battery.vbat_mv=BATT_SHUTDOWN_MV;
 for(int i=0;i<BATT_SHUTDOWN_SAMPLES;i++)check_battery();
 assert(s_crit_last && shutdowns==1);
 battery.vbus_present=true;
 for(int i=0;i<BATT_SHUTDOWN_SAMPLES;i++)check_battery();
 assert(!s_crit_last && shutdowns==1);
 battery.vbus_present=false;battery.vbat_mv=3700;battery.soc=0;battery.is_soc_estimated=false;
 for(int i=0;i<BATT_SHUTDOWN_SAMPLES;i++)check_battery();assert(shutdowns==2);
 puts("PASS: consistent gauge/voltage-estimate/unknown display and unchanged voltage-based V203 shutdown");
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.c').write_text((UI/'components/header/header_ui.c').read_text().split('#include',1)[0]+code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
