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

# Real LVGL task previews and production draft lifecycle. Run after test_keyboard_ui.py.
from pathlib import Path
import sys,subprocess
root=Path(__file__).resolve().parents[3];ui=root/'firmware_p4/components/Applications/ui'
NOTICE=(ui/'screens/home/home_ui.c').read_text().split('#include',1)[0]
base=Path(sys.argv[1]).resolve();stubs=base/'task-stubs';stubs.mkdir(exist_ok=True)
def write(name,text):
 (stubs/name).write_text(NOTICE+'#pragma once\n'+text)
manager=(ui/'include/ui_manager.h').read_text();enums=manager[manager.index('typedef enum'):manager.index('} screen_id_t;')+len('} screen_id_t;')]
inputs=(root/'firmware_p4/components/Drivers/input_manager/include/input_manager.h').read_text();inputs=inputs[inputs.index('typedef enum'):inputs.index('} input_event_t;')+len('} input_event_t;')]
write('ui_manager.h',enums+'\n'+inputs+'''
static screen_id_t active_route=SCREEN_HOME;
static screen_id_t ui_current_screen(void){return active_route;}
static void ui_switch_screen(screen_id_t to);
static void ui_input_consume_gesture(void){}
''')
write('esp_log.h','#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n')
write('esp_heap_caps.h','''#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static bool fail_alloc;
static void *heap_caps_malloc(size_t size,int caps){assert(caps==3);return fail_alloc?NULL:malloc(size);}
#define heap_caps_free free
''')
write('ui_metrics.h','static int ui_screen_w(void){return lv_display_get_horizontal_resolution(NULL);}\nstatic int ui_screen_h(void){return lv_display_get_vertical_resolution(NULL);}\n')
write('ui_theme.h','''static struct {lv_color_t screen_base,bg_primary,bg_secondary,border_accent,border_inactive,text_main,border_interface;}current_theme;
''')
write('led_control.h','static void led_signal_warning(void){}\nstatic void led_signal_error(void){}')
write('menu_component_ui.h','''static int selection;
static int menu_component_active_selection(void){return selection;}
static void menu_component_restore_selection(int value){selection=value;}
''')
write('menu_ui.h','''static const screen_id_t apps[]={SCREEN_FILES,SCREEN_SETTINGS,SCREEN_LORA_CHAT,SCREEN_NFC_MENU,SCREEN_IR_MENU,SCREEN_PLAYER,SCREEN_WIFI_MENU};
static int menu_catalog_count(void){return sizeof(apps)/sizeof(apps[0]);}
static screen_id_t menu_catalog_target(int i){return apps[i];}
static const char *menu_catalog_name(int i){static const char *names[]={"Files","Settings","LoRa","NFC","Infrared","Player","Wi-Fi"};return names[i];}
''')
write('nfc_read_ui.h','static bool ui_nfc_read_can_resume(void){return true;}')
write('rfid_menu_ui.h','static bool ui_rfid_menu_can_resume(void){return true;}')
write('notify_ui.h','#define NOTIFY_WARNING 1\nstatic int warnings;\nstatic void notify(int level,const char *text){(void)level;(void)text;warnings++;}')
write('ui_chrome.h','static char breadcrumb[32];\nstatic void ui_chrome_set_breadcrumb_root(const char *root){snprintf(breadcrumb,sizeof(breadcrumb),"%s",root);}')
write('ui_feedback.h','#define UI_FB_NAV 1\nstatic void ui_feedback(int value){(void)value;}')
code='''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
lv_group_t *main_group;
#define TAG keyboard_tag
#define s_count keyboard_count
#define s_title keyboard_title
'''+f'#include "{ui}/components/keyboard/keyboard_ui.c"\n'+'''
#undef TAG
#undef s_count
#undef s_title
'''+f'#include "{ui}/screens/home/app_switcher_ui.c"\n'+r'''
static int switches;
static void ui_switch_screen(screen_id_t to){
 ui_tasks_before_switch(active_route,to);
 keyboard_close();
 lv_obj_t *old=lv_screen_active();
 lv_obj_t *screen=lv_obj_create(NULL);
 lv_obj_set_style_bg_color(screen,lv_color_hex(0x224488),0);
 lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
 lv_obj_t *label=lv_label_create(screen);lv_label_set_text_fmt(label,"Actual screen %d",to);
 lv_screen_load(screen);active_route=to;selection=0;switches++;
 ui_tasks_after_switch(to);lv_obj_delete(old);
}
static void key(input_button_t b){input_event_t event={.button=b,.action=INPUT_ACTION_PRESS};ui_tasks_input(&event);}
static void choose(int app){
 assert(ui_tasks_is_open());
 for(int n=0;n<TASK_COUNT && s_tasks[s_selected].app!=app;n++)key(INPUT_BTN_RIGHT);
 assert(s_tasks[s_selected].app==app);key(INPUT_BTN_OK);
}
static int submitted;
static void saved(const char *text,void *context){assert(context==&submitted);assert(!strcmp(text,"Retained draft"));submitted++;}
int main(void){
 lv_init();lv_display_t *display=lv_display_create(240,320);main_group=lv_group_create();
 current_theme.text_main=lv_color_hex(0xffffff);current_theme.border_accent=lv_color_hex(0xaa22ff);
 ui_tasks_toggle();assert(s_count==0 && ui_tasks_is_open());key(INPUT_BTN_BACK);
 ui_switch_screen(SCREEN_FILES);selection=3;ui_tasks_toggle();
 assert(s_count==1 && s_tasks[0].app==0 && s_tasks[0].selection==3);
 assert(s_tasks[0].preview.header.w==240 && s_tasks[0].preview.header.h==320);
 assert(s_tasks[0].pixels[0]!=0 || s_tasks[0].pixels[1]!=0);
 int count=switches;key(INPUT_BTN_OK);assert(!ui_tasks_is_open() && switches==count);
 ui_tasks_toggle();key(INPUT_BTN_DOWN);assert(active_route==SCREEN_MENU);
 ui_switch_screen(SCREEN_LORA_CHAT);ui_switch_screen(SCREEN_LORA_MQTT);
 keyboard_open_text("Retained draft",40,saved,&submitted);
 lv_textarea_set_cursor_pos(s_text,4);s_page=2;set_map();
 ui_tasks_toggle();assert(ui_tasks_is_open() && s_tasks[0].draft!=NULL);
 key(INPUT_BTN_DOWN);assert(!keyboard_is_open());
 ui_switch_screen(SCREEN_FILES);ui_tasks_toggle();choose(2);
 assert(active_route==SCREEN_LORA_MQTT && keyboard_is_open() && !strcmp(breadcrumb,"LoRa"));
 assert(!strcmp(lv_textarea_get_text(s_text),"Retained draft") && lv_textarea_get_cursor_pos(s_text)==4 && s_page==2);
 submit();assert(submitted==1 && !keyboard_is_open());
 for(int i=0;i<7;i++){ui_switch_screen(SCREEN_MENU);ui_switch_screen(apps[i]);ui_tasks_toggle();key(INPUT_BTN_BACK);}
 assert(s_count==5);for(int i=0;i<s_count;i++)for(int j=i+1;j<s_count;j++)assert(s_tasks[i].app!=s_tasks[j].app);
 ui_switch_screen(SCREEN_WIFI_SCAN);ui_tasks_toggle();assert(!s_tasks[0].resumable);key(INPUT_BTN_DOWN);
 ui_tasks_toggle();choose(6);assert(active_route==SCREEN_WIFI_MENU);
 keyboard_open_text("Unsupported",40,NULL,NULL);count=warnings;ui_tasks_toggle();assert(!ui_tasks_is_open()&&keyboard_is_open()&&warnings==count+1);keyboard_close();
 fail_alloc=true;ui_tasks_toggle();assert(!ui_tasks_is_open());fail_alloc=false;
 lv_display_set_resolution(display,320,240);ui_tasks_toggle();assert(ui_tasks_is_open());lv_obj_update_layout(s_overlay);
 assert(lv_obj_get_y(s_hint)>=0 && lv_obj_get_y(s_hint)+lv_obj_get_height(s_hint)<=240);
 ui_tasks_reset();assert(s_count==0 && !ui_tasks_is_open());
 puts("PASS: production task cards, real snapshots, bounded MRU, current return, retained editor/cursor/page/callback, unsupported operation reopen, OOM and landscape");
}
'''
p=base/'task-test.c';p.write_text(NOTICE+code)
cmd=['cc','-std=gnu11','-g','-fsanitize=undefined',str(p),'-I'+str(stubs),'-I'+str(base),'-I'+str(ui/'components/keyboard/include'),'-I'+str(ui/'screens/home/include'),'-I'+str(root/'firmware_p4/managed_components/lvgl__lvgl'),'-I'+str(root/'firmware_p4/managed_components/lvgl__lvgl/src'),str(base/'build/lvgl-build/lib/liblvgl.a'),'-lm','-o',str(base/'task-test')]
subprocess.run(cmd,check=True);subprocess.run([str(base/'task-test')],check=True)
