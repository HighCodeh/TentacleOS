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
import subprocess,sys
from PIL import Image
asset=Path(__file__).resolve().parents[3]/'firmware_p4/assets/animations/nfc_scan.gif'
gif=Image.open(asset);assert gif.size==(160,148) and gif.n_frames==36
alpha_hashes=[];duration=0
for index in range(gif.n_frames):
 gif.seek(index);duration+=gif.info['duration'];value=1469598103934665603
 for byte in gif.convert('RGBA').getchannel('A').tobytes():value=((value^byte)*1099511628211)&((1<<64)-1)
 alpha_hashes.append(value)
assert duration==3000
root=Path(__file__).resolve().parents[3];ui=root/'firmware_p4/components/Applications/ui'
NOTICE=(ui/'screens/home/home_ui.c').read_text().split('#include',1)[0]
base=Path(sys.argv[1]).resolve();stubs=base/'nfc-stubs';(stubs/'freertos').mkdir(parents=True,exist_ok=True)
def write(name,text):(stubs/name).write_text(NOTICE+'#pragma once\n'+text)
write('esp_heap_caps.h','#include <stdlib.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n#define MALLOC_CAP_INTERNAL 4\n#define heap_caps_malloc(n,c) malloc(n)\n#define heap_caps_free free')
write('esp_log.h','#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)')
write('freertos/FreeRTOS.h','#define pdPASS 1')
write('freertos/task.h','''static void vTaskDeleteWithCaps(void *task){(void)task;}
static int xTaskCreatePinnedToCoreWithCaps(void(*fn)(void*),const char*n,int size,void*arg,int prio,void*out,int core,int caps){(void)n;(void)size;(void)prio;(void)out;(void)core;assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));fn(arg);return 1;}
''')
write('sys_prio.h','#define SYS_PRIO_SERVICE_LO 4\n#define SYS_CORE_UI 1')
write('ui_metrics.h','static int ui_screen_w(void){return lv_display_get_horizontal_resolution(NULL);}\nstatic int ui_screen_h(void){return lv_display_get_vertical_resolution(NULL);}')
write('ui_chrome.h','#define UI_CHROME_HEADER_H 42\n#define UI_CHROME_FOOTER_H 22\nstatic void ui_chrome_footer_set_text(lv_obj_t *label,const char *text){lv_label_set_text(label,text);}')
s=(ui/'screens/nfc/nfc_read_ui.c').read_text()
def fn(signature):
 start=s.index(signature);brace=s.index('{',start);end=brace+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
