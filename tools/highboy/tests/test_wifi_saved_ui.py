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

"""Exercise the complete Connect Wi-Fi screen with host LVGL and controlled radio results."""
from pathlib import Path
import os
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
UI=ROOT/'firmware_p4/components/Applications/ui'
BASE=Path(sys.argv[1]).resolve()
WORK=BASE/'saved-wifi'
WORK.mkdir(parents=True,exist_ok=True)
NOTICE=(UI/'screens/connect_wifi/connect_wifi_ui.c').read_text().split('#include',1)[0]
for name in ('st7789.h','keyboard_ui.h','menu_component_ui.h','msgbox_ui.h','ui_chrome.h',
             'ui_manager.h','ui_metrics.h','ui_theme.h','waves_ui.h','wifi_service.h','wifi_live_scan_ui.h',
             'sys_prio.h','led_control.h','freertos/FreeRTOS.h','freertos/task.h','esp_log.h'):
 p=WORK/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(NOTICE+'#pragma once\n')
CODE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "lvgl.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
#define UI_CHROME_HEADER_H 40
#define UI_CHROME_FOOTER_H 24
#define SCREEN_CONNECT_WIFI 1
#define SCREEN_WIFI_SETTINGS 2
#define KEYBOARD_PASSWORD 1
#define SPI_WIFI_BAND_DUAL 0
#define WIFI_AUTH_OPEN 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
enum {INPUT_ACTION_PRESS,INPUT_ACTION_REPEAT};
enum {INPUT_BTN_UP,INPUT_BTN_DOWN,INPUT_BTN_BACK,INPUT_BTN_LEFT,INPUT_BTN_RIGHT,INPUT_BTN_OK};
typedef int esp_err_t,portMUX_TYPE;
typedef struct {int action,button;} input_event_t;
typedef struct {lv_obj_t *root;} menu_component_t;
typedef struct {char ssid[33];int8_t rssi;uint8_t authmode,primary;} wifi_ap_record_t;
typedef struct {int error;uint16_t count;} wifi_live_scan_status_t;
typedef struct {const char *title,*initial;int max_chars,min_chars,mode;} keyboard_config_t;
static struct {lv_color_t screen_base,bg_primary,bg_secondary,text_main,text_secondary,border_accent,border_inactive;} current_theme;
static int current_screen=SCREEN_CONNECT_WIFI,connect_error,keyboards,messages;
static bool connected,received_null,fail_timer;
static lv_obj_t *scan_screen;
static void (*scan_callback)(void*),(*task_fn)(void*),(*keyboard_cb)(const char*,void*),(*message_cb)(bool);
void ui_connect_wifi_close(void);
static int ui_screen_w(void) {return lv_display_get_horizontal_resolution(NULL);}
static int ui_screen_h(void) {return lv_display_get_vertical_resolution(NULL);}
static int ui_current_screen(void) {return current_screen;}
static void ui_switch_screen(int screen) {ui_connect_wifi_close();current_screen=screen;}
static void ui_input_set_screen_handler(void (*cb)(const input_event_t*,void*),void *ctx) {(void)cb;(void)ctx;}
static void clear_slot(lv_event_t *ev) {lv_obj_t **slot=lv_event_get_user_data(ev);if(*slot==lv_event_get_target(ev))*slot=NULL;}
static void ui_screen_load_owned(lv_obj_t **slot,lv_obj_t *root) {lv_obj_add_event_cb(root,clear_slot,LV_EVENT_DELETE,slot);lv_screen_load(root);}
static void led_signal_info(void) {} static void led_signal_warning(void) {} static void led_signal_error(void) {}
static void ui_chrome_header(lv_obj_t *root,const char *title,const char *icon) {
 (void)icon;lv_obj_t *label=lv_label_create(root);lv_label_set_text(label,title);lv_obj_set_style_text_color(label,current_theme.text_main,0);
}
static lv_obj_t *ui_chrome_footer(lv_obj_t *root,const char *text) {
 lv_obj_t *footer=lv_obj_create(root);lv_obj_set_size(footer,lv_pct(100),UI_CHROME_FOOTER_H);
 lv_obj_align(footer,LV_ALIGN_BOTTOM_MID,0,0);lv_obj_set_style_pad_all(footer,0,0);
 lv_obj_t *label=lv_label_create(footer);lv_obj_set_width(label,ui_screen_w()-12);
 lv_label_set_long_mode(label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_label_set_text(label,text);return footer;
}
static void ui_chrome_footer_set_text(lv_obj_t *footer,const char *text) {lv_label_set_text(lv_obj_get_child(footer,0),text);}
static void waves_create(lv_obj_t *root,lv_align_t align,int x,int y,const char *text,const char *icon) {(void)root;(void)align;(void)x;(void)y;(void)text;(void)icon;}
static menu_component_t menu_component_create(lv_obj_t *root,const char *title,const char *icon) {ui_chrome_header(root,title,icon);return (menu_component_t){root};}
static void menu_component_add_item(menu_component_t *menu,const char *icon,const char *text) {(void)icon;lv_obj_t *label=lv_label_create(menu->root);lv_label_set_text(label,text);}
static void menu_component_next(menu_component_t *menu) {(void)menu;}
static void menu_component_prev(menu_component_t *menu) {(void)menu;}
static void keyboard_open_config(const keyboard_config_t *cfg,void (*cb)(const char*,void*),void *ctx) {
 assert(cfg->initial==NULL && cfg->mode==KEYBOARD_PASSWORD && !ctx);keyboards++;keyboard_cb=cb;
}
static void msgbox_open(const char *icon,const char *msg,const char *yes,const char *no,void (*cb)(bool)) {
 (void)icon;(void)msg;(void)yes;(void)no;messages++;message_cb=cb;
}
static int wifi_service_connect_to_ap(const char *ssid,const char *password) {assert(!strcmp(ssid,"Home"));received_null=password==NULL;return connect_error;}
static const char *wifi_service_get_connected_ssid(void) {return connected ? "Home" : NULL;}
static int wifi_service_get_ip(int interface,uint8_t *ip) {assert(!interface);memcpy(ip,(uint8_t[]){10,0,0,2},4);return ESP_OK;}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,int stack,void *arg,int priority,void *handle,int core) {
 (void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)core;task_fn=fn;return pdPASS;
}
static void vTaskDelay(int ms) {(void)ms;}
static void vTaskDelete(void *task) {assert(!task);}
static int wifi_live_scan_ui_open(uint8_t band,void (*cb)(void*),void *ctx) {
 assert(!band && !ctx);scan_callback=cb;
 lv_obj_t *previous=lv_screen_active();scan_screen=lv_obj_create(NULL);lv_screen_load(scan_screen);lv_obj_delete(previous);
 return ESP_OK;
}
static bool wifi_live_scan_ui_change_page(uint16_t *page,int size,bool next) {(void)page;(void)size;(void)next;return true;}
static void wifi_live_scan_get_status(wifi_live_scan_status_t *out) {*out=(wifi_live_scan_status_t){.count=2};}
static int wifi_live_scan_get_record(uint16_t index,wifi_ap_record_t *out) {
 assert(index<2);*out=(wifi_ap_record_t){.rssi=-40,.primary=1,.authmode=3};strcpy(out->ssid,index ? "Other" : "Home");return ESP_OK;
}
static lv_timer_t *create_timer(lv_timer_cb_t cb,uint32_t period,void *user) {return fail_timer ? NULL : lv_timer_create(cb,period,user);}
#define lv_timer_create create_timer
#include "SCREEN_SOURCE"
#undef lv_timer_create
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) {(void)area;(void)data;lv_display_flush_ready(display);}
static void send(int button) {connect_wifi_input(&(input_event_t){.action=INPUT_ACTION_PRESS,.button=button},NULL);}
static void check_bounds(lv_obj_t *obj) {
 lv_area_t area;lv_obj_get_coords(obj,&area);assert(area.x1>=0 && area.y1>=0 && area.x2<ui_screen_w() && area.y2<ui_screen_h());
}
static void scan_done(void) {
 assert(s_screen==NULL && s_connect_timer==NULL);
 scan_callback(NULL);lv_obj_delete(scan_screen);scan_screen=NULL;lv_obj_update_layout(s_screen);
}
static void close_screen(lv_obj_t *blank) {
 ui_connect_wifi_close();assert(!s_connect_timer);lv_screen_load(blank);lv_obj_delete(s_screen);assert(!s_screen);
}
int main(void) {
 lv_init();lv_display_t *display=lv_display_create(240,320);
 _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
 lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
 lv_obj_t *blank=lv_screen_active();
 for(unsigned wide=0;wide<2;wide++)for(unsigned theme=0;theme<2;theme++) {
  lv_display_set_resolution(display,wide ? 320 : 240,wide ? 240 : 320);
  current_theme.screen_base=lv_color_hex(theme ? 0xffffff : 0x101010);
  current_theme.bg_primary=current_theme.bg_secondary=current_theme.screen_base;
  current_theme.text_main=current_theme.text_secondary=current_theme.border_accent=lv_color_hex(theme ? 0x101010 : 0xffffff);
  current_screen=SCREEN_CONNECT_WIFI;keyboards=messages=0;connected=false;connect_error=ESP_ERR_NOT_FOUND;
  ui_connect_wifi_open();scan_done();assert(s_connect_timer && s_ap_count==2);
  check_bounds(s_join_footer);for(int i=0;i<s_ap_count;i++)check_bounds(s_row[i]);
  assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen,0),current_theme.screen_base));
  lv_timer_t *timer=s_connect_timer;send(INPUT_BTN_RIGHT);assert(s_connect_timer==timer);
  send(INPUT_BTN_OK);assert(connection_busy() && !keyboards);task_fn(NULL);connect_done_cb(NULL);assert(keyboards==1);
  connect_error=0;connected=true;keyboard_cb("known-password",NULL);task_fn(NULL);connect_done_cb(NULL);assert(messages==1);
  close_screen(blank);ui_connect_wifi_open();scan_done();keyboards=0;
  send(INPUT_BTN_OK);task_fn(NULL);connect_done_cb(NULL);assert(received_null && !keyboards);
  send(INPUT_BTN_OK);send(INPUT_BTN_BACK);task_fn(NULL);assert(s_connect_state==CONNECT_STATE_IDLE);close_screen(blank);
  current_screen=SCREEN_CONNECT_WIFI;fail_timer=true;ui_connect_wifi_open();scan_done();
  assert(!s_connect_timer && s_scan_state==SCAN_FAIL);send(INPUT_BTN_BACK);assert(current_screen==SCREEN_WIFI_SETTINGS);
  close_screen(blank);fail_timer=false;
 }
 puts("PASS: real LVGL saved Wi-Fi flow, scanner replacement, timer reuse, Back/late results, timer failure, themes and both orientations");
}
'''
source=CODE.replace('SCREEN_SOURCE',str(UI/'screens/connect_wifi/connect_wifi_ui.c'))
(WORK/'test.c').write_text(NOTICE+source)
args=[os.environ.get('CC','cc'),'-std=gnu11','-g','-fsanitize=undefined,bounds','-fno-sanitize-recover=all',
      '-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(WORK),'-I'+str(BASE),
      '-I'+str(UI/'screens/connect_wifi/include'),'-I'+str(ROOT/'firmware_p4/managed_components/lvgl__lvgl'),
      str(WORK/'test.c'),str(BASE/'build/lvgl-build/lib/liblvgl.a'),'-lm','-o',str(WORK/'test')]
subprocess.run(args,check=True,timeout=60)
subprocess.run([str(WORK/'test')],check=True,timeout=30)
