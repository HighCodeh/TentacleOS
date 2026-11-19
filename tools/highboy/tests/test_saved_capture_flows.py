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

"""Saved NFC identity, IR file round trips and capture UI with native LVGL.
Run after test_keyboard_ui.py with the same host-build directory.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
IR = ROOT / "firmware_p4/components/Service/ir"
STORAGE = ROOT / "firmware_p4/components/Service/storage_api"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "saved-capture"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "screens/infrared/ir_store.c").read_text().split("#include", 1)[0]


def write(name, text):
    path = WORK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(NOTICE + text)
    return path


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def run(name, code, includes=(), sources=(), lvgl=False):
    path = write(name + ".c", code)
    exe = WORK / name
    command = ["cc", "-std=gnu11", "-g", "-fsanitize=undefined,bounds",
               "-fno-sanitize-recover=all", "-I" + str(WORK)]
    command += ["-I" + str(p) for p in includes]
    command += [str(path)] + [str(p) for p in sources]
    if lvgl:
        command += ["-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(BASE),
                    "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"),
                    str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm"]
    subprocess.run(command + ["-o", str(exe)], check=True, timeout=60)
    subprocess.run([str(exe)], check=True, timeout=30)


write("esp_err.h", """#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_FOUND 4
#define ESP_ERR_INVALID_STATE 5
static inline const char *esp_err_to_name(int e) { (void)e; return "failure"; }
""")
write("esp_log.h", """#pragma once
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
""")
write("esp_heap_caps.h", """#pragma once
#include <stdlib.h>
#include <assert.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int fail_alloc;
static inline void *heap_caps_malloc(size_t n, int caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return fail_alloc ? NULL : malloc(n);
}
static inline void *heap_caps_realloc(void *p, size_t n, int caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return fail_alloc ? NULL : realloc(p, n);
}
""")
write("driver/rmt_types.h", """#pragma once
#include <stdint.h>
typedef union { struct { uint32_t duration0:15, level0:1, duration1:15, level1:1; }; uint32_t val; } rmt_symbol_word_t;
""")
write("pin_def.h", "#pragma once\n")
common = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_err.h"
#include "esp_log.h"
"""
store = (UI / "screens/infrared/ir_store.c").read_text()
store = store[store.index("static bool valid_name("):store.index("// Transmit (blocking;")]
run("ir_store_roundtrip", common + r"""
#include "esp_heap_caps.h"
#include "ir_file.h"
#include "storage_atomic.h"
#define IR_STORE_NAME_MAX IR_FILE_NAME_MAX
#define IR_STORE_PATH_MAX 96
#define IR_SERIALIZE_MAX 16384
#define IR_STORE_FILE_MAX (512 * 1024)
static const char *TAG = "TEST";
static char directory[64];
#define IR_DIR directory
int fail_alloc;
static bool fail_write;
typedef struct {char name[32],path[96],proto[16];} ir_store_entry_t;
static int storage_mkdir_recursive(const char *p) { (void)p; return ESP_OK; }
static int checked_write(const char *p,const void *d,size_t n) {
  return fail_write ? ESP_FAIL : storage_write_atomic(p,d,n);
}
#define storage_write_atomic checked_write
size_t ir_encode(const ir_data_t *d,rmt_symbol_word_t *s,size_t n) { abort(); }
uint32_t ir_carrier_freq(ir_protocol_t p) { abort(); }
esp_err_t ir_send(const ir_data_t *d) { abort(); }
esp_err_t ir_send_raw(const rmt_symbol_word_t *s,size_t n,uint32_t f) { abort(); }
const char *ir_protocol_name(ir_protocol_t p) { return "NEC"; }
""" + store + r"""
static void check(const char *name,bool raw) {
  char path[96];path_for(name,path,sizeof(path));ir_file_t file;ir_file_init(&file);
  assert(ir_store_load(path,&file)==ESP_OK && file.count==1);
  assert(!strcmp(file.signals[0].name,name));assert(file.signals[0].is_raw==raw);
  if(raw) { assert(file.signals[0].raw_count==4);assert(file.signals[0].raw[2]==560); }
  else { assert(file.signals[0].data.address==0x12 && file.signals[0].data.command==0x34); }
  ir_file_free(&file);
}
int main(void) {
  strcpy(directory,"/tmp/highboy-ir-names-XXXXXX");assert(mkdtemp(directory));
  ir_data_t data={.protocol=IR_PROTO_NEC,.address=0x12,.command=0x34};
  rmt_symbol_word_t raw[2]={{.duration0=9000,.level0=1,.duration1=4500},{.duration0=560,.level0=1,.duration1=560}};
  assert(ir_store_save_data("TV Room",&data)==ESP_OK);check("TV Room",false);
  assert(ir_store_save_raw("Air power",raw,2,38000)==ESP_OK);check("Air power",true);
  assert(ir_store_save_data("TV Room",&data)==ESP_ERR_INVALID_STATE);
  ir_store_entry_t list[4];assert(ir_store_list(list,4)==2);
  assert(!strcmp(list[0].name,"Air power") && !strcmp(list[0].proto,"RAW"));
  assert(!strcmp(list[1].name,"TV Room") && !strcmp(list[1].proto,"NEC"));
  assert(ir_store_rename("Air power","TV Room")==ESP_ERR_INVALID_STATE);check("Air power",true);
  fail_write=true;assert(ir_store_rename("Air power","Bedroom")!=ESP_OK);
  assert(!ir_store_exists("Bedroom"));check("Air power",true);fail_write=false;
  assert(ir_store_rename("Air power","Bedroom")==ESP_OK);check("Bedroom",true);
  assert(!ir_store_exists("Air power"));
  assert(ir_store_rename("TV Room","TV Room")==ESP_OK);
  assert(ir_store_save_data("../escape",&data)==ESP_ERR_INVALID_ARG);
  fail_alloc=1;assert(ir_store_save_data("Failed",&data)==ESP_ERR_NO_MEM);
  fail_alloc=0;assert(!ir_store_exists("Failed"));
  ir_file_t remote;ir_file_init(&remote);
  assert(ir_file_add_parsed(&remote,"Power",&data)==ESP_OK);
  assert(ir_file_add_parsed(&remote,"Volume",&data)==ESP_OK);
  assert(write_file(&remote,"Remote")==ESP_OK);ir_file_free(&remote);
  assert(ir_store_rename("Remote","Living room")==ESP_OK);
  char path[96];path_for("Living room",path,sizeof(path));ir_file_init(&remote);
  assert(ir_store_load(path,&remote)==ESP_OK && remote.count==2);
  assert(!strcmp(remote.signals[0].name,"Power") && !strcmp(remote.signals[1].name,"Volume"));
  ir_file_free(&remote);
  assert(ir_store_delete("TV Room")==ESP_OK);assert(ir_store_delete("Bedroom")==ESP_OK);
  assert(ir_store_delete("Living room")==ESP_OK);assert(rmdir(directory)==0);
  puts("PASS: IR parsed/raw chosen names survive save/list/reload/rename; collisions and failed writes retain data; remote buttons preserved");
}
""", [IR / "include", STORAGE / "include"], [IR / "ir_file.c", STORAGE / "storage_atomic.c"])

