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
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HB_NFC_OK 0
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -8
#define SAVE_FAILED -9
#define DESFIRE_RETRY_MS 10
#define RF_FRONTEND_NFC_13MHZ 1
#define pdMS_TO_TICKS(x) (x)
#define taskYIELD() ((void)0)
#undef strlcpy
#define strlcpy(out,in,size) snprintf(out,size,"%s",in)
typedef int hb_nfc_err_t;
typedef int esp_err_t;
typedef struct {int value;} nfc_iso14443a_data_t;
typedef struct {int value;} t4t_cc_t;
typedef struct {int dep;bool (*should_cancel)(void);} mf_desfire_session_t;
typedef struct {int identity;bool has_cc,has_ndef;uint8_t cc[12],ndef[1024];size_t ndef_length;} nfc_ntag4xx_card_t;
typedef struct {char heading[32];} nfc_protocol_result_t;
static nfc_ntag4xx_card_t s_ntag4xx_card;
static bool s_has_ntag4xx,fail_save,is_cancelled,initialized;
static char s_ntag4xx_path[64]="/sdcard/nfc/dumps/ntag4xx_0000.nfc";
static unsigned saved,reserved,released,stopped,steps;
static int fail_stage,read_error,cc_error,ndef_error;
static bool cancel_requested(void){return is_cancelled;}
static int select_type_a(nfc_iso14443a_data_t *card){card->value=1;return ESP_OK;}
static int mf_desfire_poller_init(nfc_iso14443a_data_t *card,mf_desfire_session_t *session){assert(card->value==1);session->dep=1;return ESP_OK;}
static int nfc_ntag4xx_read(mf_desfire_session_t *session,nfc_iso14443a_data_t *card,nfc_ntag4xx_card_t *out){
 assert(card->value==1 && session->should_cancel==cancel_requested);
 if(read_error)return read_error;
 out->identity=42;return ESP_OK;
}
static int t4t_read_cc(int *dep,t4t_cc_t *cc){assert(*dep==1);cc->value=1;return cc_error;}
static void encode_cc(t4t_cc_t *cc,uint8_t *out){assert(cc->value==1);out[0]=42;}
static int t4t_read_ndef(int *dep,t4t_cc_t *cc,uint8_t *out,size_t size,size_t *length){
 assert(*dep==1 && cc->value==1 && size==1024);out[0]=42;*length=1;return ndef_error;
}
static void format_ndef_records(const uint8_t *data,size_t length,nfc_protocol_result_t *out){assert(data[0]==42 && length==1);(void)out;}
static const char *esp_err_to_name(int error){(void)error;return "error";}
static const char *nfc_ntag4xx_name(nfc_ntag4xx_card_t *card){assert(card->identity==42);return "NTAG424 DNA";}
static int nfc_ntag4xx_load(const char *path,nfc_ntag4xx_card_t *out){assert(!strcmp(path,s_ntag4xx_path));if(read_error)return read_error;out->identity=42;return ESP_OK;}
static void add_line(nfc_protocol_result_t *result,const char *format,...){(void)result;(void)format;}
static int save_ntag4xx(nfc_protocol_result_t *result){(void)result;saved++;return fail_save?SAVE_FAILED:ESP_OK;}
static int rf_frontend_reserve(int radio){assert(radio==1);if(fail_stage==1)return ESP_FAIL;reserved++;return ESP_OK;}
static int rf_frontend_unreserve(int radio){assert(radio==1);released++;return ESP_OK;}
static int highboy_nfc_field_off(void){initialized=true;return fail_stage==2?ESP_FAIL:ESP_OK;}
static void highboy_nfc_deinit(void){initialized=false;}
static bool highboy_nfc_is_initialized(void){return initialized;}
static void vTaskDelay(int n){(void)n;}
static int t4t_emu_init_identity(int *identity){assert(*identity==42);return fail_stage==3?ESP_FAIL:ESP_OK;}
static int t4t_emu_configure_target(void){return fail_stage==4?ESP_FAIL:ESP_OK;}
static int t4t_emu_start(void){return fail_stage==5?ESP_FAIL:ESP_OK;}
static void t4t_emu_run_step(void){steps++;is_cancelled=true;}
static bool t4t_emu_has_error(void){return fail_stage==6;}
static void t4t_emu_stop(void){stopped++;}
/* PRODUCTION */
int main(void){
 nfc_protocol_result_t result={0};
 assert(read_ntag4xx(&result)==ESP_OK && s_has_ntag4xx && saved==1);
 assert(s_ntag4xx_card.has_cc && s_ntag4xx_card.has_ndef && s_ntag4xx_card.ndef_length==1);
 cc_error=ESP_FAIL;assert(read_ntag4xx(&result)==ESP_OK && !s_ntag4xx_card.has_cc && !s_ntag4xx_card.has_ndef);cc_error=ESP_OK;
 ndef_error=ESP_FAIL;assert(read_ntag4xx(&result)==ESP_OK && s_ntag4xx_card.has_cc && !s_ntag4xx_card.has_ndef && !s_ntag4xx_card.ndef_length);ndef_error=ESP_OK;
 assert(load_ntag4xx(&result)==ESP_OK && s_has_ntag4xx);
 fail_save=true;assert(read_ntag4xx(&result)==SAVE_FAILED && s_has_ntag4xx);fail_save=false;
 read_error=ESP_FAIL;assert(read_ntag4xx(&result)==ESP_FAIL && !s_has_ntag4xx);read_error=ESP_OK;
 is_cancelled=true;assert(read_ntag4xx(&result)==ESP_ERR_INVALID_STATE && !s_has_ntag4xx);is_cancelled=false;
 assert(emulate_ntag4xx()==ESP_ERR_INVALID_STATE && !reserved);
 assert(load_ntag4xx(&result)==ESP_OK);
 for(fail_stage=1;fail_stage<=6;fail_stage++){
  reserved=released=stopped=steps=0;is_cancelled=false;
  assert(emulate_ntag4xx()==ESP_FAIL);
  assert(reserved==released && stopped==(fail_stage==1?0U:1U));
  assert(!initialized);
 }
 fail_stage=0;is_cancelled=false;
 assert(emulate_ntag4xx()==ESP_OK && reserved==released && steps);
 puts("NTAG4xx workflow: optional NDEF failures, cancellation, identity replay and RF cleanup passed");
}
