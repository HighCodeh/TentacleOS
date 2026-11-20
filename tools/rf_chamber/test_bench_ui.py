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

"""Run the actual bench screen with native LVGL; hardware services are explicit fakes."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
UI=ROOT/'firmware_p4/components/Applications/ui'
base=Path(sys.argv[1]).resolve()
stubs=base/'bench-stubs';stubs.mkdir(parents=True,exist_ok=True)
notice=(UI/'screens/lora/rf_bench_ui.c').read_text().split('#include',1)[0]
for name in ['esp_log.h','esp_system.h','led_control.h','menu_component_ui.h','power_policy.h','ui_chrome.h','ui_manager.h','ui_metrics.h','ui_theme.h']:
 (stubs/name).write_text(notice+'#pragma once\n')
(stubs/'esp_err.h').write_text(notice+'#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL 1\n#define ESP_ERR_NO_MEM 2\n')
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "rf_tag_test.h"
#include "rf_passive.h"
#define ESP_LOGE(tag,...) ((void)(tag))
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define INPUT_ACTION_PRESS 1
#define INPUT_ACTION_REPEAT 2
#define INPUT_ACTION_RELEASE 3
#define INPUT_BTN_OK 1
#define INPUT_BTN_DOWN 2
#define INPUT_BTN_UP 3
#define INPUT_BTN_LEFT 4
#define INPUT_BTN_RIGHT 5
#define INPUT_BTN_BACK 6
typedef struct{int action,button;}input_event_t;
typedef struct{int selected,count;lv_obj_t*root;}menu_component_t;
static struct{lv_color_t screen_base,text_main;}current_theme;
static void(*handler)(const input_event_t*,void*);
static rf_tag_test_status_t mock_scan;
static rf_passive_status_t mock_bench={.path=1,.actual_path=1};
static unsigned starts,stops,restarts,saves;
static int start_error;
static const char*esp_err_to_name(int e){(void)e;return "ESP_FAIL";}
static void led_signal_error(void){}
static void esp_restart(void){restarts++;}
static int ui_screen_w(void){return lv_display_get_horizontal_resolution(NULL);}
static int ui_screen_h(void){return lv_display_get_vertical_resolution(NULL);}
static void ui_chrome_header(lv_obj_t*r,const char*t,const char*i){(void)i;lv_obj_t*l=lv_label_create(r);lv_label_set_text(l,t);lv_obj_set_pos(l,8,8);}
static lv_obj_t*ui_chrome_footer(lv_obj_t*r,const char*t){lv_obj_t*l=lv_label_create(r);lv_label_set_text(l,t);lv_obj_align(l,LV_ALIGN_BOTTOM_MID,0,-2);return l;}
static void ui_chrome_footer_set_text(lv_obj_t*l,const char*t){lv_label_set_text(l,t);}
static menu_component_t menu_component_create(lv_obj_t*r,const char*t,const char*i){ui_chrome_header(r,t,i);return(menu_component_t){.root=r};}
static void menu_component_add_item(menu_component_t*m,const char*i,const char*t){(void)i;lv_obj_t*l=lv_label_create(m->root);lv_label_set_text(l,t);lv_obj_set_pos(l,10,48+32*m->count++);}
static void menu_component_set_hint(menu_component_t*m,const char*t){ui_chrome_footer(m->root,t);}
static void menu_component_next(menu_component_t*m){m->selected=(m->selected+1)%m->count;}
static void menu_component_prev(menu_component_t*m){m->selected=(m->selected+m->count-1)%m->count;}
static int menu_component_get_selected(menu_component_t*m){return m->selected;}
static void power_policy_set_display_hold(lv_obj_t*r,bool hold){assert(r&&hold);}
static void ui_input_set_screen_handler(void(*fn)(const input_event_t*,void*),void*c){assert(!c);handler=fn;}
static void ui_screen_load_owned(lv_obj_t**slot,lv_obj_t*r){*slot=r;lv_screen_load(r);}
void rf_tag_test_snapshot(rf_tag_test_status_t*s){*s=mock_scan;}
bool rf_tag_test_busy(void){return mock_scan.busy;}
void rf_tag_test_stop(void){stops++;}
int rf_tag_test_start(rf_tag_test_kind_t kind){assert(!mock_scan.busy);starts++;if(start_error)return start_error;mock_scan=(rf_tag_test_status_t){.kind=kind,.phase=RF_TAG_TEST_READING,.busy=true};return 0;}
void rf_passive_snapshot(rf_passive_status_t*s){*s=mock_bench;}
int rf_passive_select(uint8_t p){assert(p>=1&&p<=4&&!mock_scan.busy);mock_bench.path=mock_bench.actual_path=p;saves++;return 0;}
int rf_passive_save_boot(bool enabled,uint8_t p){assert(!enabled&&p>=1&&p<=4);return 0;}
"""+f'#include "{UI}/screens/lora/rf_bench_ui.c"\n'+r"""
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*b){(void)a;(void)b;lv_display_flush_ready(d);}
static void key(int k){input_event_t e={.button=k,.action=INPUT_ACTION_PRESS};handler(&e,NULL);lv_obj_update_layout(s_screen);}
static void check_bounds(void){
 lv_obj_update_layout(s_screen);lv_area_t body,footer,title,detail;
 lv_obj_get_coords(s_body,&body);lv_obj_get_coords(s_footer,&footer);
 lv_obj_get_coords(s_title,&title);lv_obj_get_coords(s_detail,&detail);
 assert(body.x1>=0&&body.x2<ui_screen_w()&&body.y1>=UI_CHROME_HEADER_H);
 assert(body.y2<footer.y1&&footer.y2<ui_screen_h());
 assert(title.x1>=body.x1&&title.x2<=body.x2&&detail.x2<=body.x2);
}
int main(void){
 lv_init();lv_display_t*d=lv_display_create(240,320);
 _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);
 for(int wide=0;wide<2;wide++)for(int light=0;light<2;light++){
  lv_display_set_resolution(d,wide?320:240,wide?240:320);
  current_theme.screen_base=lv_color_hex(light?0xffffff:0);current_theme.text_main=lv_color_hex(light?0:0xffffff);
  unsigned before=starts;ui_rf_bench_open();assert(starts==before&&s_view==VIEW_MENU);
  key(INPUT_BTN_OK);check_bounds();assert(s_view==VIEW_SWITCH);
  for(int i=0;i<4;i++){key(INPUT_BTN_RIGHT);check_bounds();assert(mock_bench.path>=1&&mock_bench.path<=4);}
  key(INPUT_BTN_BACK);key(INPUT_BTN_DOWN);key(INPUT_BTN_OK);check_bounds();assert(s_view==VIEW_NFC&&starts==before);
  key(INPUT_BTN_OK);assert(mock_scan.busy&&starts==before+1);key(INPUT_BTN_BACK);assert(s_wait_back&&s_view==VIEW_NFC);
  mock_scan.busy=false;refresh(NULL);assert(s_view==VIEW_MENU);
  key(INPUT_BTN_DOWN);key(INPUT_BTN_DOWN);key(INPUT_BTN_OK);check_bounds();assert(s_view==VIEW_RFID);
  key(INPUT_BTN_OK);mock_scan=(rf_tag_test_status_t){.kind=RF_TAG_TEST_LF,.phase=RF_TAG_TEST_FOUND,.uid={1,2,3,4,5},.uid_len=5};refresh(NULL);check_bounds();
  assert(strstr(lv_label_get_text(s_detail),"01 02 03 04 05")&&strstr(lv_label_get_text(s_title),"TAG READ"));
  mock_scan.phase=RF_TAG_TEST_HELD;mock_scan.busy=true;refresh(NULL);assert(strstr(lv_label_get_text(s_title),"OFF NOT CONFIRMED"));
  key(INPUT_BTN_BACK);assert(s_wait_back);mock_scan.busy=false;refresh(NULL);
  build(VIEW_NFC);key(INPUT_BTN_OK);mock_scan=(rf_tag_test_status_t){.phase=RF_TAG_TEST_FOUND,.uid={0,1,2,3,4,5,6,7,8,9},.uid_len=10};refresh(NULL);check_bounds();
  key(INPUT_BTN_BACK);key(INPUT_BTN_BACK);assert(s_view==VIEW_EXIT);key(INPUT_BTN_OK);assert(restarts);
  lv_obj_t*old=s_screen;ui_rf_bench_close();assert(s_timer==NULL);lv_screen_load(lv_obj_create(NULL));lv_obj_delete(old);assert(s_screen==NULL);
 }
 puts("PASS: actual LVGL bench portrait/landscape, themes, switch controls, explicit start, cancelled/held OFF, 5/10-byte IDs and teardown");
}
"""
test=base/'bench_ui_test.c';test.write_text(notice+code)
subprocess.run(['cc','-std=gnu11','-g','-Wall','-Wextra','-Werror','-fsanitize=undefined','-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE',str(test),'-I'+str(stubs),'-I'+str(base),'-I'+str(UI/'screens/lora/include'),'-I'+str(UI/'include'),'-I'+str(ROOT/'firmware_p4/components/Service/rf_chamber/include'),'-I'+str(ROOT/'firmware_p4/components/Drivers/rf_lab/include'),'-I'+str(ROOT/'firmware_p4/managed_components/lvgl__lvgl'),str(base/'build/lvgl-build/lib/liblvgl.a'),'-lm','-o',str(base/'bench_ui_test')],check=True)
subprocess.run([str(base/'bench_ui_test')],check=True,timeout=20)