sim = (UI / "screens/nfc/nfc_sim.c").read_text()
protocols = sorted(set(re.findall(r"HB_PROTO_[A-Z0-9_]+", function(sim, "static hb_nfc_protocol_t type_protocol("))))
identity = "typedef enum {" + ",".join(protocols) + "} hb_nfc_protocol_t;\n"
for signature in ("static hb_nfc_protocol_t type_protocol(", "bool nfc_sim_same_card(", "bool nfc_sim_resolve_name("):
    identity += function(sim, signature)
emu = (UI / "screens/nfc/nfc_emulate_ui.c").read_text()
read_ui = (UI / "screens/nfc/nfc_read_ui.c").read_text()
run("nfc_saved_identity", common + r"""
#include "nfc_sim.h"
#define LV_MIN(a,b) ((a)<(b)?(a):(b))
#define MAX_CARDS 10
#define SCREEN_NFC_MENU 1
#define SCREEN_NFC_SAVED 2
typedef int screen_id_t;
static const char *TAG="TEST";
static nfc_sim_card_t s_cards[NFC_SIM_MAX_SAVED];static int s_count;
static bool s_hw_running,s_has_selection;
static screen_id_t s_requested_return;
static nfc_sim_card_t s_requested_card;
static char directory[64];
#define MFC_DUMP_DIR directory
typedef struct {nfc_sim_card_t card;char path[128];bool is_plus,is_t2t,is_v;} nfc_emu_dump_entry_t;
static nfc_emu_dump_entry_t s_dumps[MAX_CARDS];
static bool nfc_listener_is_active(void){return false;}
int nfc_sim_saved_count(void){return s_count;}
const nfc_sim_card_t *nfc_sim_saved_get(int i){return i>=0 && i<s_count ? &s_cards[i] : NULL;}
#define HB_NFC_OK 0
#define NFC_CARD_ACTION_SAVE 1
#define NFC_SND_SAVE 1
#define NOTIFY_WARNING 1
#define NOTIFY_SAVED 2
static nfc_sim_card_t s_card;
static int s_hw_card,s_cr,writes,save_failure,last_notification;
static char result_title[32];
static int nfc_device_save_generic(const char *name,void *card){writes++;return save_failure;}
static void capture_result_set_title(void *cr,const char *name){strlcpy(result_title,name,sizeof(result_title));}
static void capture_result_mark_action_done(void *cr,int action,const char *name){(void)cr;}
static void nfc_ui_play_sound(int sound){(void)sound;}
static void notify(int kind,const char *message){last_notification=kind;}

static bool inspect_mfc1k_dump(const char *path,nfc_sim_card_t *card){
 FILE *f=fopen(path,"rb");assert(f);bool ok=fread(card,sizeof(*card),1,f)==1;fclose(f);return ok;
}
static bool inspect_plus_dump(nfc_emu_dump_entry_t *e){return false;}
static bool inspect_t2t_dump(nfc_emu_dump_entry_t *e){return false;}
static bool inspect_v_dump(nfc_emu_dump_entry_t *e){return false;}
""" + identity + function(read_ui, "static void on_name_submit(") + function(emu, "void ui_nfc_emulate_select_card(") +
function(emu, "static int scan_real_dumps(").replace('MFC_DUMP_DIR "/%s", ent->d_name', '"%s/%s", MFC_DUMP_DIR, ent->d_name') + r"""
static void fixture(const char *name,const nfc_sim_card_t *card){
 char path[128];snprintf(path,sizeof(path),"%s/%s",directory,name);
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(card,sizeof(*card),1,f)==1);assert(fclose(f)==0);
}
int main(void){
 nfc_sim_card_t a={.name="Office access",.type="MIFARE Classic",.uid={1,2,3,4,5,6,7},.uid_len=7};
 nfc_sim_card_t b=a;b.uid[6]=8;assert(!nfc_sim_same_card(&a,&b));
 b=a;strcpy(b.type,"MIFARE Plus");assert(!nfc_sim_same_card(&a,&b));
 b=a;strcpy(b.type,"MIFARE Classic 1K");assert(nfc_sim_same_card(&a,&b));
 b=a;strcpy(b.type,"Type 2 read-only");strcpy(a.type,"Ultralight / NTAG");assert(nfc_sim_same_card(&a,&b));
 strcpy(a.type,"MIFARE Classic");b=a;
 b.uid_len=11;assert(!nfc_sim_same_card(&a,&b));assert(!nfc_sim_same_card(NULL,&a));
 s_cards[0]=a;s_count=1;b=a;strcpy(b.name,"MFC 01020304");
 assert(nfc_sim_resolve_name(&b));assert(!strcmp(b.name,"Office access"));
 s_cards[1]=a;strcpy(s_cards[1].name,"Second alias");s_count=2;
 strcpy(b.name,"filename");assert(!nfc_sim_resolve_name(&b));assert(!strcmp(b.name,"filename"));
 s_count=1;s_card=a;on_name_submit("Office access",NULL);
 assert(writes==0 && last_notification==NOTIFY_SAVED);
 s_card.uid[6]=8;on_name_submit("Office access",NULL);
 assert(writes==0 && last_notification==NOTIFY_WARNING);
 s_card=a;save_failure=1;on_name_submit("Renamed",NULL);
 assert(writes==1 && !strcmp(s_card.name,"Office access"));
 save_failure=0;on_name_submit("Renamed",NULL);
 assert(writes==2 && !strcmp(s_card.name,"Renamed") && !strcmp(result_title,"Renamed"));
 strcpy(directory,"/tmp/highboy-nfc-names-XXXXXX");assert(mkdtemp(directory));
 fixture("office.nfc",&a);b=a;b.uid[6]=8;fixture("different.nfc",&b);
 b=a;strcpy(b.type,"MIFARE Plus");fixture("plus.nfc",&b);
 ui_nfc_emulate_select_card(&a,SCREEN_NFC_SAVED);memset(&a,0,sizeof(a));
 assert(s_requested_return==SCREEN_NFC_SAVED && scan_real_dumps()==1);
 assert(!strcmp(s_dumps[0].card.name,"Office access"));assert(strstr(s_dumps[0].path,"office.nfc"));
 s_requested_card.uid[6]=9;assert(scan_real_dumps()==0);
 ui_nfc_emulate_select_card(NULL,SCREEN_NFC_MENU);assert(scan_real_dumps()==3);
 for(int i=0;i<3;i++)assert(unlink(s_dumps[i].path)==0);assert(rmdir(directory)==0);
 puts("PASS: NFC alias, full UID/protocol matching, ambiguity fallback, copied selection and no unrelated dump fallback");
}
""", [UI / "screens/nfc/include"])

