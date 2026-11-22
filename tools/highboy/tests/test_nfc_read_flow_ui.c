// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.


#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "ui_semantic.h"
#include "nfc_sim.h"
#include "keyboard_ui.h"
#include "capture_result_ui.h"
#define HB_NFC_OK 0
#define HB_NFC_ERR_NOT_FOUND -2
#define HB_NFC_ERR_PROTOCOL -3
#define HB_PROTO_MF_CLASSIC 1
#define NFC_MFC_NDEF_TEXT_MAX 256
#define EXT_RAM_BSS_ATTR
#define NFC_MFKEY_CAPTURE 0
#define SYS_CORE_RADIO 0
#define SYS_PRIO_SERVICE_LO 1
#define SYS_PRIO_SERVICE_HI 2
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portMUX_INITIALIZER_UNLOCKED 0
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
#define UI_FB_NAV 1
#define NFC_SND_FOUND 1
#define NFC_SND_SAVE 2
#define NOTIFY_SAVED 1
#define NOTIFY_WARNING 2

typedef int hb_nfc_err_t, portMUX_TYPE, nfc_ui_field_t;
typedef void *TaskHandle_t;
typedef struct { uint8_t uid[10], uid_len, sak, atqa[2]; } nfc_iso14443a_data_t;
typedef struct { nfc_iso14443a_data_t iso14443a; int protocol; } hb_nfc_card_data_t;
typedef struct { int phase, error; unsigned recovered, saved, pairs, processed, samples; bool captures_saved; } nfc_mfkey_status_t;
/* HEADERS */
enum {INPUT_ACTION_PRESS, INPUT_ACTION_REPEAT};
enum {INPUT_BTN_BACK, INPUT_BTN_DOWN, INPUT_BTN_UP, INPUT_BTN_OK, INPUT_BTN_RIGHT};
typedef struct { int button, action; } input_event_t;
enum {SCREEN_NFC_MENU, SCREEN_NFC_READ, SCREEN_NFC_KEYDICT, SCREEN_NFC_ULTRALIGHT,
      SCREEN_NFC_NDEF, SCREEN_NFC_APDU, SCREEN_NFC_BANKCARD, SCREEN_NFC_DESFIRE,
      SCREEN_NFC_ISO15693, SCREEN_NFC_FELICA, SCREEN_NFC_WRITE, SCREEN_NFC_EMULATE};
static struct { lv_color_t border_accent, border_inactive, bg_primary, bg_secondary,
                         text_main, text_secondary; } current_theme;
