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

"""Exercise the Battery screen with native LVGL after test_keyboard_ui.py."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
base = Path(sys.argv[1]).resolve()
stubs = base / "battery-stubs"
stubs.mkdir(parents=True, exist_ok=True)
notice = (UI / "screens/settings/battery_settings_ui.c").read_text().split("#include", 1)[0]
for name in ("battery_service.h", "led_control.h", "tos_config.h", "ui_chrome.h",
             "ui_manager.h", "ui_metrics.h", "ui_semantic.h", "ui_theme.h"):
    (stubs / name).write_text(notice + "#pragma once\n")
(stubs / "esp_log.h").write_text(notice + """
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
""")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define UI_COL_SUCCESS 0x44cc44
#define UI_COL_WARNING 0xffcc00
#define SCREEN_SETTINGS 1
#define INPUT_ACTION_PRESS 1
#define INPUT_BTN_OK 1
#define INPUT_BTN_LEFT 2
#define INPUT_BTN_RIGHT 3
#define INPUT_BTN_BACK 4
#define CHARGE_STATUS_CHARGE_DONE 3
#define ESP_OK 0
typedef int esp_err_t;
typedef struct {int action,button;} input_event_t;
typedef struct {int soc,vbat_mv,chg;bool present,charging,vbus_present,low,is_soc_estimated;} battery_snapshot_t;
typedef struct {int vbat_mv,vbus_mv;bool power_good;} charger_t;
typedef struct {bool requested,current_valid;int battery_current_ma;const char *status;charger_t charger;} battery_charge_snapshot_t;
static struct {lv_color_t screen_base,bg_secondary,border_accent,text_main,text_secondary;} current_theme;
static bool valid=true,fail_save;
static uint16_t capacity=2450;
static unsigned backs;
static battery_snapshot_t snapshot={.soc=50,.vbat_mv=3700,.present=true,.is_soc_estimated=true};
static battery_charge_snapshot_t charge={.requested=true,.current_valid=true,.battery_current_ma=-120,.status="On battery",.charger={3700,0,false}};
static bool battery_service_get(battery_snapshot_t *out){*out=snapshot;return valid;}
static bool battery_service_get_charge(battery_charge_snapshot_t *out){*out=charge;return valid;}
static uint16_t battery_service_capacity_mah(void){return capacity;}
static int tos_config_set_battery_capacity(uint16_t value){if(fail_save)return 1;capacity=value;return 0;}
static int battery_service_request_charge(bool enable){charge.requested=enable;return 0;}
static void led_signal_info(void){}
static void led_signal_warning(void){}
static void led_signal_error(void){}
static int ui_screen_w(void){return lv_display_get_horizontal_resolution(NULL);}
static int ui_screen_h(void){return lv_display_get_vertical_resolution(NULL);}
static void ui_chrome_header(lv_obj_t *root,const char *title,const char *icon){
  (void)icon;lv_obj_t *label=lv_label_create(root);lv_label_set_text(label,title);
}
static lv_obj_t *ui_chrome_footer(lv_obj_t *root,const char *text){
  lv_obj_t *label=lv_label_create(root);lv_label_set_text(label,text);
  lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0);
  lv_obj_align(label,LV_ALIGN_BOTTOM_MID,0,0);return label;
}
static void ui_chrome_footer_set_text(lv_obj_t *label,const char *text){lv_label_set_text(label,text);}
static void ui_input_set_screen_handler(void (*cb)(const input_event_t *,void *),void *ctx){(void)cb;(void)ctx;}
static void clear_slot(lv_event_t *e){lv_obj_t **slot=lv_event_get_user_data(e);if(*slot==lv_event_get_target(e))*slot=NULL;}
static void ui_screen_load_owned(lv_obj_t **slot,lv_obj_t *root){
  *slot=root;lv_obj_add_event_cb(root,clear_slot,LV_EVENT_DELETE,slot);lv_screen_load(root);
}
static void ui_switch_screen(int screen){assert(screen==SCREEN_SETTINGS);backs++;}
""" + f'#include "{UI}/screens/settings/battery_settings_ui.c"\n' + r"""
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *buffer){
  (void)area;(void)buffer;lv_display_flush_ready(display);
}
static void check_bounds(void){
  lv_obj_update_layout(s_screen);
  lv_obj_t *labels[]={s_pct,s_state,s_source,s_current,s_caption,s_note,s_reason};
  for(unsigned i=0;i<sizeof(labels)/sizeof(labels[0]);i++){
    lv_area_t area;lv_obj_get_coords(labels[i],&area);
    assert(area.x1>=0 && area.x2<ui_screen_w());
    if(area.y2>=ui_screen_h()-UI_CHROME_FOOTER_H)
      fprintf(stderr,"Overflow: %s y=%d..%d\n",lv_label_get_text(labels[i]),(int)area.y1,(int)area.y2);
    assert(area.y1>=UI_CHROME_HEADER_H && area.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
  }
}
int main(void){
  lv_init();lv_display_t *display=lv_display_create(240,320);
  _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
  lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,flush);
  for(int wide=0;wide<2;wide++){
    lv_display_set_resolution(display,wide?320:240,wide?240:320);
    for(int theme=0;theme<2;theme++){
      current_theme.screen_base=lv_color_hex(theme?0xffffff:0x101010);
      current_theme.text_main=lv_color_hex(theme?0x101010:0xffffff);
      current_theme.text_secondary=lv_color_hex(theme?0x404040:0xcccccc);
      for(int state=0;state<3;state++){
        valid=state!=2;snapshot.charging=state==1;snapshot.vbus_present=state==1;
        ui_battery_settings_open();check_bounds();
        assert(lv_color_eq(lv_obj_get_style_text_color(s_note,0),current_theme.text_secondary));
        input_event_t event={INPUT_ACTION_PRESS,INPUT_BTN_RIGHT};
        battery_settings_input(&event,NULL);check_bounds();
        fail_save=true;battery_settings_input(&event,NULL);check_bounds();fail_save=false;
        lv_tick_inc(BATTERY_SAVE_STATUS_MS);refresh_cb(NULL);check_bounds();
        event.button=INPUT_BTN_BACK;battery_settings_input(&event,NULL);
        ui_battery_settings_close();ui_battery_settings_close();assert(s_refresh_timer==NULL);
        lv_screen_load(lv_obj_create(NULL));lv_obj_delete(s_screen);assert(s_screen==NULL);
        lv_tick_inc(BATTERY_REFRESH_MS+1);lv_timer_handler();
      }
    }
  }
  assert(backs==12);
  puts("PASS: Battery 240x320/320x240 layout, light/dark themes, saved/error/unavailable states and repeated close/delete");
}
"""
test = base / "battery_layout_test.c"
test.write_text(notice + code)
subprocess.run([
    "cc", "-std=gnu11", "-g", "-fsanitize=undefined", "-DLV_CONF_INCLUDE_SIMPLE",
    "-DLV_KCONFIG_IGNORE", str(test), "-I" + str(stubs), "-I" + str(base),
    "-I" + str(UI / "screens/settings/include"),
    "-I" + str(ROOT / "firmware_p4/components/Drivers/bq25896/include"),
    "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"),
    str(base / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(base / "battery_layout_test"),
], check=True)
subprocess.run([str(base / "battery_layout_test")], check=True, timeout=15)