for header in ("assets_manager.h", "ui_chrome.h", "ui_theme.h", "ui_metrics.h", "st7789.h",
               "ir_store.h", "keyboard_ui.h", "menu_component_ui.h", "msgbox_ui.h", "notify_ui.h",
               "ui_feedback.h", "ui_manager.h"):
    write(header, "#pragma once\n")
write("esp_attr.h", "#define EXT_RAM_BSS_ATTR\n")
ui_stubs = r"""
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define INPUT_ACTION_PRESS 1
#define INPUT_ACTION_REPEAT 2
#define INPUT_BTN_DOWN 1
#define INPUT_BTN_UP 2
#define INPUT_BTN_OK 3
#define INPUT_BTN_BACK 4
#define SCREEN_IR_MENU 1
#define UI_FB_WRITE 1
#define UI_FB_NAV 2
#define UI_FB_SELECT 3
#define UI_FB_EMULATE 4
#define NOTIFY_WARNING 1
#define NOTIFY_INFO 2
#define IR_STORE_MAX_ENTRIES 64
#define IR_STORE_NAME_MAX 32
#define IR_PROTO_UNKNOWN 0
typedef struct {int action,button;} input_event_t;
typedef struct {char name[32],path[96],proto[16];} ir_store_entry_t;
typedef struct {bool is_raw;struct{int protocol;uint32_t address,command;}data;} ir_signal_t;
typedef struct {int count;ir_signal_t *signals;} ir_file_t;
typedef struct {lv_obj_t *root;} menu_component_t;
static struct {lv_color_t screen_base,bg_primary,bg_secondary,border_accent,border_inactive,text_main,text_secondary;} current_theme;
static int last_notification,backs;
static bool io_failure;
static ir_store_entry_t entries[2]={
  {.name="Air conditioner power",.path="air.ir",.proto="RAW"},
  {.name="TV Room",.path="tv.ir",.proto="NEC"}
};
static int entry_count=2;
static int ir_store_list(ir_store_entry_t *out,int max){assert(max>=entry_count);memcpy(out,entries,sizeof(entries));return entry_count;}
static bool ir_store_exists(const char *name){for(int i=0;i<entry_count;i++)if(!strcmp(name,entries[i].name))return true;return false;}
static int ir_store_rename(const char *old,const char *name){
 if(io_failure)return ESP_FAIL;
 for(int i=0;i<entry_count;i++)if(!strcmp(old,entries[i].name)){
  snprintf(entries[i].name,sizeof(entries[i].name),"%s",name);
  ir_store_entry_t temp=entries[0];entries[0]=entries[1];entries[1]=temp;return ESP_OK;
 }
 return ESP_ERR_NOT_FOUND;
}
static int ir_store_delete(const char *name){if(io_failure)return ESP_FAIL;entry_count=0;return ESP_OK;}
static void ir_file_init(ir_file_t *f){memset(f,0,sizeof(*f));}
static void ir_file_free(ir_file_t *f){(void)f;}
static int ir_store_load(const char *path,ir_file_t *f){
 static ir_signal_t signal={.is_raw=true};f->count=1;f->signals=&signal;return io_failure?ESP_FAIL:ESP_OK;
}
static int ir_store_send_signal(ir_signal_t *s){return io_failure?ESP_FAIL:ESP_OK;}
static lv_image_dsc_t *assets_get(const char *name){return NULL;}
static int ui_screen_w(void){return lv_display_get_horizontal_resolution(NULL);}
static int ui_screen_h(void){return lv_display_get_vertical_resolution(NULL);}
static void ui_feedback(int event){(void)event;}
static void notify(int kind,const char *message){last_notification=kind;}
static void keyboard_open_filename(const char *title,const char *name,int max,void(*cb)(const char*,void*)){(void)cb;}
static void msgbox_open(const char *icon,const char *text,const char *yes,const char *no,void(*cb)(bool)){(void)cb;}
static void ui_async_call(void(*cb)(void*),void *ctx){cb(ctx);}
static lv_obj_t *ui_chrome_screen_create(void){return lv_obj_create(NULL);}
static void ui_chrome_header(lv_obj_t *root,const char *text,const char *icon){(void)root;}
static void ui_chrome_footer(lv_obj_t *root,const char *text){(void)root;}
static void ui_screen_load_owned(lv_obj_t **slot,lv_obj_t *root){*slot=root;lv_screen_load(root);}
static void ui_input_set_screen_handler(void(*cb)(const input_event_t*,void*),void *ctx){(void)cb;}
static void ui_switch_screen(int screen){assert(screen==SCREEN_IR_MENU);backs++;}
static menu_component_t menu_component_create(lv_obj_t *root,const char *title,const char *icon){return (menu_component_t){root};}
static void menu_component_add_item(menu_component_t *m,const char *icon,const char *text){(void)m;}
static void menu_component_set_hint(menu_component_t *m,const char *hint){(void)m;}
"""
run("saved_ui_layout", common + ui_stubs + f'''
#define TAG capture_tag
#include "{UI}/components/capture_result/capture_result_ui.c"
#undef TAG
#define TAG saved_tag
#include "{UI}/screens/infrared/ir_saved_ui.c"
#undef TAG
''' + r"""
static void settle(lv_obj_t *root){lv_obj_update_layout(root);lv_tick_inc(500);lv_timer_handler();lv_obj_update_layout(root);}
static void in_body(lv_obj_t *object){
 lv_area_t area;lv_obj_get_coords(object,&area);
 assert(area.x1>=0 && area.x2<ui_screen_w());
 assert(area.y1>=UI_CHROME_HEADER_H && area.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *buffer){lv_display_flush_ready(display);}
int main(void){
 lv_init();lv_display_t *display=lv_display_create(240,320);
 _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
 lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display,flush);
 for(int wide=0;wide<2;wide++)for(int light=0;light<2;light++){
  lv_display_set_resolution(display,wide?320:240,wide?240:320);
  current_theme.screen_base=lv_color_hex(light?0xffffff:0x101010);
  current_theme.bg_primary=current_theme.screen_base;
  current_theme.bg_secondary=lv_color_hex(light?0xeeeeee:0x303030);
  current_theme.border_accent=lv_color_hex(light?0x303090:0xaaaaff);
  current_theme.text_main=lv_color_hex(light?0x101010:0xffffff);
  current_theme.text_secondary=lv_color_hex(light?0x404040:0xcccccc);
  ui_ir_saved_open();settle(s_screen);
  assert(s_level==LEVEL_FILES && s_file_count==2);
  assert(!strcmp(lv_label_get_text(s_file_names[0]),entries[0].name));in_body(s_file_rows[0]);
  input_event_t event={INPUT_ACTION_PRESS,INPUT_BTN_OK};ir_saved_input(&event,NULL);settle(s_screen);
  assert(s_cr.count==3 && !strcmp(lv_label_get_text(s_cr.title),entries[0].name));in_body(s_cr.root);
  assert(lv_color_eq(lv_obj_get_style_text_color(s_cr.title,0),current_theme.text_main));
  capture_result_set_title(&s_cr,"A long saved name with 31 bytes");
  assert(!strcmp(lv_label_get_text(s_cr.title),"A long saved name with 31 bytes"));
  capture_result_next(&s_cr);capture_result_next(&s_cr);settle(s_screen);in_body(s_cr.rows[s_cr.sel]);
  capture_result_destroy(&s_cr);capture_result_destroy(&s_cr);
  capture_result_set_title(&s_cr,"After close");assert(s_cr.root==NULL && s_cr.title==NULL);
  event.button=INPUT_BTN_BACK;ir_saved_input(&event,NULL);assert(s_level==LEVEL_FILES);
  ir_saved_input(&event,NULL);
 }
 assert(backs==4);
 ui_ir_saved_open();s_file=0;s_level=LEVEL_ACTIONS;build_screen();
 on_rename_submit("Bedroom power",NULL);
 assert(s_file==1 && !strcmp(s_files[s_file].name,"Bedroom power"));
 assert(!strcmp(lv_label_get_text(s_cr.title),"Bedroom power"));
 io_failure=true;send_selected();assert(last_notification==NOTIFY_WARNING);
 on_delete_confirm(true);assert(last_notification==NOTIFY_WARNING && entry_count==2);
 io_failure=false;send_selected();assert(last_notification==NOTIFY_INFO);
 on_delete_confirm(true);assert(entry_count==0 && s_level==LEVEL_FILES);
 puts("PASS: native LVGL named IR list/actions, rename selection, truthful send/delete, 240x320/320x240 bounds and themes");
}
""", [UI / "components/capture_result/include", UI / "screens/infrared/include"], lvgl=True)

