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
#

"""Render real LVGL Companion/PC Monitor screens and exercise QR ownership and bounds."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "psk_qr"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "components/psk_qr/psk_qr_ui.c").read_text().split("#include", 1)[0]


def write(name, text):
    path = WORK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(NOTICE + text)
    return path


for name in ("esp_log.h", "esp_heap_caps.h", "host_link_sec.h", "host_link_ble.h", "host_link.h",
             "host_pc_monitor.h", "ui_theme.h", "ui_chrome.h", "ui_manager.h", "ui_feedback.h",
             "ui_metrics.h", "st7789.h", "led_control.h"):
    write(name, "#pragma once\n")

common = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#define ESP_OK 0
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define HOST_LINK_PSK_HEX_SIZE 65
#define HOST_PC_MONITOR_PERCENT_MAX 10000
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define UI_CONTENT_FADE_MS 120
#define SCREEN_BLE_MENU 1
#define SCREEN_DEV_MENU 2
#define UI_FB_READ 1
enum {INPUT_ACTION_PRESS, INPUT_ACTION_REPEAT, INPUT_ACTION_RELEASE};
enum {INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_OK, INPUT_BTN_BACK};
typedef struct {int action,button;} input_event_t;
static const char PSK[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static bool authenticated, ble_active, ble_error, psk_error, has_sample, fail_event, fail_timer;
static unsigned starts, stops, backs, notices, fail_alloc, alloc_calls, live;
static void *allocations[16];
static void *allocate(size_t size,int caps,bool zero) {
  assert(caps==3);if (++alloc_calls==fail_alloc) return NULL;
  void *p=zero ? calloc(1,size) : malloc(size);assert(p);
  for(unsigned i=0;i<16;i++)if(!allocations[i]) {allocations[i]=p;live++;return p;}
  abort();
}
static void *heap_caps_malloc(size_t n,int caps) {return allocate(n,caps,false);}
static void *heap_caps_calloc(size_t n,size_t s,int caps) {return allocate(n*s,caps,true);}
static void free_checked(void *p) {
  if(!p)return;
  for(unsigned i=0;i<16;i++)if(allocations[i]==p) {allocations[i]=NULL;--live;free(p);return;}
  abort();
}
static lv_event_dsc_t *add_event(lv_obj_t *obj,lv_event_cb_t cb,lv_event_code_t code,void *data) {
  if(fail_event)return NULL;
  return lv_obj_add_event_cb(obj,cb,code,data);
}
static lv_timer_t *create_timer(lv_timer_cb_t cb,uint32_t ms,void *data) {
  return fail_timer ? NULL : lv_timer_create(cb,ms,data);
}
static struct {lv_color_t screen_base,bg_primary,bg_secondary,text_main,text_secondary,border_accent,border_interface;} current_theme;
static void led_signal_error(void) {++notices;}
static void led_signal_warning(void) {++notices;}
static int host_link_sec_get_psk_hex(char *out,size_t cap) {assert(cap==sizeof(PSK));if(psk_error)return -1;memcpy(out,PSK,cap);return ESP_OK;}
static bool host_link_sec_is_authenticated(void) {return authenticated;}
static bool host_link_is_ble_transport(void) {return false;}
static bool host_link_ble_is_active(void) {return ble_active;}
static int host_link_ble_init(void) {return ble_error ? -1 : 0;}
static int host_link_ble_start(void) {++starts;ble_active=!ble_error;return ble_error ? -1 : 0;}
static void host_link_ble_stop(void) {++stops;ble_active=false;}
static int ui_screen_w(void) {return lv_display_get_horizontal_resolution(NULL);}
static int ui_screen_h(void) {return lv_display_get_vertical_resolution(NULL);}
static void ui_feedback(int event) {(void)event;}
static void ui_input_set_screen_handler(void (*cb)(const input_event_t*,void*),void *ctx) {assert(cb && !ctx);}
static void ui_switch_screen(int id) {assert(id==SCREEN_BLE_MENU || id==SCREEN_DEV_MENU);++backs;}
static void clear_slot(lv_event_t *event) {
  lv_obj_t **slot=lv_event_get_user_data(event);
  if(*slot==lv_event_get_target(event))*slot=NULL;
}
static void ui_screen_load_owned(lv_obj_t **slot,lv_obj_t *screen) {
  *slot=screen;lv_obj_add_event_cb(screen,clear_slot,LV_EVENT_DELETE,slot);lv_screen_load(screen);
}
static lv_obj_t *ui_chrome_screen_create(void) {
  lv_obj_t *root=lv_obj_create(NULL);lv_obj_set_style_bg_color(root,current_theme.screen_base,0);
  lv_obj_set_style_border_width(root,0,0);lv_obj_set_style_pad_all(root,0,0);
  lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);return root;
}
static lv_obj_t *bar(lv_obj_t *parent,const char *text,int h,lv_align_t align) {
  lv_obj_t *obj=lv_obj_create(parent);lv_obj_set_size(obj,lv_pct(100),h);lv_obj_align(obj,align,0,0);
  lv_obj_set_style_bg_color(obj,current_theme.bg_primary,0);lv_obj_set_style_pad_all(obj,0,0);
  lv_obj_set_style_border_width(obj,0,0);lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *label=lv_label_create(obj);lv_label_set_text(label,text);
  lv_obj_set_width(label,lv_pct(100));lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0);
  lv_obj_set_style_text_color(label,current_theme.text_main,0);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
  lv_label_set_long_mode(label,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_center(label);return obj;
}
static void ui_chrome_header(lv_obj_t *parent,const char *text,const char *icon) {(void)icon;bar(parent,text,42,LV_ALIGN_TOP_MID);}
static lv_obj_t *ui_chrome_footer(lv_obj_t *parent,const char *text) {return bar(parent,text,22,LV_ALIGN_BOTTOM_MID);}
static void ui_chrome_footer_set_text(lv_obj_t *footer,const char *text) {lv_label_set_text(lv_obj_get_child(footer,0),text);}
typedef struct {char hostname[32],volume[32],local_ip[46],public_ip[46];uint16_t cpu,ram,disk;uint32_t ram_used_mib,ram_total_mib,disk_used_mib,disk_total_mib;} host_pc_monitor_snapshot_t;
static bool host_pc_monitor_get(host_pc_monitor_snapshot_t *out) {
  *out=(host_pc_monitor_snapshot_t){.hostname="Workstation",.volume="System",.local_ip="192.168.1.42",.public_ip="203.0.113.1",.cpu=2100,.ram=4200,.disk=6500,.ram_used_mib=8192,.ram_total_mib=16384,.disk_used_mib=102400,.disk_total_mib=204800};return has_sample;
}
#define TAG qr_tag
#define make_label qr_label
#define lv_obj_add_event_cb add_event
#define free free_checked
"""
common += f'#include "{UI}/components/psk_qr/psk_qr_ui.c"\n'
common += r"""
#undef free
#undef lv_obj_add_event_cb
#undef make_label
#undef TAG
#define lv_timer_create create_timer
"""
checks = r"""
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {(void)area;(void)pixels;lv_display_flush_ready(display);}
static void tick(void) {lv_tick_inc(1000);lv_timer_handler();}
static void inside(lv_obj_t *obj,const lv_area_t *bounds) {
  if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;
  lv_area_t a;lv_obj_get_coords(obj,&a);
  if(!(a.x1>=bounds->x1 && a.x2<=bounds->x2 && a.y1>=bounds->y1 && a.y2<=bounds->y2))
    fprintf(stderr,"out of bounds: %s [%d,%d,%d,%d] parent [%d,%d,%d,%d]\n",lv_obj_check_type(obj,&lv_label_class)?lv_label_get_text(obj):"object",a.x1,a.y1,a.x2,a.y2,bounds->x1,bounds->y1,bounds->x2,bounds->y2);
  assert(a.x1>=bounds->x1 && a.x2<=bounds->x2 && a.y1>=bounds->y1 && a.y2<=bounds->y2);
  for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)inside(lv_obj_get_child(obj,i),&a);
}
static unsigned qr_red(lv_obj_t *canvas,unsigned x,unsigned y) {
  lv_draw_buf_t *buf=lv_canvas_get_draw_buf(canvas);
  const uint8_t *px=lv_draw_buf_goto_xy(buf,x,y);
  unsigned index=(*px>>(7-x%8))&1;
  return ((lv_color32_t *)buf->data)[index].red;
}
static void check_qr(lv_obj_t *view) {
  lv_obj_update_layout(s_screen);
  lv_area_t body={0,UI_CHROME_HEADER_H,ui_screen_w()-1,ui_screen_h()-UI_CHROME_FOOTER_H-1};inside(view,&body);
  lv_obj_t *slot=lv_obj_get_child(view,QR_SLOT_INDEX);assert(lv_obj_get_child_count(slot)==1);
  lv_obj_t *canvas=lv_obj_get_child(slot,0);lv_draw_buf_t *buf=lv_canvas_get_draw_buf(canvas);
  assert(buf->header.w==(ui_screen_w()==240 ? 180 : 135));
  lv_area_t a;lv_obj_get_coords(canvas,&a);
  assert(abs((a.x1+a.x2)-(body.x1+body.x2))<=1 && abs((a.y1+a.y2)-(body.y1+body.y2))<=1);
  unsigned scale=buf->header.w/45;
  assert(qr_red(canvas,0,0)==255);
  for(unsigned y=0;y<buf->header.h;y++)for(unsigned x=0;x<buf->header.w;x++)
    if(x<4*scale || y<4*scale || x>=buf->header.w-4*scale || y>=buf->header.h-4*scale)
      assert(qr_red(canvas,x,y)==255);
}
static void save_image(const char *name) {
  lv_obj_update_layout(s_screen);
  lv_draw_buf_t *shot=lv_snapshot_take(s_screen,LV_COLOR_FORMAT_RGB888);assert(shot);
  FILE *file=fopen(name,"wb");assert(file);
  fprintf(file,"P6\n%u %u\n255\n",shot->header.w,shot->header.h);
  for(unsigned y=0;y<shot->header.h;y++)for(unsigned x=0;x<shot->header.w;x++) {
    const uint8_t *px=shot->data+y*shot->header.stride+x*3;
    uint8_t rgb[]={px[2],px[1],px[0]};assert(fwrite(rgb,1,3,file)==3);
  }
  assert(fclose(file)==0);lv_draw_buf_destroy(shot);
}
"""
for kind, source in (("companion", "bluetooth/ble_companion_ui.c"), ("monitor", "dev/pc_monitor_ui.c")):
    code = common + f'#include "{UI}/screens/{source}"\n' + checks
    if kind == "companion":
        code += r"""
#define OPEN ui_companion_pairing_open
#define STOP ui_companion_pairing_stop
#define BODY s_body
static void key(void) {input_event_t ev={INPUT_ACTION_PRESS,INPUT_BTN_OK};companion_input(&ev,NULL);}
static void update(void) {phase_timer_cb(s_phase_timer);}
"""
    else:
        code += r"""
#define OPEN pc_monitor_ui_open
#define STOP pc_monitor_ui_stop
#define BODY s_content
static void key(void) {input_event_t ev={INPUT_ACTION_PRESS,INPUT_BTN_OK};handle_input(&ev,NULL);}
static void update(void) {refresh(NULL);}
"""
    code += r"""
static void close_screen(lv_obj_t *blank) {STOP();assert(live==0);lv_screen_load(blank);lv_obj_delete(s_screen);assert(!s_screen);STOP();assert(live==0);}
int main(int argc,char **argv) {
  assert(argc==2);lv_init();lv_display_t *display=lv_display_create(240,320);
  _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t pixels[320*40*2];lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
  lv_obj_t *blank=lv_obj_create(NULL);lv_screen_load(blank);
  for(unsigned rotation=0;rotation<2;rotation++)for(unsigned theme=0;theme<2;theme++) {
    lv_display_set_resolution(display,rotation ? 320 : 240,rotation ? 240 : 320);
    current_theme.screen_base=current_theme.bg_primary=lv_color_hex(theme ? 0xeeeeee : 0x080010);
    current_theme.bg_secondary=lv_color_hex(theme ? 0xdddddd : 0x180020);
    current_theme.text_main=lv_color_hex(theme ? 0x111111 : 0xffffff);
    current_theme.text_secondary=lv_color_hex(theme ? 0x333333 : 0xaaa0bb);
    current_theme.border_accent=current_theme.border_interface=lv_color_hex(theme ? 0x3050aa : 0xcc00ff);
    authenticated=false;has_sample=false;OPEN();tick();assert(live==1);
    lv_obj_t *view=lv_obj_get_child(BODY,0);check_qr(view);
    char path[512];snprintf(path,sizeof(path),"%s-%u-%u.ppm",argv[1],rotation,theme);save_image(path);
    key();lv_obj_update_layout(s_screen);assert(lv_obj_has_flag(lv_obj_get_child(view,QR_SLOT_INDEX),LV_OBJ_FLAG_HIDDEN));
    lv_area_t bounds={0,42,ui_screen_w()-1,ui_screen_h()-23};inside(view,&bounds);
    key();check_qr(view);
    authenticated=true;has_sample=true;update();tick();tick();lv_obj_update_layout(s_screen);
"""
    if kind == "companion":
        code += '    inside(BODY,&bounds);assert(live==0);\n'
    else:
        code += '    assert(lv_obj_has_flag(view,LV_OBJ_FLAG_HIDDEN));assert(!lv_obj_has_flag(s_metrics,LV_OBJ_FLAG_HIDDEN));\n    authenticated=false;update();check_qr(view);\n'
    code += r"""
    close_screen(blank);
    for(unsigned failure=1;failure<=2;failure++) {
      authenticated=false;alloc_calls=0;fail_alloc=failure;OPEN();assert(live==0);
      view=lv_obj_get_child(BODY,0);lv_obj_update_layout(s_screen);inside(view,&bounds);
      assert(!lv_obj_has_flag(lv_obj_get_child(view,KEY_INDEX),LV_OBJ_FLAG_HIDDEN));key();close_screen(blank);fail_alloc=0;
    }
    fail_event=true;OPEN();assert(live==0);close_screen(blank);fail_event=false;
    psk_error=true;OPEN();assert(live==0);view=lv_obj_get_child(BODY,0);lv_obj_update_layout(s_screen);inside(view,&bounds);close_screen(blank);psk_error=false;
    ble_error=true;OPEN();view=lv_obj_get_child(BODY,0);check_qr(view);key();close_screen(blank);ble_error=false;
    fail_timer=true;unsigned old_starts=starts;OPEN();assert(starts==old_starts);close_screen(blank);fail_timer=false;
  }
  puts("PASS: real LVGL QR/screens, portrait/landscape, light/dark themes, quiet zone, text toggle, allocation failures, navigation and cleanup");
}
"""
    test = write(kind + ".c", code)
    command = [os.environ.get("CC", "cc"), "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
               "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-DLV_USE_QRCODE=1", "-I" + str(WORK), "-I" + str(BASE),
               "-I" + str(UI / "include"), "-I" + str(UI / "components/psk_qr/include"),
               "-I" + str(UI / "screens/bluetooth/include"), "-I" + str(UI / "screens/dev/include"),
               "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"), str(test),
               str(ROOT / "firmware_p4/managed_components/lvgl__lvgl/src/libs/qrcode/qrcodegen.c"),
               str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / kind)]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(WORK / kind), str(WORK / kind)], check=True, timeout=60)