/* GLOBALS */
static unsigned read_count, save_count, name_count, exits, scans, notices, stops;
static bool fail_task, fail_save, fail_name;
static unsigned read_blocks = 64;
static int last_notice;
static void (*worker)(void *);
static keyboard_submit_cb_t keyboard_submit;
static char keyboard_title[32];
static void show_options(void);
static void run_context_action(nfc_card_action_id_t action);
static void start_block_read(void);
static void clear_result_objects(void);
static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
static lv_image_dsc_t *assets_get(const char *path) { (void)path; return NULL; }
static void show_rings(bool show) { (void)show; }
static void ui_chrome_footer_set_text(lv_obj_t *obj,const char *text) { lv_label_set_text(obj,text); }
static void nfc_ui_play_sound(int sound) { (void)sound; }
static void ui_feedback(int kind) { (void)kind; }
static void notify(int kind,const char *text) { (void)text; notices++; last_notice=kind; }
static void led_signal_info(void) {}
static void led_signal_error(void) {}
static void msgbox_open_info(const char *icon,const char *title,const char *text,lv_color_t color) { (void)icon;(void)title;(void)text;(void)color; }
static void stop_scan(void) { stops++; }
static void nfc_poller_stop(void) { stops++; }
typedef int highboy_nfc_config_t;
#define HIGHBOY_NFC_CONFIG_DEFAULT() 0
static bool highboy_nfc_is_initialized(void) { return true; }
static int highboy_nfc_init(const highboy_nfc_config_t *config) { (void)config;return ESP_OK; }
static void highboy_nfc_field_off(void) {}
static void nfc_guide_prepare(void) {}
static void nfc_guide_close(void) {}
static void card_found_cb(const hb_nfc_card_data_t *card,void *ctx) { (void)card;(void)ctx; }
static int nfc_manager_start(void (*cb)(const hb_nfc_card_data_t *,void *),void *ctx) { (void)cb;(void)ctx;scans++;return HB_NFC_OK; }
static void begin_scan(void);
static void ui_switch_screen(int screen) { (void)screen; exits++; }
static void start_reader_keys(void) { s_state=ST_READER_KEYS; }
static void ui_nfc_emulate_select_card(const nfc_sim_card_t *card,int screen) { (void)card;(void)screen; }
static bool take_pending_card(hb_nfc_card_data_t *card) { (void)card; return false; }
static void reveal(const hb_nfc_card_data_t *card) { (void)card; }
static bool nfc_guide_show(lv_obj_t *screen) { (void)screen;return false; }
static void nfc_ui_field_tick(nfc_ui_field_t *field,uint32_t elapsed) { (void)field;(void)elapsed; }
static void show_no_tag(void) { s_state=ST_ERROR; }
static void show_card_info(void) { s_state=ST_FOUND; }
static int nfc_mfc_ndef_extract_text(const void *blocks,const bool *known,char *text,size_t cap,size_t *len) { (void)blocks;(void)known;(void)text;(void)cap;*len=0;return HB_NFC_ERR_NOT_FOUND; }
int nfc_mfc_dump_block_sector(int block) { return block/4; }
int nfc_mfc_dump_first_block(int sector) { return sector*4; }
int nfc_mfc_dump_sector_blocks(int sector) { (void)sector;return 4; }
void nfc_sim_format_uid(const nfc_sim_card_t *card,char *out,int size) { (void)card;snprintf(out,size,"01020304"); }
int nfc_sim_saved_count(void) { return 0; }
const nfc_sim_card_t *nfc_sim_saved_get(int index) { (void)index;return NULL; }
bool nfc_sim_same_card(const nfc_sim_card_t *a,const nfc_sim_card_t *b) { return !memcmp(a->uid,b->uid,4); }
static int nfc_device_save_generic(const char *name,const hb_nfc_card_data_t *card) { (void)name;(void)card;name_count++;return fail_name ? ESP_FAIL : HB_NFC_OK; }
void keyboard_open_config(const keyboard_config_t *config,keyboard_submit_cb_t cb,void *context) { (void)context;keyboard_submit=cb;strlcpy(keyboard_title,config->title,sizeof(keyboard_title)); }
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,int stack,void *arg,int priority,TaskHandle_t *out,int core) {
  (void)name;(void)arg;(void)priority;(void)out;assert(core==SYS_CORE_RADIO && stack==BLOCK_WORK_STACK);
  if(fail_task)return 0;assert(!worker);worker=fn;return pdPASS;
}
static void vTaskDelete(void *task) { (void)task; }
static void finish_worker(void) { assert(worker);void (*fn)(void *)=worker;worker=NULL;fn(NULL); }
static void vTaskDelay(int ms) { (void)ms;if(worker)finish_worker(); }
hb_nfc_err_t nfc_mfc_dump_read(nfc_iso14443a_data_t *card,const nfc_mfc_dump_callbacks_t *callbacks,nfc_mfc_dump_result_t *out) {
  assert(card->uid_len==4);read_count++;out->total_blocks=64;out->sector_count=16;
  out->cancelled=callbacks->cancel(NULL);out->blocks_read=read_blocks;
  for(unsigned i=0;i<read_blocks;i++){out->block_read[i]=true;memset(out->blocks[i],i,NFC_MFC_BLOCK_SIZE);}
  return read_blocks==64 ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}