# Exercise file actions with the production layout and native LVGL input widgets.
lab = (UI / "screens/nfc/nfc_protocol_lab_ui.c").read_text()
service_header = (ROOT / "firmware_p4/components/Applications/nfc/include/nfc_protocol_service.h").read_text()
service_types = service_header[service_header.index("typedef enum {"):service_header.index("/* DESFire:")]
lab_globals = lab[lab.index("static screen_id_t s_return"):lab.index("static void clear_result(")]
lab_functions = "\n".join(function(lab, signature) for signature in (
    "static void clear_result(", "static void select_archive(bool is_previous) {",
    "static void archive_render(", "static void archive_actions_close(",
    "static void archive_start(nfc_protocol_op_t operation) {", "static void archive_rename_submit(",
    "static void archive_delete_confirm(", "static void archive_actions_open(",
    "static void archive_input(", "void ui_nfc_protocol_lab_open(", "void ui_nfc_archive_open("))
nfc_stubs = ui_stubs.replace(
    'static void ui_chrome_footer(lv_obj_t *root,const char *text){(void)root;}',
    'static lv_obj_t *ui_chrome_footer(lv_obj_t *root,const char *text){return lv_label_create(root);}')
nfc_stubs = nfc_stubs.replace(
    'static void ui_switch_screen(int screen){assert(screen==SCREEN_IR_MENU);backs++;}',
    'static void ui_switch_screen(int screen){assert(screen==SCREEN_NFC_MENU);backs++;}')
