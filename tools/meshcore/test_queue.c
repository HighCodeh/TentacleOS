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
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define TX_QUEUE_DEPTH 4
#define SX1262_LORA_BW_125 4
#define SX1262_LORA_BW_250 5
#define SX1262_LORA_BW_500 6
typedef enum {LORA_PROTO_NONE,LORA_PROTO_MESHTASTIC,LORA_PROTO_MESHCORE} lora_proto_t;
typedef struct {uint32_t frequency_hz;int8_t tx_power_dbm;uint8_t sf,bw,cr;uint16_t preamble_len,sync_word;} sx1262_config_t;
static _Atomic(lora_proto_t) s_proto;
static atomic_bool s_is_ready;
static char s_tx_text[4][160];
static uint8_t s_tx_peer[4][32],s_tx_kind[4];
static uint16_t s_tx_head,s_tx_count;
static bool locked,fail_lock;
static unsigned submissions,adverts,pushes;
static uint32_t last_to;
static uint8_t last_peer[32];
static char last_text[160];
static esp_err_t backend_error;
static bool ring_lock(void){assert(!locked);if(fail_lock)return false;locked=true;return true;}
static void ring_unlock(void){assert(locked);locked=false;}
static lora_proto_t lora_session_active(void){return s_is_ready?s_proto:LORA_PROTO_NONE;}
static void ring_push(bool outgoing,const char *who,const char *text){assert(!locked&&outgoing&&who&&text);++pushes;}
static esp_err_t send_text(const char *text){assert(!locked);strcpy(last_text,text);++submissions;return backend_error;}
static esp_err_t meshtastic_mesh_send_nodeinfo(void){assert(!locked);++adverts;return ESP_OK;}
static esp_err_t meshcore_send_advert_throttled(void){return meshtastic_mesh_send_nodeinfo();}
static esp_err_t meshtastic_mesh_send_text(const char *text,uint32_t to){last_to=to;return send_text(text);}
static uint32_t meshcore_get_unix_time(void){return 1234;}
static esp_err_t meshcore_send_direct_msg(const uint8_t *peer,const char *text,uint32_t t,uint8_t attempt,uint32_t *ack){
 assert(t==1234&&attempt==0);memcpy(last_peer,peer,32);*ack=0x12345678;return send_text(text);
}
/* PRODUCTION */
int main(void){
 char text[160]="copied";uint8_t peer[32];memset(peer,0x42,sizeof(peer));
 assert(lora_session_send_text(text)==ESP_ERR_INVALID_STATE);
 s_proto=LORA_PROTO_MESHTASTIC;s_is_ready=true;
 assert(lora_session_send_text("")==ESP_ERR_INVALID_ARG);
 memset(text,'a',160);assert(lora_session_send_text(text)==ESP_ERR_INVALID_ARG);
 strcpy(text,"copied");assert(lora_session_send_text(text)==ESP_OK);strcpy(text,"mutated");
 assert(submissions==0);lora_session_process_tx(LORA_PROTO_MESHCORE);assert(submissions==0);
 lora_session_process_tx(LORA_PROTO_MESHTASTIC);assert(submissions==1&&strcmp(last_text,"copied")==0);
 for(unsigned i=0;i<4;i++)assert(lora_session_send_text("queued")==ESP_OK);
 assert(lora_session_send_text("overflow")==ESP_ERR_TIMEOUT);
 for(unsigned i=0;i<4;i++)lora_session_process_tx(LORA_PROTO_MESHTASTIC);
 assert(s_tx_count==0 && !s_tx_status.processing && s_tx_status.completed==5 && s_tx_status.last_error==ESP_OK);
 assert(lora_session_send_direct("dm",peer,32)==ESP_ERR_INVALID_ARG);
 uint8_t mt[4]={0x12,0x34,0xab,0xcd};
 assert(lora_session_send_direct("dm",mt,4)==ESP_OK);memset(mt,0,4);
 lora_session_process_tx(LORA_PROTO_MESHTASTIC);assert(last_to==0x1234abcd&&pushes==1);
 s_proto=LORA_PROTO_MESHCORE;
 assert(lora_session_send_direct("mc",peer,32)==ESP_OK);peer[0]=0;
 lora_session_process_tx(LORA_PROTO_MESHCORE);assert(last_peer[0]==0x42&&pushes==2);
 backend_error=ESP_ERR_TIMEOUT;assert(lora_session_send_direct("failed",peer,32)==ESP_OK);
 lora_session_process_tx(LORA_PROTO_MESHCORE);assert(pushes==2);
 assert(!s_tx_status.processing && s_tx_status.completed==8 && s_tx_status.last_error==ESP_ERR_TIMEOUT);
 assert(lora_session_advertise()==ESP_OK);lora_session_process_tx(LORA_PROTO_MESHCORE);assert(adverts==1);
 fail_lock=true;assert(lora_session_send_text("busy")==ESP_ERR_TIMEOUT);fail_lock=false;
 puts("PASS: production mesh queue bounds/copies/ownership readiness/DM addressing/failure reporting");
}
