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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define ESP_OK 0
#define HB_NFC_OK 0
#define ESP_ERR_NO_MEM -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_RESPONSE -3
#define ESP_ERR_INVALID_STATE -4
#define PLUS_MAX_KEYS 64
#define T2T_AES_BLOCK_SIZE 16
#define UL_AES_PAGE_SIZE 4
#define UL_VERSION_SIZE 8
#define T2T_TAG_ULTRALIGHT_AES 9
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef int hb_nfc_err_t;
typedef struct {uint8_t sak;} nfc_iso14443a_data_t;
typedef struct {uint8_t key[16];bool is_secure;} t2t_aes_session_t;
typedef struct {struct {int type;uint16_t aes_key_known[2];} config;uint8_t memory[240];size_t length;} nfc_t2t_file_t;
typedef struct {char heading[32];} nfc_protocol_result_t;
typedef struct {const char *sd_path,*flash_path;size_t key_size,max_keys;} nfc_dict_load_params_t;
static bool s_has_ul_aes_key,s_ul_aes_secure,s_aes_write_pending,s_aes_copy_key;
static uint8_t s_ul_aes_key[16];
static const char *s_aes_write_path="dump.nfc";
static bool stop,secure_target,wrong_type,source_key_unknown;
static unsigned reselects,auths,page_writes,fail_at,last_page;
static uint8_t target_key=0xA5;
static bool cancel_requested(void){return stop;}
static int iso14443a_poller_reselect(nfc_iso14443a_data_t *id){(void)id;reselects++;return 0;}
static int select_type_a(nfc_iso14443a_data_t *id){id->sak=0;return 0;}
static int mful_get_version(uint8_t *version){memset(version,0,8);version[0]=wrong_type?0:1;return 8;}
static bool mful_is_aes(const uint8_t *version){return version[0]==1;}
static int nfc_dict_load_file(const nfc_dict_load_params_t *params,uint8_t *keys,size_t *loaded){
 assert(params->max_keys>=1);memset(keys,target_key,16);*loaded=1;return 0;
}
static int mful_aes_auth(t2t_aes_session_t *session,const uint8_t *key,uint8_t number,bool secure){
 assert(number==0);auths++;memcpy(session->key,key,16);session->is_secure=secure;return key[0]==target_key?0:-3;
}
static int mful_aes_read_card(t2t_aes_session_t *session,const uint8_t *version,nfc_t2t_file_t *card){
 (void)version;if(session->is_secure!=secure_target)return -3;
 memset(card,0,sizeof(*card));card->config.type=T2T_TAG_ULTRALIGHT_AES;card->length=240;
 card->config.aes_key_known[0]=UINT16_MAX;memset(card->memory+192,target_key,16);
 card->memory[164]=secure_target?2:0;return 0;
}
static int nfc_t2t_file_load(const char *path,nfc_t2t_file_t *card){
 (void)path;card->length=240;card->config.type=T2T_TAG_ULTRALIGHT_AES;
 card->config.aes_key_known[0]=source_key_unknown?0:UINT16_MAX;
 for(unsigned page=0;page<60;page++)memset(card->memory+page*4,page,4);return 0;
}
static int mful_aes_write_page(t2t_aes_session_t *session,uint8_t page,const uint8_t *data){
 assert(session->is_secure==secure_target && session->key[0]==target_key);
 assert((page>=4 && page<40)||(page>=48 && page<52));assert(data[0]==page);
 page_writes++;last_page=page;return page_writes==fail_at?-3:0;
}
static void t2t_aes_reset(t2t_aes_session_t *s){memset(s,0,sizeof(*s));}
static void add_line(nfc_protocol_result_t *r,const char *fmt,...){(void)r;(void)fmt;}
/* PRODUCTION */
int main(void){
 nfc_protocol_result_t result={0};
 for(unsigned secure=0;secure<2;secure++){
  secure_target=secure;s_aes_write_pending=true;page_writes=0;
  assert(write_ul_aes(&result)==0 && page_writes==36 && last_page==39 && !s_aes_write_pending);
  s_aes_copy_key=true;s_aes_write_pending=true;page_writes=0;
  assert(write_ul_aes(&result)==0 && page_writes==40 && last_page==51);
  s_aes_copy_key=false;fail_at=3;s_aes_write_pending=true;page_writes=0;
  assert(write_ul_aes(&result)!=0 && page_writes==3);fail_at=0;
 }
 stop=true;s_aes_write_pending=true;page_writes=0;
 assert(write_ul_aes(&result)!=0 && page_writes==0);stop=false;
 wrong_type=true;s_aes_write_pending=true;
 assert(write_ul_aes(&result)!=0 && page_writes==0);wrong_type=false;
 source_key_unknown=true;s_aes_copy_key=true;s_aes_write_pending=true;
 assert(write_ul_aes(&result)!=0 && page_writes==0);source_key_unknown=false;
 s_aes_copy_key=false;s_has_ul_aes_key=true;memset(s_ul_aes_key,target_key,16);
 s_ul_aes_secure=true;s_aes_write_pending=true;
 assert(write_ul_aes(&result)==0 && page_writes==36);
 assert(auths>0 && reselects>0);
 puts("AES workflow: dictionary, plain/CMAC, user/key copy, cancellation and stop-on-failure passed");
}
