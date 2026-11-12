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
typedef struct {size_t system_count;} nfc_felica_card_t;
typedef struct {const uint8_t *key; bool (*should_cancel)(void);} nfc_felica_read_options_t;
typedef struct {char heading[32];} nfc_protocol_result_t;
static nfc_felica_card_t s_felica_card;
static bool s_has_felica,s_has_felica_key,fail_save,is_cancelled,dirty,initialized;
static uint8_t s_felica_key[16];
static char s_felica_path[64]="/sdcard/nfc/dumps/felica_0000.nfc";
static unsigned saved,reserved,released,stopped,freed,steps;
static int fail_stage,read_error;
static bool cancel_requested(void){return is_cancelled;}
static void nfc_felica_free(nfc_felica_card_t *card){memset(card,0,sizeof(*card));freed++;}
static int nfc_felica_read(nfc_felica_card_t *out,const nfc_felica_read_options_t *options){
 assert((options->key!=NULL)==s_has_felica_key);assert(options->should_cancel==cancel_requested);
 if(read_error)return read_error;
 out->system_count=2;return ESP_OK;
}
static int nfc_felica_load(const char *path,nfc_felica_card_t *out){assert(!strcmp(path,s_felica_path));if(read_error)return read_error;out->system_count=2;return ESP_OK;}
static void add_line(nfc_protocol_result_t *result,const char *format,...){(void)result;(void)format;}
static int save_felica(nfc_protocol_result_t *result){(void)result;saved++;return fail_save?SAVE_FAILED:ESP_OK;}
static int rf_frontend_reserve(int radio){assert(radio==1);if(fail_stage==1)return ESP_FAIL;reserved++;return ESP_OK;}
static int rf_frontend_unreserve(int radio){assert(radio==1);released++;return ESP_OK;}
static int highboy_nfc_field_off(void){initialized=true;return fail_stage==2?ESP_FAIL:ESP_OK;}
static void highboy_nfc_deinit(void){initialized=false;}
static bool highboy_nfc_is_initialized(void){return initialized;}
static void vTaskDelay(int n){(void)n;}
static int felica_emu_init_profile(nfc_felica_card_t *card){assert(card==&s_felica_card);return fail_stage==3?ESP_FAIL:ESP_OK;}
static int felica_emu_configure_target(void){return fail_stage==4?ESP_FAIL:ESP_OK;}
static int felica_emu_start(void){return fail_stage==5?ESP_FAIL:ESP_OK;}
static void felica_emu_run_step(void){steps++;is_cancelled=true;}
static int felica_emu_error(void){return fail_stage==6?ESP_FAIL:ESP_OK;}
static void felica_emu_stop(void){stopped++;}
static bool felica_emu_dirty(void){return dirty;}
/* PRODUCTION */
int main(void){
 nfc_protocol_result_t result={0};
 assert(read_felica(&result)==ESP_OK && s_has_felica && saved==1);
 s_has_felica_key=true;assert(read_felica(&result)==ESP_OK && saved==2);
 assert(load_felica(&result)==ESP_OK && s_has_felica && saved==3);
 fail_save=true;assert(read_felica(&result)==SAVE_FAILED && s_has_felica);fail_save=false;
 read_error=ESP_FAIL;assert(read_felica(&result)==ESP_FAIL && !s_has_felica);read_error=ESP_OK;
 is_cancelled=true;assert(read_felica(&result)==ESP_ERR_INVALID_STATE && !s_has_felica);is_cancelled=false;
 assert(emulate_felica(&result)==ESP_ERR_INVALID_STATE && !reserved);
 s_has_felica=true;
 for(fail_stage=1;fail_stage<=6;fail_stage++){
  reserved=released=stopped=steps=0;is_cancelled=false;
  assert(emulate_felica(&result)==ESP_FAIL);
  assert(reserved==released && stopped==(fail_stage==1?0U:1U));
  assert(!initialized);
 }
 fail_stage=0;is_cancelled=false;dirty=true;unsigned before=saved;
 assert(emulate_felica(&result)==ESP_OK && saved==before+1 && reserved==released);
 is_cancelled=false;fail_save=true;assert(emulate_felica(&result)==SAVE_FAILED && reserved==released);
 assert(freed>=6);
 puts("FeliCa workflow: typed read/load, save failure, cancellation, RF cleanup and session persistence passed");
}