esp_err_t nfc_mfc_dump_save(const nfc_iso14443a_data_t *card,nfc_mfc_dump_result_t *out) {
  assert(card->uid_len==4 && out->blocks_read>0);save_count++;out->saved=!fail_save;return fail_save ? ESP_FAIL : ESP_OK;
}
/* CAPTURE UI */
/* FUNCTIONS */
static void press(int button) { nfc_read_input(&(input_event_t){.button=button,.action=INPUT_ACTION_PRESS},NULL); }
static void completed(void) { finish_worker();refresh_cb(NULL); }
static void check_options(void) {
  lv_obj_update_layout(s_screen);
  for(int i=0;i<s_cr.count;i++) {
    lv_area_t row,label;lv_obj_get_coords(s_cr.rows[i],&row);lv_obj_get_coords(s_cr.labels[i],&label);
    assert(label.x1>=row.x1 && label.x2<=row.x2);
  }
}
static void check_layout(void) {
  lv_obj_update_layout(s_screen);lv_area_t box;lv_obj_get_coords(s_dump,&box);
  assert(box.x1>=0 && box.x2<ui_screen_w());
  assert(box.y1>=UI_CHROME_HEADER_H && box.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
  lv_obj_get_coords(s_block_key,&box);assert(box.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
}
int main(void) {
  lv_init();lv_display_t *display=lv_display_create(240,320);
  s_screen=lv_screen_active();lv_obj_remove_style_all(s_screen);lv_obj_set_size(s_screen,240,320);
  s_status=lv_label_create(s_screen);s_hint=lv_label_create(s_screen);
  s_hw_card.protocol=HB_PROTO_MF_CLASSIC;s_hw_card.iso14443a.uid_len=4;
  s_card.uid_len=4;strcpy(s_card.type,"MIFARE Classic 1K");
  const int ids[]={NFC_CARD_ACTION_MFC_DUMP,NFC_CARD_ACTION_READER_KEYS,NFC_CARD_ACTION_MFC_NDEF_READ,
    NFC_CARD_ACTION_WRITE_DUMP,NFC_CARD_ACTION_KEY_DICTIONARY,NFC_CARD_ACTION_EMULATE_DUMP,NFC_CARD_ACTION_SAVE,NFC_CARD_ACTION_RESCAN};
  s_capabilities.action_count=8;
  for(int i=0;i<8;i++)s_capabilities.actions[i]=(nfc_card_action_t){.id=ids[i],.label="Action"};
  for(int orientation=0;orientation<2;orientation++) {
    lv_display_set_resolution(display,orientation?320:240,orientation?240:320);
    lv_obj_set_size(s_screen,ui_screen_w(),ui_screen_h());
    current_theme.bg_secondary=lv_color_hex(orientation?0xf0f0f0:0x181818);
    current_theme.text_main=lv_color_hex(orientation?0x101010:0xfafafa);
    show_options();run_context_action(NFC_CARD_ACTION_MFC_DUMP);
    assert(s_state==ST_BLOCK_READING);completed();assert(s_state==ST_BLOCK_DONE && !s_block_result.saved);
    check_layout();assert(lv_color_eq(lv_obj_get_style_bg_color(s_dump,0),current_theme.bg_secondary));
    press(INPUT_BTN_DOWN);assert(s_block_selected==1);unsigned reads=read_count;
    press(INPUT_BTN_BACK);assert(s_state==ST_OPTIONS && exits==0 && scans==0 && s_cr.count==9);
    assert(s_cr.action_ids[8]==NFC_CARD_ACTION_RESCAN);check_options();
    run_context_action(NFC_CARD_ACTION_VIEW_BLOCKS);assert(read_count==reads && s_block_selected==1);check_layout();
    press(INPUT_BTN_OK);assert(keyboard_submit && !strcmp(keyboard_title,"Save card as"));
    keyboard_submit=NULL;assert(save_count==0);press(INPUT_BTN_BACK);assert(s_state==ST_OPTIONS);
  }
  fail_task=true;run_context_action(NFC_CARD_ACTION_MFC_DUMP);assert(s_state==ST_BLOCK_ERROR && !atomic_load(&s_block_running));
  press(INPUT_BTN_BACK);assert(s_state==ST_OPTIONS && s_block_result.blocks_read==64);fail_task=false;
  read_blocks=0;run_context_action(NFC_CARD_ACTION_MFC_DUMP);completed();assert(s_state==ST_BLOCK_ERROR && s_block_result.blocks_read==64);
  press(INPUT_BTN_BACK);read_blocks=6;run_context_action(NFC_CARD_ACTION_MFC_DUMP);press(INPUT_BTN_BACK);
  assert(s_state==ST_BLOCK_READING && s_block_cancel);completed();assert(s_state==ST_OPTIONS && s_block_result.blocks_read==6 && save_count==0);
  run_context_action(NFC_CARD_ACTION_VIEW_BLOCKS);press(INPUT_BTN_OK);fail_name=true;keyboard_submit("Office",NULL);
  assert(save_count==0 && s_state==ST_BLOCK_DONE && last_notice==NOTIFY_WARNING);fail_name=false;
  fail_task=true;keyboard_submit("Office",NULL);assert(s_state==ST_OPTIONS && save_count==0 && !s_block_result.saved);fail_task=false;
  fail_save=true;on_name_submit("Office",NULL);assert(s_state==ST_BLOCK_SAVING);press(INPUT_BTN_BACK);assert(s_state==ST_BLOCK_SAVING && exits==0);
  completed();assert(s_state==ST_OPTIONS && !s_block_result.saved && s_block_result.blocks_read==6 && last_notice==NOTIFY_WARNING);
  fail_save=false;on_name_submit("Office",NULL);completed();assert(s_block_result.saved && save_count==2 && last_notice==NOTIFY_SAVED);
  on_name_submit("Office",NULL);assert(save_count==2 && !worker);check_options();
  read_blocks=4;run_context_action(NFC_CARD_ACTION_MFC_DUMP);ui_nfc_read_stop();assert(!worker && !atomic_load(&s_block_running) && s_block_cancel && save_count==2);
  refresh_cb(NULL);press(INPUT_BTN_BACK);assert(s_state==ST_OPTIONS);
  press(INPUT_BTN_BACK);assert(exits==1);
  run_context_action(NFC_CARD_ACTION_RESCAN);assert(scans==1 && s_state==ST_SCAN && !has_block_data(&s_block_result));
  read_blocks=3;start_block_read();completed();on_name_submit("New tag",NULL);assert(s_state==ST_BLOCK_SAVING);
  ui_nfc_read_stop();assert(!worker && !atomic_load(&s_block_running) && save_count==3);
  refresh_cb(NULL);assert(s_state==ST_OPTIONS && s_block_result.saved);
  puts("PASS: read without dump writes, Back/cancellation preserves context, view without RF, 9 actions, explicit save/retry/deduplication, worker cleanup, portrait/landscape and theme colors");
}
