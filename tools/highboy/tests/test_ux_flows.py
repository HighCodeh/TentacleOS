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

"""Production capture naming, storage collision and diagnostics layout.
Run after test_keyboard_ui.py with the same build-directory argument (pinned LVGL library).
"""
from pathlib import Path
import subprocess,sys,json
source=Path(__file__).resolve().parents[3]
NOTICE = '\n'.join(line[2:] if line.startswith('# ') else '' for line in Path(__file__).read_text().splitlines()[:15])
base=Path(sys.argv[1]).resolve();base.mkdir(exist_ok=True,parents=True)
def read(rel):return (source/'firmware_p4/components'/rel).read_text()
def function(text,name):
 start=text.index(name);start=text.rfind('\n',0,start)+1
 brace=text.index('{',start);depth=1;end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
def run(name,code,lvgl=False):
 path=base/(name+'.c');path.write_text(''.join('// '+l+'\n' for l in NOTICE.splitlines())+'\n'+code)
 cmd=['cc','-std=gnu11','-g','-fsanitize=undefined',str(path),'-o',str(base/name)]
 if lvgl:
  cmd+=['-I'+str(base),'-I'+str(source/'firmware_p4/managed_components/lvgl__lvgl'),str(base/'build/lvgl-build/lib/liblvgl.a'),'-lm']
 subprocess.run(cmd,check=True);subprocess.run([str(base/name)],check=True)
