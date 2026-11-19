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

from pathlib import Path
import sys, subprocess, tempfile
source=Path(__file__).resolve().parents[3]
notice_source=(source/'firmware_p4/components/Applications/ui/components/keyboard/keyboard_ui.c').read_text().split('#include',1)[0]
NOTICE='\n'.join(line.removeprefix('// ').removeprefix('//') for line in notice_source.splitlines())
base=Path(sys.argv[1]) if len(sys.argv)>1 else Path(tempfile.mkdtemp(prefix='highboy-keyboard-'))
base.mkdir(exist_ok=True,parents=True)
def write(name,text):
 p=base/name;p.parent.mkdir(exist_ok=True,parents=True)
 prefix=''.join(('# ' if name.endswith('.txt') else '// ')+l+'\n' for l in NOTICE.splitlines())
 p.write_text(prefix+'\n'+text)
lv=source/'firmware_p4/managed_components/lvgl__lvgl';kb=source/'firmware_p4/components/Applications/ui/components/keyboard'
write('CMakeLists.txt',f'''cmake_minimum_required(VERSION 3.20)
project(keyboard_contract C)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("{lv}" lvgl-build)
add_executable(keyboard_test test.c)
target_include_directories(keyboard_test PRIVATE stubs "{kb}/include")
target_link_libraries(keyboard_test PRIVATE lvgl m)
target_compile_options(keyboard_test PRIVATE -fsanitize=undefined -g)
target_link_options(keyboard_test PRIVATE -fsanitize=undefined)
''')
manager=(source/'firmware_p4/components/Applications/ui/ui_manager.c').read_text()
start=manager.index('static void clear_current_screen(void) {')
end=manager.index('\nbool ui_input_is_locked',start)
clear_function=manager[start:end]
write('lv_conf.h', '''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_USE_OS LV_OS_NONE
#define LV_USE_KEYBOARD 0
#define LV_USE_CALENDAR 0
#define LV_USE_WIN 0
#define LV_USE_LOG 0
#define LV_USE_SNAPSHOT 1
#define LV_USE_GIF 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_MEM_SIZE (4 * 1024 * 1024U)
#endif
''')
write('stubs/esp_heap_caps.h','''#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static bool fail_alloc;
static void *heap_caps_malloc(size_t n, int caps) { assert(caps == 3); return fail_alloc ? NULL : malloc(n); }
#define heap_caps_free free
''')
write('stubs/esp_log.h','#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n')
write('stubs/led_control.h','static void led_signal_warning(void) {}\nstatic void led_signal_error(void) {}\n')
write('stubs/ui_manager.h','static void ui_input_consume_gesture(void) {}\n')
write('stubs/ui_metrics.h','static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }\nstatic int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }\n')
write('stubs/ui_theme.h','static struct { lv_color_t screen_base, bg_secondary, border_accent, text_main, border_interface; } current_theme;\n')
write('test.c',r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "lvgl.h"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
''' + f'#include "{kb}/keyboard_ui.c"\n'+'''
static void ui_input_set_screen_handler(void *a, void *b) {(void)a; (void)b;}
'''+clear_function+r'''
lv_group_t *main_group;
static char received[1024];
static unsigned calls;
static bool reopen;
static void saved(const char *text, void *context) {
  assert(context == &calls);
  assert(!keyboard_is_open());
  strcpy(received, text);
  calls++;
  if (reopen) {
    lv_obj_clean(lv_screen_active());
    keyboard_open_config(&(keyboard_config_t){.title="Second editor"}, NULL, NULL);
  }
}
static void press(const char *key) {
  lv_buttonmatrix_t *matrix=(lv_buttonmatrix_t *)s_keys;
  for(unsigned i=0;i<matrix->btn_cnt;i++)
    if(strcmp(lv_buttonmatrix_get_button_text(s_keys,i),key)==0) {
      lv_buttonmatrix_set_selected_button(s_keys,i);
      lv_obj_send_event(s_keys, LV_EVENT_VALUE_CHANGED, NULL);
      return;
    }
  assert(!"Missing key");
}
static void tick(void) { lv_tick_inc(40); lv_timer_handler(); }
static void layout(void) {
  lv_obj_update_layout(s_screen);
  lv_buttonmatrix_t *matrix=(lv_buttonmatrix_t *)s_keys;
  assert(lv_obj_get_y(s_keys)+lv_obj_get_height(s_keys) <= ui_screen_h()-FOOTER_H);
  assert(lv_obj_get_y(s_text)+lv_obj_get_height(s_text) < KEYS_Y);
  for(unsigned i=0;i<matrix->btn_cnt;i++) {
    assert(lv_area_get_width(&matrix->button_areas[i])>=16);
    assert(lv_area_get_height(&matrix->button_areas[i])>=20);
    assert(lv_buttonmatrix_get_button_text(s_keys,i)[0]);
  }
}
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *b) {(void)a;(void)b;lv_display_flush_ready(d);}
int main(void) {
  lv_init();
  lv_display_t *display=lv_display_create(240,320);
  static uint8_t buffer[320*20*2];
  lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,flush);
  main_group=lv_group_create();
  for(int wide=0;wide<2;wide++) {
    lv_display_set_resolution(display,wide?320:240,wide?240:320);
    keyboard_open_config(&(keyboard_config_t){.title="Name",.max_chars=5,.min_chars=1}, saved,&calls);
    layout(); press("a");press("b");press(LV_SYMBOL_LEFT);press("c");
    assert(strcmp(lv_textarea_get_text(s_text),"acb")==0);
    press(LV_SYMBOL_BACKSPACE);assert(strcmp(lv_textarea_get_text(s_text),"ab")==0);
    press("ABC");layout();press("A");press("123");layout();press("9");press("#+");layout();press("~");
    press("@");assert(strlen(lv_textarea_get_text(s_text))==5);
    assert(strstr(lv_label_get_text(s_count),"limit reached"));
    fail_alloc=true;press(LV_SYMBOL_OK);assert(keyboard_is_open());fail_alloc=false;
    press(LV_SYMBOL_OK);assert(!keyboard_is_open());assert(strcmp(received,"aA9~b")==0);tick();
    keyboard_open_config(&(keyboard_config_t){.title="Hex",.mode=KEYBOARD_HEX,.min_chars=12,.max_chars=12},saved,&calls);
    layout();press("A");press(LV_SYMBOL_OK);assert(keyboard_is_open());
    for(unsigned i=1;i<12;i++)press("F");
    press("0");assert(strlen(lv_textarea_get_text(s_text))==12);
    press(LV_SYMBOL_OK);assert(!keyboard_is_open());tick();
    keyboard_open_config(&(keyboard_config_t){.mode=KEYBOARD_PASSWORD,.initial="secret",.max_chars=64},saved,&calls);
    assert(lv_textarea_get_password_mode(s_text));assert(strcmp(lv_textarea_get_text(s_text),"secret")==0);
    keyboard_close();tick();
    keyboard_open_config(&(keyboard_config_t){.mode=KEYBOARD_HEX,.max_chars=32},saved,&calls);
    press("1");press(LV_SYMBOL_OK);assert(keyboard_is_open());press("2");
    reopen=true;press(LV_SYMBOL_OK);reopen=false;assert(keyboard_is_open());tick();assert(keyboard_is_open());
    keyboard_close();tick();
    unsigned before=calls;
    keyboard_open_config(&(keyboard_config_t){.min_chars=1},saved,&calls);
    press(LV_SYMBOL_OK);assert(keyboard_is_open());keyboard_close();tick();assert(calls==before);
    keyboard_open_config(&(keyboard_config_t){0},saved,&calls);press(LV_SYMBOL_OK);assert(calls==before+1);tick();
  }
  keyboard_open_config(&(keyboard_config_t){0},saved,&calls);
  clear_current_screen();assert(!keyboard_is_open());assert(!lv_group_get_editing(main_group));
  lv_obj_clean(lv_screen_active());tick();
  bool available[128]={0};
  for(unsigned page=0;page<4;page++) {
    assert(strlen(s_pages[page])==KEY_COUNT);
    for(unsigned i=0;i<KEY_COUNT;i++) available[(unsigned char)s_pages[page][i]]=true;
  }
  for(unsigned c=33;c<127;c++) assert(available[c]);
  keyboard_open_config(&(keyboard_config_t){.initial="é",.max_chars=1,.min_chars=1},saved,&calls);
  assert(character_count(lv_textarea_get_text(s_text))==1);
  press(LV_SYMBOL_OK);assert(strcmp(received,"é")==0);tick();
  const char *bad[]={"../x","TV/On","TV\\On","TV:On","x*","x?","x\"","x<","x>","x|",".hidden"," leading","trailing ","trailing.",""};
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
    keyboard_open_config(&(keyboard_config_t){.mode=KEYBOARD_FILENAME,.initial=bad[i],.min_chars=1,.max_chars=31},saved,&calls);
    unsigned before=calls;press(LV_SYMBOL_OK);assert(keyboard_is_open() && calls==before);
    keyboard_close();tick();
  }
  keyboard_open_config(&(keyboard_config_t){.mode=KEYBOARD_FILENAME,.initial="TV Sala_1-ON",.min_chars=1,.max_chars=31},saved,&calls);
  press(LV_SYMBOL_OK);assert(strcmp(received,"TV Sala_1-ON")==0);tick();
  keyboard_open_config(&(keyboard_config_t){.mode=KEYBOARD_FILENAME,.initial="éé",.min_chars=1,.max_chars=2},saved,&calls);
  press(LV_SYMBOL_OK);assert(keyboard_is_open());keyboard_close();tick();
  puts("PASS: filename validation; real LVGL editor layouts, modes, cursor, erase, limits, masking, OOM, cancel, empty/hex validation and reentrant callbacks");
}
''')

subprocess.run(['cmake','-S',str(base),'-B',str(base/'build'),'-DCMAKE_BUILD_TYPE=Debug'],check=True)
subprocess.run(['cmake','--build',str(base/'build'),'-j6'],check=True)
subprocess.run([str(base/'build/keyboard_test')],check=True)
