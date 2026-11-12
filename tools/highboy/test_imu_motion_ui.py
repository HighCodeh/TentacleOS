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
import re,subprocess,json
R=Path(__file__).resolve().parents[2]
D=R/'firmware_p4/components/Applications/ui/screens/imu'
manager=(R/'firmware_p4/components/Applications/ui/ui_manager.c').read_text()
assert re.search(r'case SCREEN_IMU_MONITOR:\s*return ui_imu_monitor_stop;',manager)
for service_source in (R/'firmware_p4/components/Service/imu').glob('*.c'):
 assert not re.search(r'#include\s+"(?:ui_|lvgl)',service_source.read_text())
import argparse
parser=argparse.ArgumentParser(description='Actual LVGL IMU screen, simulated sensor fixtures only')
parser.add_argument('--output',type=Path,required=True)
O=parser.parse_args().output.resolve();O.mkdir(parents=True,exist_ok=True)
def write_changed(path,text):
 if not path.exists() or path.read_text()!=text:path.write_text(text)
notice=(D/'imu_monitor_ui.c').read_text().split('#include')[0]
def source(p):return re.sub(r'^\s*#(?:include|pragma)[^\n]*$','',p.read_text(),flags=re.M)
shim=r'''
#include "lvgl.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
static int fail_alloc, alloc_count;
static void *heap_caps_malloc(size_t n,int caps){return ++alloc_count==fail_alloc?NULL:malloc(n);}
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(...) ((void)0)
static const char *esp_err_to_name(int n){return "fixture";}
static int64_t esp_timer_get_time(void){return (int64_t)lv_tick_get()*1000;}
enum {INPUT_ACTION_PRESS};
enum {INPUT_BTN_BACK,INPUT_BTN_OK,INPUT_BTN_UP,INPUT_BTN_RIGHT,SCREEN_SETTINGS};
typedef struct {int action,button;} input_event_t;
static void ui_switch_screen(int s){}
static void ui_input_set_screen_handler(void(*c)(const input_event_t*,void*),void *ctx){}
static void ui_screen_load_owned(lv_obj_t **o,lv_obj_t *s){lv_screen_load(s);}
static int32_t ui_screen_w(void){return lv_display_get_horizontal_resolution(NULL);}
static int32_t ui_screen_h(void){return lv_display_get_vertical_resolution(NULL);}
static bool fail_timer;
static lv_timer_t *test_timer_create(lv_timer_cb_t cb,uint32_t ms,void *ctx){return fail_timer?NULL:lv_timer_create(cb,ms,ctx);}
#define lv_timer_create test_timer_create
static bool held;
static void power_policy_set_display_hold(lv_obj_t *o,bool hold){held=hold;}
static void ui_chrome_header(lv_obj_t *p,const char *t,const char *i){
 lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,t);lv_obj_set_pos(l,8,18);
 lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);lv_obj_set_style_text_color(l,lv_color_white(),0);
}
static void ui_chrome_footer(lv_obj_t *p,const char *t){
 lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,t);lv_obj_set_pos(l,8,ui_screen_h()-19);
 lv_obj_set_style_text_font(l,&lv_font_montserrat_12,0);lv_obj_set_style_text_color(l,lv_color_hex(0x99A9C4),0);
}
'''
stubs=r'''
static imu_motion_snapshot_t fake;
static int stopped,centered,zeroed;
static bool fail_start;
esp_err_t imu_motion_start(void){if(fail_start)return 0x101;fake.running=true;return 0;}
esp_err_t imu_motion_stop(void){fake.running=false;++stopped;return 0;}
void imu_motion_snapshot(imu_motion_snapshot_t *out){*out=fake;}
void imu_motion_request(bool c){if(c)++zeroed;else ++centered;}
void imu_motion_log(void){}
'''
test=r'''
static uint32_t frame[240*320],buffer[240*320];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *pixels){
 uint32_t *p=(uint32_t*)pixels;for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)frame[y*ui_screen_w()+x]=*p++;
 lv_display_flush_ready(d);
}
static void capture(const char *name,bool fresh){
 lv_tick_inc(210);if(fresh)fake.sample_us=esp_timer_get_time();refresh(NULL);
 lv_obj_update_layout(s_screen);lv_refr_now(NULL);
 FILE *f=fopen(name,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",ui_screen_w(),ui_screen_h());
 for(int i=0;i<240*320;++i){uint8_t rgb[]={frame[i]>>16,frame[i]>>8,frame[i]};fwrite(rgb,1,3,f);}fclose(f);
 lv_obj_t *labels[]={s_status,s_values,s_hint};
 for(int i=0;i<3;++i){lv_area_t a;lv_obj_get_coords(labels[i],&a);assert(a.x1>=0&&a.y1>=0&&a.x2<ui_screen_w()&&a.y2<ui_screen_h());}
}
static void press(int b){input_event_t e={0,b};input(&e,NULL);}
int main(void){
 lv_init();lv_tick_inc(10000);lv_display_t *d=lv_display_create(240,320);
 lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
 fake.matrix[0]=fake.matrix[4]=fake.matrix[8]=1;fake.running=fake.calibrated=true;fake.rate_hz=112;
 fake.accel[2]=1;fake.temperature=27.3;
 ui_imu_monitor_open();capture("neutral.ppm",true);assert(s_pixels&&s_depth&&held);
 for(int axis=0;axis<3;++axis){
  memset(fake.matrix,0,sizeof(fake.matrix));float t=.9f;int j=(axis+1)%3,k=(axis+2)%3;
  fake.matrix[axis*3+axis]=1;fake.matrix[j*3+j]=fake.matrix[k*3+k]=cosf(t);
  fake.matrix[j*3+k]=-sinf(t);fake.matrix[k*3+j]=sinf(t);
  char name[32];snprintf(name,sizeof(name),"axis_%d.ppm",axis);capture(name,true);
 }
 fake.matrix[0]=-1;fake.matrix[1]=fake.matrix[2]=fake.matrix[3]=fake.matrix[5]=fake.matrix[6]=fake.matrix[7]=0;fake.matrix[4]=1;fake.matrix[8]=-1;capture("back.ppm",true);
 press(INPUT_BTN_RIGHT);capture("data.ppm",true);assert(s_data_view);
 press(INPUT_BTN_OK);press(INPUT_BTN_UP);assert(centered==1&&zeroed==1);
 fake.zeroing=true;fake.zero_count=80;capture("calibrating.ppm",true);
 fake.sample_us=esp_timer_get_time()-1000000;capture("stale.ppm",false);assert(strstr(lv_label_get_text(s_status),"PAUSED"));
 fake.error=0x105;capture("error.ppm",false);assert(strstr(lv_label_get_text(s_status),"ERROR"));
 ui_imu_monitor_stop();assert(!held&&!s_timer&&!s_running);lv_obj_delete(s_screen);assert(!s_screen);
 fake.error=0;ui_imu_monitor_open();assert(held);ui_imu_monitor_stop();lv_obj_delete(s_screen);
 test_model_path="missing_model.bin";ui_imu_monitor_open();
 assert(!s_timer && strstr(lv_label_get_text(s_status),"MISSING"));lv_obj_delete(s_screen);
 FILE *broken=fopen("broken_model.bin","wb");assert(broken);fputs("HBM1",broken);fclose(broken);
 test_model_path="broken_model.bin";ui_imu_monitor_open();
 assert(!s_timer && strstr(lv_label_get_text(s_status),"INVALID"));lv_obj_delete(s_screen);
 test_model_path="/Users/anarchyysm/Developer/TentacleOS/firmware_p4/assets/models/highboy_imu.bin";
 for(int n=1;n<=3;n++){alloc_count=0;fail_alloc=n;ui_imu_monitor_open();assert(!s_timer&&!fake.running&&!held&&!s_pixels&&!s_depth&&!s_mesh);assert(strstr(lv_label_get_text(s_status),"MEMORY"));lv_obj_delete(s_screen);}fail_alloc=0;
 fail_start=true;ui_imu_monitor_open();assert(!s_timer&&!fake.running&&!held&&!s_pixels&&!s_mesh);assert(strstr(lv_label_get_text(s_status),"START"));lv_obj_delete(s_screen);fail_start=false;
 fail_timer=true;ui_imu_monitor_open();assert(!s_timer&&!fake.running&&!held&&!s_pixels&&!s_mesh);assert(strstr(lv_label_get_text(s_status),"TIMER"));lv_obj_delete(s_screen);fail_timer=false;
 lv_display_set_resolution(d,320,240);ui_imu_monitor_open();capture("landscape.ppm",true);press(INPUT_BTN_RIGHT);capture("landscape_data.ppm",true);ui_imu_monitor_stop();lv_obj_delete(s_screen);
 puts("PASS real LVGL: six model views, raw values, calibration, stale/error, controls and reopen/delete lifecycle");
}
'''
shim += '\nstatic const char *test_model_path = ' + json.dumps(str(R/'firmware_p4/assets/models/highboy_imu.bin')) + ';\n#define IMU_MESH_PATH test_model_path\n'
(O/'test.c').write_text(notice+shim+source(R/'firmware_p4/components/Service/imu/include/imu_motion.h')+source(D/'include/imu_highboy_mesh.h')+stubs+source(D/'imu_monitor_ui.c')+test)
write_changed(O/'lv_conf.h',notice+'''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 32
#define LV_USE_OS 0
#define LV_MEM_SIZE (2 * 1024 * 1024U)
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_USE_LOG 0
#define LV_USE_THORVG_INTERNAL 0
#endif
''')
write_changed(O/'CMakeLists.txt','\n'.join('#'+line[2:] for line in notice.strip().splitlines())+'\n'+'''cmake_minimum_required(VERSION 3.18)
project(imu_ui_test C CXX ASM)
set(LV_BUILD_CONF_PATH "'''+str(O)+'''/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("'''+str(R/'firmware_p4/managed_components/lvgl__lvgl')+'''" lvgl)
add_executable(imu_ui_test test.c)
target_link_libraries(imu_ui_test PRIVATE lvgl m)
''')
subprocess.run(['cmake','-S',str(O),'-B',str(O/'build')],check=True)
subprocess.run(['cmake','--build',str(O/'build'),'-j','4'],check=True)
subprocess.run([str(O/'build/imu_ui_test')],cwd=O,check=True)
from PIL import Image
for p in O.glob('*.ppm'):Image.open(p).save(p.with_suffix('.png'))