common='#include <assert.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdlib.h>\n'
storage=read('Applications/SubGhz/subghz_storage.c');sub=read('Applications/ui/screens/subghz/subghz_read_ui.c');ir=read('Applications/ui/screens/infrared/ir_receive_ui.c')
constants='''
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_INVALID_STATE 3
#define NOTIFY_SAVED 1
#define NOTIFY_WARNING 2
#define UI_FB_WRITE 1
static bool s_saved;static int s_cr,marked,dialogs,writes;
static char editor[64];
static void capture_result_mark_saved(int *p){assert(p==&s_cr);marked++;}
static void capture_result_set_title(int *p,const char *name){assert(p==&s_cr);assert(!strcmp(name,"TV Room"));}
static void ui_feedback(int a){(void)a;}
static void notify(int a,const char*b){(void)a;(void)b;}
static void keyboard_open_filename(const char *t,const char *initial,unsigned m,void(*cb)(const char*,void*)){
 (void)t;(void)m;(void)cb;dialogs++;strcpy(editor,initial);
}
'''
run('subghz_name_test',common+'#include <sys/stat.h>\n#include <unistd.h>\n'+constants+'''
#define PATH_BUF_SIZE 128
#define SUB_EXT ".sub"
static char directory[64];
#define STORAGE_DIR directory
static struct{char save_name[40];}s_result;
'''+function(storage,'static void build_path(')+function(storage,'esp_err_t subghz_storage_rename(')+function(sub,'static void save_name_submit(')+'''
static void file(const char *name,const char *data){char p[128];build_path(p,sizeof(p),name);FILE*f=fopen(p,"wb");assert(f);fputs(data,f);fclose(f);}
static void content(const char *name,const char *data){char p[128],b[32]={0};build_path(p,sizeof(p),name);FILE*f=fopen(p,"rb");assert(f);fread(b,1,sizeof(b)-1,f);fclose(f);assert(!strcmp(b,data));}
int main(void){
 strcpy(directory,"/tmp/highboy-name-test-XXXXXX");assert(mkdtemp(directory));
 file("capture","original");file("occupied","keep");strcpy(s_result.save_name,"capture");
 save_name_submit("occupied",NULL);assert(!s_saved && marked==0 && dialogs==1);content("occupied","keep");content("capture","original");
 save_name_submit("TV Room",NULL);assert(s_saved && marked==1 && !strcmp(s_result.save_name,"TV Room"));content("TV Room","original");
 assert(subghz_storage_rename("capture","x")==ESP_ERR_NOT_FOUND);
 assert(subghz_storage_rename("TV Room","TV Room")==ESP_OK);
 assert(subghz_storage_rename("TV Room","../x")==ESP_ERR_INVALID_ARG);
 s_saved=false;strcpy(s_result.save_name,"missing");save_name_submit("new",NULL);assert(!s_saved && marked==1);
 char p[128];build_path(p,sizeof(p),"TV Room");assert(unlink(p)==0);build_path(p,sizeof(p),"occupied");assert(unlink(p)==0);assert(rmdir(directory)==0);
 puts("PASS: production Sub-GHz save naming, persisted bytes, collision retention and missing-file errors");
}
''')
run('ir_name_test',common+constants+'''
#define IR_PROTO_UNKNOWN 0
#define IR_STORE_NAME_MAX 32
#define IR_MAX_SYMBOLS 32
#define IR_CARRIER_HZ_DEFAULT 38000
static struct{int protocol;}s_captured;
static int s_raw_buf[32];static bool exists,fail;static size_t raw_count=3;
static bool ir_store_exists(const char*n){(void)n;return exists;}
static int ir_store_save_data(const char*n,void*d){(void)d;assert(!strcmp(n,"TV Room"));writes++;return fail?ESP_FAIL:ESP_OK;}
static int ir_get_last_raw(void*b,unsigned n,size_t*c){(void)b;(void)n;*c=raw_count;return ESP_OK;}
static int ir_store_save_raw(const char*n,void*b,size_t c,unsigned f){(void)b;assert(c==3&&f==38000);return ir_store_save_data(n,NULL);}
static void ir_store_name_for_data(void*d,char*b,size_t n){(void)d;snprintf(b,n,"NEC_01");}
static void ir_store_next_free(const char*p,char*b,size_t n){snprintf(b,n,"%s_001",p);}
'''+function(ir,'static void save_name_submit(')+function(ir,'static void save_captured(')+'''
int main(void){
 s_captured.protocol=1;save_captured();assert(dialogs==1&&writes==0&&!strcmp(editor,"NEC_01"));
 exists=true;save_name_submit("TV Room",NULL);assert(!s_saved&&writes==0&&!strcmp(editor,"TV Room"));
 exists=false;fail=true;save_name_submit("TV Room",NULL);assert(!s_saved&&marked==0);
 fail=false;save_name_submit("TV Room",NULL);assert(s_saved&&marked==1);
 s_saved=false;s_captured.protocol=0;save_captured();assert(!strcmp(editor,"raw_001"));
 raw_count=0;save_name_submit("TV Room",NULL);assert(!s_saved&&marked==1);
 raw_count=3;save_name_submit("TV Room",NULL);assert(s_saved&&marked==2);
 puts("PASS: production IR decoded/raw save waits for a chosen name; collision and I/O errors keep capture unsaved");
}
''')
diag=read('Applications/ui/screens/dev/dev_diag_ui.c');diag=diag[diag.index('#define TICK_MS'):]
run('diag_layout_test',common+'''
#include "lvgl.h"
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define UI_COL_SUCCESS 0x00FF00
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define INPUT_ACTION_PRESS 1
#define INPUT_BTN_BACK 5
#define SCREEN_DEV_MENU 120
typedef struct{int action,button;}input_event_t;
typedef struct{bool valid;int soc;unsigned vbat_mv;}battery_snapshot_t;
static struct{lv_color_t bg_secondary,border_inactive,text_secondary,bg_primary,text_main;}current_theme;
static unsigned heap_caps_get_free_size(int a){(void)a;return 80*1024;}
static bool battery_service_get(battery_snapshot_t*b){*b=(battery_snapshot_t){true,-1,4000};return true;}
static bool sys_metrics_die_temp_c(float*c){*c=40;return true;}
static bool spi_bridge_is_alive(void){return true;}
static int ui_screen_w(void){return lv_display_get_horizontal_resolution(NULL);}
static int ui_screen_h(void){return lv_display_get_vertical_resolution(NULL);}
static lv_obj_t *ui_chrome_screen_create(void){return lv_obj_create(NULL);}
static void ui_chrome_header(lv_obj_t*a,const char*b,const char*c){(void)a;(void)b;(void)c;}
static void ui_chrome_footer(lv_obj_t*a,const char*b){(void)a;(void)b;}
static void ui_switch_screen(int a){(void)a;}
static void ui_input_set_screen_handler(void(*a)(const input_event_t*,void*),void*b){(void)a;(void)b;}
static void ui_screen_load_owned(lv_obj_t**slot,lv_obj_t*a){*slot=a;lv_screen_load(a);}
'''+diag+'''
static unsigned timers(void){unsigned n=0;for(lv_timer_t*t=lv_timer_get_next(NULL);t;t=lv_timer_get_next(t))n++;return n;}
static void bounds(lv_obj_t*o){
 lv_area_t a;lv_obj_get_coords(o,&a);
 assert(a.x1>=0&&a.x2<ui_screen_w());assert(a.y1>=42&&a.y2<ui_screen_h()-22);
 for(unsigned i=0;i<lv_obj_get_child_count(o);i++)bounds(lv_obj_get_child(o,i));
}
int main(void){
 lv_init();lv_display_t*d=lv_display_create(240,320);unsigned before=timers();
 for(int mode=0;mode<2;mode++){
  lv_display_set_resolution(d,mode?320:240,mode?240:320);
  ui_dev_diag_open();tick_cb(s_tick);lv_obj_update_layout(s_screen);bounds(lv_obj_get_child(s_screen,0));
  assert(timers()==before+1);lv_obj_t*old=s_screen;
  ui_dev_diag_open();assert(timers()==before+1);lv_obj_delete(old);assert(timers()==before+1);
  ui_dev_diag_close();ui_dev_diag_close();assert(timers()==before);lv_obj_delete(s_screen);s_screen=NULL;
 }
 puts("PASS: real LVGL diagnostics bounds in both orientations; timer close/re-entry and stale deletion");
}
''',True)
