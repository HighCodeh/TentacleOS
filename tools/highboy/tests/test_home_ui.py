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

"""Run the production Home screen with native LVGL after test_keyboard_ui.py."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
base = Path(sys.argv[1]).resolve()
stubs = base / "home-stubs"
stubs.mkdir(parents=True, exist_ok=True)
notice = (UI / "screens/home/home_ui.c").read_text().split("#include", 1)[0]

def write(name, code):
    (stubs / name).write_text(notice + "#pragma once\n" + code)

write("esp_log.h", '#define ESP_LOGW(tag, ...) ((void)(tag))\n')
write("assets_manager.h", "")
write("sys_time.h", "")
write("ui_chrome.h", "")
write("ui_manager.h", "")
write("ui_metrics.h", "")
write("ui_theme.h", "")
write("menu_ui.h", "")
write("favorites.h", "")
write("ui_feedback.h", "")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
typedef enum {SCREEN_NONE,SCREEN_MENU,SCREEN_SETTINGS,SCREEN_WIFI_MENU,SCREEN_BLE_MENU,
 SCREEN_NFC_MENU,SCREEN_RFID_MENU,SCREEN_IR_MENU,SCREEN_SUBGHZ_MENU,SCREEN_LORA_CHAT,
 SCREEN_BADUSB_MENU,SCREEN_GPIO,SCREEN_FILES,SCREEN_PLAYER,SCREEN_DEV_MENU,SCREEN_OCTOBIT_STATUS,
 SCREEN_COUNT} screen_id_t;
#define UI_FB_NAV 1
static bool pins[SCREEN_COUNT];
static const screen_id_t catalog[]={SCREEN_WIFI_MENU,SCREEN_BLE_MENU,SCREEN_NFC_MENU,
 SCREEN_RFID_MENU,SCREEN_IR_MENU,SCREEN_SUBGHZ_MENU,SCREEN_LORA_CHAT,SCREEN_BADUSB_MENU,
 SCREEN_GPIO,SCREEN_SETTINGS,SCREEN_FILES,SCREEN_PLAYER,SCREEN_DEV_MENU};
static int menu_ui_catalog_count(void) {return sizeof(catalog)/sizeof(catalog[0]);}
static screen_id_t menu_ui_catalog_target(int index) {return catalog[index];}
static const char *menu_ui_catalog_name(int index) {(void)index;return "Pinned application";}
static bool favorites_is(screen_id_t screen) {return pins[screen];}
static void ui_feedback(int type) {assert(type==UI_FB_NAV);}
static int last_open;

#define INPUT_ACTION_PRESS 1
#define INPUT_ACTION_REPEAT 2
#define INPUT_ACTION_RELEASE 3
#define INPUT_BTN_OK 1
#define INPUT_BTN_DOWN 2
#define INPUT_BTN_UP 3
#define INPUT_BTN_LEFT 4
#define INPUT_BTN_RIGHT 5
#define INPUT_BTN_BACK 6
typedef struct {int action, button;} input_event_t;
static struct {lv_color_t screen_base, bg_primary, bg_secondary, border_accent,
  border_inactive, text_main, text_secondary;} current_theme;
static void (*handler)(const input_event_t *, void *);
static unsigned opens;
static bool has_art;
static const uint8_t pixels[8] = {0};
static const lv_image_dsc_t art = {.header = {.magic=LV_IMAGE_HEADER_MAGIC,
  .cf=LV_COLOR_FORMAT_RGB565, .w=2, .h=2, .stride=4}, .data_size=8, .data=pixels};
static const lv_image_dsc_t *assets_get(const char *path) {
  assert(!strncmp(path,"/assets/",8)); return has_art ? &art : NULL;
}
static bool sys_time_format(char *out, size_t size, const char *format) {
  (void)format; snprintf(out,size,"Thu, 19 Nov 2026"); return true;
}
static int ui_screen_w(void) {return lv_display_get_horizontal_resolution(NULL);}
static int ui_screen_h(void) {return lv_display_get_vertical_resolution(NULL);}
static void ui_chrome_header(lv_obj_t *root, const char *title, const char *icon) {
  assert(title[0]=='\0' && icon==NULL);
  lv_obj_t *o=lv_label_create(root);lv_label_set_text(o,"12:34");
}
static void ui_chrome_footer(lv_obj_t *root, const char *text) {
  assert(strstr(text,"OK Open") && strstr(text,"Apps") && !strstr(text,"Recent"));
  lv_obj_t *o=lv_label_create(root);lv_label_set_text(o,text);lv_obj_align(o,LV_ALIGN_BOTTOM_MID,0,0);
}
static void ui_chrome_gradient(lv_obj_t *o, lv_color_t a, lv_color_t b, int h) {
  (void)b;(void)h;lv_obj_set_style_bg_color(o,a,0);
}
static void ui_input_set_screen_handler(void (*cb)(const input_event_t *,void *),void *ctx) {
  assert(ctx==NULL);handler=cb;
}
static void clear_slot(lv_event_t *e) {
  lv_obj_t **slot=lv_event_get_user_data(e);if(*slot==lv_event_get_target(e))*slot=NULL;
}
static void ui_screen_load_owned(lv_obj_t **slot,lv_obj_t *root) {
  *slot=root;lv_obj_add_event_cb(root,clear_slot,LV_EVENT_DELETE,slot);lv_screen_load(root);
}
static void ui_switch_screen(int screen) {last_open=screen;opens++;}
""" + f'#include "{UI}/screens/home/home_ui.c"\n' + r"""
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *b) {
  (void)a;(void)b;lv_display_flush_ready(d);
}
static void press(int button, int action) {
  input_event_t event={.button=button,.action=action};handler(&event,NULL);
}
int main(void) {
  lv_init();lv_display_t *display=lv_display_create(240,320);
  _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
  lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,flush);
  for(int wide=0;wide<2;wide++) {
    lv_display_set_resolution(display,wide?320:240,wide?240:320);
    for(int light=0;light<2;light++) {
      current_theme.screen_base=lv_color_hex(light?0xffffff:0x101010);
      current_theme.bg_primary=current_theme.screen_base;
      current_theme.bg_secondary=lv_color_hex(light?0xdddddd:0x333333);
      current_theme.text_main=lv_color_hex(light?0:0xffffff);
      current_theme.text_secondary=current_theme.text_main;
      current_theme.border_accent=lv_color_hex(light?0x005599:0x66bbff);
      current_theme.border_inactive=lv_color_hex(0x888888);
      for(int mode=0;mode<3;mode++) {
        memset(pins,0,sizeof(pins)); has_art=mode!=0;
        if(mode==1)pins[SCREEN_NFC_MENU]=true;
        if(mode==2)for(int i=0;i<SCREEN_COUNT;i++)pins[i]=true;
        ui_home_open();lv_obj_update_layout(s_screen_home);
        assert(s_clock && s_focus==0 && s_shortcut_count==(mode==0?1:mode==1?2:menu_ui_catalog_count()));
        assert(!strcmp(lv_label_get_text(s_name),"Settings"));
        unsigned before=opens;
        press(INPUT_BTN_UP,INPUT_ACTION_PRESS);press(INPUT_BTN_OK,INPUT_ACTION_REPEAT);
        press(INPUT_BTN_OK,INPUT_ACTION_RELEASE);assert(opens==before);
        press(INPUT_BTN_OK,INPUT_ACTION_PRESS);assert(last_open==SCREEN_SETTINGS);
        for(int i=0;i<s_shortcut_count;i++) {
          lv_obj_update_layout(s_screen_home);
          lv_obj_t *check[HOME_VISIBLE_MAX+3]={s_date,s_name,s_shortcuts_title};
          for(int j=0;j<s_visible;j++)check[j+3]=s_tiles[j];
          for(int j=0;j<s_visible+3;j++) {
            if(lv_obj_has_flag(check[j],LV_OBJ_FLAG_HIDDEN))continue;
            lv_area_t area;lv_obj_get_coords(check[j],&area);
            assert(area.x1>=0 && area.x2<ui_screen_w());
            assert(area.y1>=UI_CHROME_HEADER_H && area.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
          }
          assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen_home,0),current_theme.screen_base));
          const char *name;screen_id_t target=shortcut_at(i,&name);
          assert(target==SCREEN_SETTINGS || pins[target]);
          assert(!strcmp(lv_label_get_text(s_name),name));
          press(INPUT_BTN_OK,INPUT_ACTION_PRESS);assert(last_open==target);
          press(INPUT_BTN_RIGHT,INPUT_ACTION_PRESS);
        }
        assert(s_focus==0);
        press(INPUT_BTN_LEFT,INPUT_ACTION_REPEAT);assert(s_focus==s_shortcut_count-1);
        press(INPUT_BTN_DOWN,INPUT_ACTION_PRESS);assert(last_open==SCREEN_MENU);
        ui_home_close();ui_home_close();assert(!s_clock&&!s_date&&!s_name);
        ui_home_open();assert(s_focus==0);
        lv_obj_t *old=s_screen_home;
        lv_screen_load(lv_obj_create(NULL));lv_obj_delete(old);
        assert(s_screen_home==NULL && !s_clock && !s_date && !s_name);
        lv_tick_inc(30001);lv_timer_handler();
      }
    }
  }
  puts("PASS: production Home: Settings default, manual pins/page traversal, no recents, themes, orientations, fallback and teardown");
}
"""
test = base / "home_test.c"
test.write_text(notice + code)
subprocess.run(["cc", "-std=gnu11", "-g", "-fsanitize=undefined", "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", str(test),
                "-I" + str(stubs), "-I" + str(base),
                "-I" + str(UI / "screens/home/include"),
                "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"),
                str(base / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o",
                str(base / "home_test")], check=True)
subprocess.run([str(base / "home_test")], check=True, timeout=15)
