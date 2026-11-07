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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define ESP_LOGW(tag,...) ((void)0)
#define HB_NFC_OK 0
#define NFC_PROTOCOL_OP_DESFIRE 30
#define NFC_PROTOCOL_OP_DESFIRE_LOAD 31
#define NFC_PROTOCOL_OP_DESFIRE_EMULATE 32
static size_t s_desfire_page;
static unsigned s_desfire_views, s_desfire_emulations, s_desfire_loads;
static void desfire_page(bool previous) {(void)previous; s_desfire_views++;}
static int nfc_protocol_service_desfire_file(const char *text) { return text && *text ? 0 : 1; }
#define NFC_PROTOCOL_OP_NTAG4XX 36
#define NFC_PROTOCOL_OP_NTAG4XX_LOAD 37
#define NFC_PROTOCOL_OP_NTAG4XX_EMULATE 38
static size_t s_ntag4xx_page;
static unsigned s_ntag4xx_views, s_ntag4xx_emulations, s_ntag4xx_loads;
static void ntag4xx_page(bool previous) {(void)previous; s_ntag4xx_views++;}
static int nfc_protocol_service_ntag4xx_file(const char *text) { return text && *text ? 0 : 1; }
#define NFC_PROTOCOL_OP_FELICA 33
#define NFC_PROTOCOL_OP_FELICA_LOAD 34
#define NFC_PROTOCOL_OP_FELICA_EMULATE 35
static size_t s_felica_page;
static unsigned s_felica_views, s_felica_emulations, s_felica_loads;
static void felica_page(bool previous) {(void)previous; s_felica_views++;}
static int nfc_protocol_service_felica_file(const char *text) { return text && *text ? 0 : 1; }
#define NFC_PROTOCOL_OP_NDEF 40
#define NFC_PROTOCOL_OP_T4T_LOAD 41
#define NFC_PROTOCOL_OP_T4T_EMULATE 42
#define NFC_PROTOCOL_OP_T4T_WRITE 43
#define NFC_PROTOCOL_OP_T4T_CREATE 44
static bool s_t4t_create;
static int s_t4t_last;
static void *s_lines[2];
static void clear_result(void) {}
static int nfc_protocol_service_t4t_file(const char *text) {return text && *text ? 0 : 1;}
#define ESP_OK 0
static int nfc_protocol_service_felica_key(const char *text) { return strlen(text)==32 || !*text ? 0 : 1; }
#define INPUT_ACTION_PRESS 1
#define INPUT_BTN_BACK 1
#define INPUT_BTN_LEFT 2
#define INPUT_BTN_RIGHT 3
#define INPUT_BTN_DOWN 4
#define INPUT_BTN_OK 5
#define INPUT_BTN_UP 6
#define NFC_PROTOCOL_OP_UL_AES_WRITE 9
#define NFC_PROTOCOL_OP_PLUS 7
#define NFC_PROTOCOL_OP_PLUS_WRITE 8
#define NFC_PROTOCOL_OP_ULTRALIGHT 6
#define NFC_PROTOCOL_OP_ST25TB 1
#define NFC_PROTOCOL_OP_ST25TB_WRITE 2
#define NFC_PROTOCOL_OP_SLIX 3
#define NFC_PROTOCOL_OP_SLIX_WRITE 24
#define NFC_PROTOCOL_OP_ARCHIVE 25
#define NFC_PROTOCOL_OP_ARCHIVE_EXPORT 26
#define NFC_PROTOCOL_OP_TEXKOM 4
#define NFC_PROTOCOL_OP_TEXKOM_EMULATE 5
#define SCREEN_NFC_MENU 1
#define UI_FB_SELECT 1
#define NFC_PROTOCOL_STATE_RUNNING 1
#define NFC_PROTOCOL_STATE_DONE 2
typedef int nfc_protocol_state_t;
typedef struct { int button, action; } input_event_t;
typedef struct { bool running; int state; } nfc_protocol_status_t;
typedef int lv_timer_t;
static bool s_write_pending, staged, keyboard, reject_stage;
static int s_operation = NFC_PROTOCOL_OP_ST25TB;
static int s_last_state = NFC_PROTOCOL_STATE_DONE;
static void *s_status = (void *)1, *s_heading = (void *)2, *s_footer = (void *)3, *s_screen = (void *)4;
static nfc_protocol_status_t current = {.state = NFC_PROTOCOL_STATE_DONE};
static unsigned writes, reads, renders, cancels;
static const char *last_label;
static int nfc_protocol_service_st25tb_write(const char *text) {
  if (reject_stage) return 1;
  staged = text != NULL; return HB_NFC_OK;
}
static char s_archive_selected[64];
static void select_archive(bool previous) {(void)previous;}
static int nfc_protocol_service_archive(const char *name) {(void)name;return 0;}
static bool plus_staged;
static int nfc_protocol_service_slix_write(const char *text) { plus_staged=text!=NULL; return 0; }
static int nfc_protocol_service_ul_aes_write(const char *text) { plus_staged=text!=NULL; return 0; }
static int nfc_protocol_service_plus_key(const char *text) {(void)text; return 0;}
static int nfc_protocol_service_plus_write(const char *text) {
  if (reject_stage) return 1;
  plus_staged = text != NULL; return 0;
}
static int nfc_protocol_service_start(int op) {
  if (op >= NFC_PROTOCOL_OP_T4T_LOAD && op <= NFC_PROTOCOL_OP_T4T_CREATE) {s_t4t_last=op; current.running=true; return 0;}
  if (op == NFC_PROTOCOL_OP_DESFIRE_LOAD) { s_desfire_loads++; current.running = true; return HB_NFC_OK; }
  if (op == NFC_PROTOCOL_OP_DESFIRE_EMULATE) { s_desfire_emulations++; current.running = true; return HB_NFC_OK; }
  if (op == NFC_PROTOCOL_OP_NTAG4XX_LOAD) { s_ntag4xx_loads++; current.running = true; return HB_NFC_OK; }
  if (op == NFC_PROTOCOL_OP_NTAG4XX_EMULATE) { s_ntag4xx_emulations++; current.running = true; return HB_NFC_OK; }
  if (op == NFC_PROTOCOL_OP_FELICA_LOAD) { s_felica_loads++; current.running = true; return HB_NFC_OK; }
  if (op == NFC_PROTOCOL_OP_FELICA_EMULATE) { s_felica_emulations++; current.running = true; return HB_NFC_OK; }
  assert(op == NFC_PROTOCOL_OP_ST25TB_WRITE || op == NFC_PROTOCOL_OP_PLUS_WRITE || op == NFC_PROTOCOL_OP_UL_AES_WRITE || op == NFC_PROTOCOL_OP_SLIX_WRITE);
  if (op == NFC_PROTOCOL_OP_PLUS_WRITE || op == NFC_PROTOCOL_OP_UL_AES_WRITE || op == NFC_PROTOCOL_OP_SLIX_WRITE) {assert(plus_staged);plus_staged=false;}
  else {assert(staged);staged=false;} writes++; current.running = true;
  current.state = NFC_PROTOCOL_STATE_RUNNING;
  return HB_NFC_OK;
}
static bool keyboard_is_open(void) {return keyboard;}
static void keyboard_open(void *initial, void (*submit)(const char *, void *), void *ctx) {
  (void)initial; (void)submit; (void)ctx; keyboard = true;
}
static void nfc_protocol_service_get_status(nfc_protocol_status_t *status) {*status = current;}
static void nfc_protocol_service_cancel(void) {cancels++;}
static int nfc_protocol_service_slix_password(const char *text) {(void)text; return 0;}
static int nfc_protocol_service_ul_aes_key(const char *text) {(void)text;return 0;}
static void aes_key_submit(const char *text, void *ctx) {(void)text;(void)ctx;}
static void password_submit(const char *text, void *ctx) {(void)text; (void)ctx;}
static void ui_feedback(int event) {(void)event;}
static void ui_switch_screen(int screen) {(void)screen;}
static void start_read(void) {reads++;}
static void lv_label_set_text(void *label, const char *text) {(void)label; last_label = text;}
static void ui_chrome_footer_set_text(void *footer, const char *text) {(void)footer; (void)text;}
static void *lv_screen_active(void) {return s_screen;}
static void render(const nfc_protocol_status_t *status) {(void)status; renders++;}
/* PRODUCTION */
static void press(int button) {
  const input_event_t event = {.action = INPUT_ACTION_PRESS, .button = button};
  input_cb(&event, NULL);
}
int main(void) {
  press(INPUT_BTN_RIGHT); assert(keyboard && !writes);
  keyboard = false;
  write_submit("03:01234567", NULL);
  assert(staged && s_write_pending && !writes);
  assert(!strcmp(last_label, "03:01234567"));
  s_last_state = NFC_PROTOCOL_STATE_RUNNING;
  timer_cb(NULL); assert(!renders); /* Refresh cannot hide a pending confirmation. */
  press(INPUT_BTN_BACK); assert(!staged && !s_write_pending && !writes);
  write_submit(NULL, NULL); assert(!s_write_pending);
  reject_stage = true;
  write_submit("bad", NULL); assert(!s_write_pending && !writes);
  reject_stage = false;
  write_submit("03:01234567", NULL);
  press(INPUT_BTN_OK); assert(writes == 1 && !s_write_pending && !staged);
  press(INPUT_BTN_OK); assert(writes == 1 && !reads);
  press(INPUT_BTN_BACK); assert(cancels == 1);
  current.running = false; current.state = NFC_PROTOCOL_STATE_DONE;
  press(INPUT_BTN_OK); assert(reads == 1 && writes == 1);
  s_operation=NFC_PROTOCOL_OP_PLUS;
  plus_write_submit("plain:plus_1.nfc",NULL);
  assert(plus_staged && s_write_pending && writes==1);
  press(INPUT_BTN_BACK);assert(!plus_staged && !s_write_pending && writes==1);
  plus_write_submit("plus_1.nfc",NULL);press(INPUT_BTN_OK);
  assert(writes==2 && !plus_staged && !s_write_pending);
  current.running=false;
  plus_key_submit("00000000000000000000000000000000",NULL);assert(reads==2);
  s_operation=NFC_PROTOCOL_OP_ULTRALIGHT;
  aes_write_submit("key:aes_1.nfc",NULL);assert(s_write_pending && plus_staged && writes==2);
  press(INPUT_BTN_OK);assert(writes==3 && !plus_staged && !s_write_pending);
  current.running=false;s_operation=NFC_PROTOCOL_OP_SLIX;
  slix_write_submit("slix_1.nfc",NULL);assert(s_write_pending && writes==3);
  press(INPUT_BTN_BACK);assert(!s_write_pending && !plus_staged && writes==3);
  slix_write_submit("slix_1.nfc",NULL);press(INPUT_BTN_OK);
  assert(writes==4 && !plus_staged && !s_write_pending);
  current.running = false; s_operation = NFC_PROTOCOL_OP_DESFIRE;
  press(INPUT_BTN_UP); press(INPUT_BTN_DOWN); assert(s_desfire_views == 2);
  press(INPUT_BTN_RIGHT); assert(keyboard); keyboard = false;
  desfire_load_submit("", NULL); assert(!s_desfire_loads);
  desfire_load_submit("desfire_0000.nfc", NULL); assert(s_desfire_loads == 1);
  current.running = false;
  press(INPUT_BTN_LEFT); assert(s_desfire_emulations == 1 && current.running);
  press(INPUT_BTN_LEFT); assert(s_desfire_emulations == 1);
  press(INPUT_BTN_BACK); assert(cancels == 2);
  current.running = false; s_operation = NFC_PROTOCOL_OP_FELICA;
  press(INPUT_BTN_UP); press(INPUT_BTN_DOWN); assert(s_felica_views == 2);
  press(INPUT_BTN_RIGHT); assert(keyboard); keyboard = false;
  felica_load_submit("key:bad", NULL); assert(!s_felica_loads);
  felica_load_submit("key:00000000000000000000000000000000", NULL);
  assert(strstr(last_label, "Lite key set"));
  felica_load_submit("key:", NULL); assert(strstr(last_label, "cleared"));
  felica_load_submit("felica_0000.nfc", NULL); assert(s_felica_loads == 1);
  current.running = false;
  press(INPUT_BTN_LEFT); assert(s_felica_emulations == 1 && current.running);
  press(INPUT_BTN_LEFT); assert(s_felica_emulations == 1);
  press(INPUT_BTN_BACK); assert(cancels == 3);
  current.running = false; s_operation = NFC_PROTOCOL_OP_NTAG4XX;
  press(INPUT_BTN_UP); press(INPUT_BTN_DOWN); assert(s_ntag4xx_views == 2);
  press(INPUT_BTN_RIGHT); assert(keyboard); keyboard = false;
  ntag4xx_load_submit("", NULL); assert(!s_ntag4xx_loads);
  ntag4xx_load_submit("ntag4xx_0000.nfc", NULL); assert(s_ntag4xx_loads == 1);
  current.running = false;
  press(INPUT_BTN_LEFT); assert(s_ntag4xx_emulations == 1 && current.running);
  press(INPUT_BTN_LEFT); assert(s_ntag4xx_emulations == 1);
  press(INPUT_BTN_BACK); assert(cancels == 4);
  current.running = false; s_operation = NFC_PROTOCOL_OP_NDEF;
  press(INPUT_BTN_DOWN); assert(s_write_pending && !s_t4t_last);
  press(INPUT_BTN_BACK); assert(!s_write_pending && !s_t4t_last);
  press(INPUT_BTN_UP); assert(s_write_pending && s_t4t_create && !s_t4t_last);
  press(INPUT_BTN_OK); assert(!s_write_pending && s_t4t_last == NFC_PROTOCOL_OP_T4T_CREATE);
  current.running = false;
  press(INPUT_BTN_DOWN); press(INPUT_BTN_OK); assert(s_t4t_last == NFC_PROTOCOL_OP_T4T_WRITE);
  current.running = false;
  press(INPUT_BTN_LEFT); assert(s_t4t_last == NFC_PROTOCOL_OP_T4T_EMULATE);
  current.running = false;
  t4t_load_submit("t4t_0000.nfc", NULL); assert(s_t4t_last == NFC_PROTOCOL_OP_T4T_LOAD);
  puts("ST25TB/Plus/AES/SLIX/DESFire UI: staging, explicit confirmation, cancellation and refresh passed");
}