# Compile the exact discovery/success/info branches, without the unrelated recovery worker.
start=s.index('  if (s_state == ST_SCAN) {',s.index('static void refresh_cb'))
end=s.index('  } else if (s_state == ST_BLOCK_READING',start)
flow='static void poll(void) {\n'+s[start:end]+'  }\n}\n'
alpha_table='static const uint64_t expected_alpha[]={'+','.join(str(v)+'ULL' for v in alpha_hashes)+'};\n'
code='''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "lvgl.h"
static FILE *asset_file(const char *path,const char *mode){assert(!strcmp(path,"/assets/animations/nfc_scan.gif"));return fopen(ASSET,mode);}
#define fopen asset_file
'''+alpha_table+f'#include "{ui}/screens/nfc/nfc_guide_ui.c"\n'+'''
#undef fopen
#define UI_COL_SUCCESS 0x00E676
#define UI_COL_BLACK 0
#define SUCCESS_MS 700
#define REVEAL_MS 1400
#define SCAN_TIMEOUT_MS 15000
#define UI_CONTENT_FADE_MS 120
#define NFC_SND_FOUND 1
static struct{lv_color_t text_secondary;}current_theme;
static lv_obj_t *s_screen,*s_status,*s_hint,*s_card_panel,*s_dump,*s_success;
static int s_field,s_card,prepared,sounds,stopped;
static uint32_t s_scan_start,s_found_at;
static int s_state;
enum{ST_SCAN,ST_SUCCESS,ST_FOUND,ST_OPTIONS,ST_ERROR};
typedef struct{int uid;}hb_nfc_card_data_t;
static bool pending;
static void show_rings(bool show){(void)show;}
static void prepare_card_view(const hb_nfc_card_data_t *card){assert(card->uid==123);s_card=card->uid;prepared++;}
static void nfc_ui_play_sound(int sound){assert(sound==1);sounds++;}
static void stop_scan(void){nfc_guide_close();stopped++;}
static lv_obj_t *nfc_ui_card_panel(lv_obj_t *parent,int *card){assert(*card==123);return lv_obj_create(parent);}
static void build_dump(void){s_dump=lv_label_create(s_screen);lv_label_set_text(s_dump,"UID 123");}
static void show_options(void){s_state=ST_OPTIONS;}
static bool take_pending_card(hb_nfc_card_data_t *card){if(!pending)return false;pending=false;card->uid=123;return true;}
static void nfc_ui_field_tick(int *field,uint32_t elapsed){(void)field;(void)elapsed;}
'''+fn('static void show_card_info(void) {')+'\n'+fn('static void reveal(const hb_nfc_card_data_t *card) {')+'\n'+fn('static void show_no_tag(void) {')+'\n'+flow+r'''
static void setup(void){s_screen=lv_screen_active();s_status=lv_label_create(s_screen);s_hint=lv_label_create(s_screen);s_state=ST_SCAN;s_scan_start=lv_tick_get();}
int main(void){
 lv_init();lv_display_t *display=lv_display_create(240,320);setup();nfc_guide_prepare();assert(atomic_load(&s_load_state)==2);
 assert(nfc_guide_show(s_screen));lv_obj_update_layout(s_screen);
 assert(lv_obj_get_y(s_image)>=42 && lv_obj_get_y(s_image)+GUIDE_HEIGHT<=298);
 const lv_image_dsc_t *frame=lv_image_get_src(s_image);assert(frame && frame->header.cf==LV_COLOR_FORMAT_ARGB8888 && frame->data_size==GUIDE_WIDTH*GUIDE_HEIGHT*4);
 unsigned differences=0;uint8_t prior[160*148*4];memcpy(prior,frame->data,sizeof(prior));
 for(int i=0;i<420;i++){
  lv_tick_inc(17);lv_timer_handler();
  uint64_t alpha=1469598103934665603ULL;
  for(size_t pixel=0;pixel<160*148;pixel++)alpha=(alpha^frame->data[pixel*4+3])*1099511628211ULL;
  bool matched=false;for(size_t j=0;j<36;j++)if(alpha==expected_alpha[j])matched=true;
  assert(matched);assert(frame->data[3]==0 && frame->data[(160*148-1)*4+3]==0);
  if(memcmp(prior,frame->data,sizeof(prior))){differences++;memcpy(prior,frame->data,sizeof(prior));}
 }
 assert(differences>=65 && s_state==ST_SCAN && sounds==0);
 poll();assert(s_state==ST_SCAN && prepared==0);
 pending=true;poll();assert(s_state==ST_SUCCESS && s_image==NULL && s_success!=NULL && sounds==1 && prepared==1);
 lv_tick_inc(699);poll();assert(s_state==ST_SUCCESS);
 lv_tick_inc(1);poll();assert(s_state==ST_FOUND && s_success==NULL && s_card_panel!=NULL && s_dump!=NULL);
 lv_tick_inc(1399);poll();assert(s_state==ST_FOUND);lv_tick_inc(1);poll();assert(s_state==ST_OPTIONS && sounds==1);
 s_state=ST_SCAN;s_scan_start=lv_tick_get();assert(nfc_guide_show(s_screen));lv_tick_inc(15000);poll();assert(s_state==ST_ERROR && s_image==NULL && sounds==1);
 lv_display_set_resolution(display,320,240);assert(nfc_guide_show(s_screen));lv_obj_update_layout(s_screen);assert(lv_obj_get_y(s_image)>=42);nfc_guide_close();nfc_guide_close();
 assert(nfc_guide_show(s_screen));lv_obj_t *old=s_screen;lv_screen_load(lv_obj_create(NULL));lv_obj_delete(old);assert(s_image==NULL);
 puts("PASS: real LVGL ARGB GIF decode/loop, alpha masks match all reference frames without trails, 12 fps cadence, portrait and landscape placement, success only on card event, 700 ms success then info/options, timeout and repeated/delete cleanup");
}
'''
p=base/'nfc-guide-test.c';p.write_text(NOTICE+code)
cmd=['cc','-std=gnu11','-g','-fsanitize=undefined',str(p),'-I'+str(stubs),'-I'+str(base),'-I'+str(ui/'screens/nfc/include'),'-I'+str(root/'firmware_p4/managed_components/lvgl__lvgl'),str(base/'build/lvgl-build/lib/liblvgl.a'),'-DASSET="'+str(root/'firmware_p4/assets/animations/nfc_scan.gif')+'"','-lm','-o',str(base/'nfc-guide-test')]
subprocess.run(cmd,check=True);subprocess.run([str(base/'nfc-guide-test')],check=True)