run("nfc_file_actions", common + r'''
#define SCREEN_NFC_MENU 2
#define SCREEN_NFC_READ 3
#define SCREEN_NFC_SAVED 4
#define SCREEN_HOME 5
#define SCREEN_MENU 6
#define INPUT_BTN_LEFT 5
#define INPUT_BTN_RIGHT 6
#define NFC_STORE_NAME_MAX 32
#define NFC_ARCHIVE_PATH_SIZE 192
#define NFC_ARCHIVE_DIRECTORY "/tmp/highboy-nfc-ui-actions"
#define NFC_PROTOCOL_RESULT_LINES 7
#define NFC_PROTOCOL_RESULT_LINE_LEN 64
#define REFRESH_MS 100
#define HB_NFC_OK 0
typedef int screen_id_t;
typedef int hb_nfc_err_t;
''' + nfc_stubs + service_types + r'''
static nfc_protocol_status_t worker;
static int starts, cancels, info_signals, warning_signals;
static char staged_file[NFC_ARCHIVE_PATH_SIZE], staged_name[NFC_STORE_NAME_MAX];
static int ui_current_screen(void){return SCREEN_NFC_MENU;}
static void ui_chrome_footer_set_text(lv_obj_t *footer,const char *text){lv_label_set_text(footer,text);}
static void nfc_protocol_service_get_status(nfc_protocol_status_t *out){*out=worker;}
static int nfc_protocol_service_archive(const char *name){strlcpy(staged_file,name,sizeof(staged_file));return ESP_OK;}
static int nfc_protocol_service_archive_name(const char *name){strlcpy(staged_name,name,sizeof(staged_name));return ESP_OK;}
static int nfc_protocol_service_start(nfc_protocol_op_t op){
 if(io_failure)return ESP_FAIL;
 starts++;worker=(nfc_protocol_status_t){.running=true,.operation=op,.state=NFC_PROTOCOL_STATE_RUNNING};return ESP_OK;
}
static void nfc_protocol_service_cancel(void){cancels++;}
static void led_signal_info(void){info_signals++;}
static void led_signal_warning(void){warning_signals++;}
static void led_signal_error(void){warning_signals++;}
static void start_read(void){assert(!"Archive must not start RF");}
static void timer_cb(lv_timer_t *timer){(void)timer;}
static void input_cb(const input_event_t *event,void *context){(void)event;(void)context;}
''' + f'''
#define TAG capture_tag
#include "{UI}/components/capture_result/capture_result_ui.c"
#undef TAG
''' + lab_globals + lab_functions + r'''
static void settle(void){lv_obj_update_layout(s_screen);lv_tick_inc(500);lv_timer_handler();lv_obj_update_layout(s_screen);}
static void visible(lv_obj_t *object){
 lv_area_t area;lv_obj_get_coords(object,&area);
 assert(area.x1>=0 && area.x2<ui_screen_w());
 assert(area.y1>=UI_CHROME_HEADER_H && area.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *buffer){lv_display_flush_ready(display);}
static void press(int button){input_event_t event={INPUT_ACTION_PRESS,button};archive_input(&event);settle();}
static void finish(bool success){worker.running=false;worker.state=success?NFC_PROTOCOL_STATE_DONE:NFC_PROTOCOL_STATE_ERROR;archive_render(&worker);settle();}
int main(void){
 assert(mkdir(NFC_ARCHIVE_DIRECTORY,0700)==0);
 FILE *file=fopen(NFC_ARCHIVE_DIRECTORY "/Office access.nfc","wb");assert(file && fclose(file)==0);
 lv_init();lv_display_t *display=lv_display_create(240,320);
 _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
 lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
 lv_display_set_flush_cb(display,flush);
 for(int wide=0;wide<2;wide++)for(int light=0;light<2;light++){
  lv_display_set_resolution(display,wide?320:240,wide?240:320);
  current_theme.screen_base=lv_color_hex(light?0xffffff:0x101010);
  current_theme.bg_primary=current_theme.screen_base;
  current_theme.bg_secondary=lv_color_hex(light?0xeeeeee:0x303030);
  current_theme.border_accent=lv_color_hex(light?0x303090:0xaaaaff);
  current_theme.text_main=lv_color_hex(light?0x101010:0xffffff);
  current_theme.text_secondary=lv_color_hex(light?0x404040:0xcccccc);
  ui_nfc_archive_open();settle();
  assert(!s_archive_is_working && !strcmp(s_archive_selected,"Office access.nfc"));
  press(INPUT_BTN_OK);assert(s_archive_has_actions && s_archive_actions.count==4);
  assert(!strcmp(lv_label_get_text(s_archive_actions.title),"Office access.nfc"));
  assert(lv_color_eq(lv_obj_get_style_text_color(s_archive_actions.title,0),current_theme.text_main));
  visible(s_archive_actions.root);
  press(INPUT_BTN_DOWN);press(INPUT_BTN_DOWN);press(INPUT_BTN_DOWN);visible(s_archive_actions.rows[3]);
  int before=starts;archive_delete_confirm(false);assert(starts==before);
  io_failure=true;archive_delete_confirm(true);assert(starts==before && s_archive_has_actions && !s_archive_is_working);io_failure=false;
  archive_delete_confirm(true);assert(s_archive_is_working && !s_archive_has_actions && worker.operation==NFC_PROTOCOL_OP_ARCHIVE_DELETE);
  press(INPUT_BTN_BACK);assert(s_archive_exit && cancels>0);before=backs;finish(false);assert(backs==before+1);
  assert(strstr(lv_label_get_text(s_status),"Not completed"));s_archive_exit=false;
  press(INPUT_BTN_OK);archive_rename_submit("Bedroom card",NULL);
  assert(s_archive_is_working && worker.operation==NFC_PROTOCOL_OP_ARCHIVE_RENAME && !strcmp(staged_name,"Bedroom card"));
  finish(true);assert(!strcmp(s_archive_selected,"Bedroom card.nfc"));
  press(INPUT_BTN_OK);press(INPUT_BTN_DOWN);press(INPUT_BTN_DOWN);press(INPUT_BTN_OK);
  assert(worker.operation==NFC_PROTOCOL_OP_ARCHIVE_EXPORT);finish(true);
  lv_timer_delete(s_timer);s_timer=NULL;
  archive_actions_open();assert(!s_archive_has_actions);
  lv_obj_del(s_screen);s_screen=NULL;
 }
 assert(info_signals==8 && warning_signals==8);
 assert(unlink(NFC_ARCHIVE_DIRECTORY "/Office access.nfc")==0 && rmdir(NFC_ARCHIVE_DIRECTORY)==0);
 puts("PASS: native NFC file actions, loss/error feedback, rename selection, copy, confirmed delete, busy BACK and both orientations/themes");
}
''', [UI / "components/capture_result/include"], lvgl=True)

saved = (UI / "screens/nfc/nfc_saved_ui.c").read_text()
saved_delete_functions = "\n".join(function(saved, signature) for signature in (
    "static void delete_tick(", "static void on_del_confirm(", "void ui_nfc_saved_stop("))
run("nfc_saved_delete_lifecycle", common + r'''
#define SCREEN_NFC_MENU 2
#define DELETE_REFRESH_MS 100
#define NFC_PROTOCOL_RESULT_LINES 7
#define NFC_PROTOCOL_RESULT_LINE_LEN 64
typedef int hb_nfc_err_t;
''' + nfc_stubs + service_types + r'''
static const char *TAG="NFC_SAVED_TEST";
static lv_timer_t *s_delete_timer;
static lv_obj_t *s_ov_footer;
static bool s_is_deleting,s_exit_after_delete,fail_timer,fail_stage;
static int s_sel_idx,starts,stops,rebuilds,closed;
static struct {uint8_t uid[10];size_t uid_len;} s_view_card={.uid={1,2,3,4},.uid_len=4};
static nfc_protocol_status_t worker;
static void ui_chrome_footer_set_text(lv_obj_t *footer,const char *text){lv_label_set_text(footer,text);}
static void nfc_protocol_service_get_status(nfc_protocol_status_t *out){*out=worker;}
static int nfc_protocol_service_delete_saved(int index,const uint8_t *uid,size_t size){assert(index==0 && size==4 && uid[0]==1);return fail_stage?ESP_FAIL:ESP_OK;}
static int nfc_protocol_service_start(nfc_protocol_op_t op){
 assert(op==NFC_PROTOCOL_OP_CARD_DELETE);if(io_failure)return ESP_FAIL;
 starts++;worker=(nfc_protocol_status_t){.running=true,.operation=op,.state=NFC_PROTOCOL_STATE_RUNNING};return ESP_OK;
}
static void nfc_protocol_service_stop(void){stops++;worker.running=false;}
static void led_signal_info(void){}
static void led_signal_warning(void){}
static void led_signal_error(void){}
static void overlay_close(void){closed++;}
static void rebuild_async(void *context){(void)context;rebuilds++;}
static lv_timer_t *create_timer(lv_timer_cb_t callback,uint32_t period,void *data){return fail_timer?NULL:lv_timer_create(callback,period,data);}
#define lv_timer_create create_timer
''' + saved_delete_functions + r'''
int main(void){
 lv_init();lv_display_create(240,320);lv_obj_t *root=lv_obj_create(NULL);s_ov_footer=lv_label_create(root);
 on_del_confirm(false);assert(!s_is_deleting && starts==0);
 fail_timer=true;on_del_confirm(true);assert(!s_delete_timer && starts==0);fail_timer=false;
 fail_stage=true;on_del_confirm(true);assert(!s_delete_timer && starts==0);fail_stage=false;
 io_failure=true;on_del_confirm(true);assert(!s_delete_timer && starts==0);io_failure=false;
 on_del_confirm(true);assert(s_is_deleting && s_delete_timer && starts==1);
 on_del_confirm(true);assert(starts==1);delete_tick(s_delete_timer);assert(s_is_deleting);
 worker.running=false;worker.state=NFC_PROTOCOL_STATE_DONE;delete_tick(s_delete_timer);
 assert(!s_delete_timer && !s_is_deleting && closed==1 && rebuilds==1 && last_notification==NOTIFY_INFO);
 ui_nfc_saved_stop();assert(stops==0);
 on_del_confirm(true);worker.running=false;worker.state=NFC_PROTOCOL_STATE_ERROR;delete_tick(s_delete_timer);
 assert(!s_delete_timer && !s_is_deleting && rebuilds==1 && last_notification==NOTIFY_WARNING);
 on_del_confirm(true);s_exit_after_delete=true;worker.running=false;worker.state=NFC_PROTOCOL_STATE_DONE;delete_tick(s_delete_timer);
 assert(backs==1 && rebuilds==1 && !s_is_deleting);
 on_del_confirm(true);ui_nfc_saved_stop();ui_nfc_saved_stop();assert(stops==1 && !s_delete_timer && !s_is_deleting);
 puts("PASS: saved NFC delete timer/start failures, completion, error, deferred exit and idempotent owner teardown");
}
''', lvgl=True)
