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
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tos_api.h"
#include "lora_session.h"
#include "meshtastic_nodedb.h"
#include "meshcore_internal.h"
#include "meshcore_nvs.h"
#include "meshtastic_nvs.h"
#include "esp_log.h"
#define EXT_RAM_BSS_ATTR
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef pthread_mutex_t *SemaphoreHandle_t;
static atomic_bool s_fail_allocate, s_fail_lock;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  if(s_fail_allocate)return NULL;
  SemaphoreHandle_t mutex=malloc(sizeof(*mutex));assert(mutex);
  assert(pthread_mutex_init(mutex,NULL)==0);return mutex;
}
static void vSemaphoreDelete(SemaphoreHandle_t mutex) {
  assert(pthread_mutex_destroy(mutex)==0);free(mutex);
}
static int xSemaphoreTake(SemaphoreHandle_t mutex,uint32_t timeout) {
  (void)timeout;if(s_fail_lock)return 0;
  return pthread_mutex_lock(mutex)==0;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex) {assert(pthread_mutex_unlock(mutex)==0);}
/* SESSION */
uint32_t xTaskGetTickCount(void) {return 1000;}
int mt_nvs_get_blob(const char *key,void *out,size_t capacity) {
  (void)key;(void)out;(void)capacity;return 0;
}
esp_err_t mt_nvs_set_blob(const char *key,const void *in,size_t size) {
  (void)key;(void)in;(void)size;return ESP_OK;
}
esp_err_t mc_nvs_get_blob(const char *key,void *out,size_t *size) {
  (void)key;(void)out;(void)size;return ESP_ERR_NOT_FOUND;
}
esp_err_t mc_nvs_set_blob(const char *key,const void *in,size_t size) {
  (void)key;(void)in;(void)size;return ESP_OK;
}
uint32_t mc_now_ms(void) {return 1000;}
uint32_t meshcore_get_unix_time(void) {return 1000;}
esp_err_t meshcore_crypto_sha256(const uint8_t *data,size_t size,uint8_t out[32]) {
  (void)data;(void)size;memset(out,7,32);return ESP_OK;
}
uint8_t mc_parse_hash_size(uint8_t selector) {
  const uint8_t values[]={1,2,3,0};return selector<4?values[selector]:0;
}
static bool s_is_packet_consumed;
static unsigned s_decrypt_calls,s_direct_messages,s_group_messages;
static bool s_is_path_payload;
static const meshcore_identity_t s_identity={.pub_key={9}};
const meshcore_identity_t *mc_get_identity(void) {return &s_identity;}
static void group_message(uint8_t channel,uint8_t hops,const char *text,uint32_t ts,
                          int16_t rssi,int8_t snr,void *context) {
  (void)channel;(void)hops;(void)ts;(void)rssi;(void)snr;(void)context;
  assert(strlen(text)<=155);++s_group_messages;
}
static void direct_message(const uint8_t *pub,const char *text,uint32_t ts,uint8_t kind,
                           uint8_t hops,int16_t rssi,int8_t snr,void *context) {
  (void)pub;(void)ts;(void)kind;(void)hops;(void)rssi;(void)snr;(void)context;
  assert(strlen(text)<=155);++s_direct_messages;
}
const meshcore_callbacks_t *mc_get_callbacks(void) {
  static const meshcore_callbacks_t callbacks={.on_grp_txt=group_message,.on_direct_msg=direct_message};
  return &callbacks;
}
esp_err_t mc_x25519(uint8_t shared[32],const uint8_t pub[32],const uint8_t private_key[64]) {
  (void)pub;(void)private_key;memset(shared,0,32);return ESP_OK;
}
int meshcore_crypto_mac_decrypt(const uint8_t *key,uint8_t *out,const uint8_t *in,int size) {
  (void)key;(void)in;++s_decrypt_calls;
  int count=size-MC_CIPHER_MAC_SIZE;
  assert(count>0 && count%16==0);
  memset(out,'A',(size_t)count);
  if(s_is_path_payload){out[0]=1;out[1]=0xab;out[2]=0;}
  else out[4]=0;
  return count;
}
esp_err_t mc_sha256_two(uint8_t *out,size_t out_len,const uint8_t *a,size_t a_len,
                    const uint8_t *b,size_t b_len) {
  (void)a;(void)b;(void)b_len;assert(a_len<=160);memset(out,0,out_len);return ESP_OK;
}
uint8_t mc_build_header(uint8_t version,uint8_t type,uint8_t route) {
  (void)version;(void)type;(void)route;return 0;
}
uint8_t mc_build_path_len(uint8_t size,uint8_t count) {(void)size;return count;}
esp_err_t mc_transmit_raw(const uint8_t *data,uint16_t size) {(void)data;(void)size;return ESP_OK;}
esp_err_t mc_send_path_return(const uint8_t pub[32],const uint8_t *path,uint8_t count,
                              uint8_t size,uint8_t type,const uint8_t *extra,size_t extra_len) {
  (void)pub;(void)path;(void)count;(void)size;(void)type;(void)extra;(void)extra_len;return ESP_OK;
}
/* RECEIVE */
static void *write_mt(void *argument) {
  uintptr_t thread=(uintptr_t)argument;
  for(unsigned i=0;i<2000;++i) {
    uint8_t payload[34]={0x12,32};memset(payload+2,(i+thread)%2?'X':'Y',32);
    assert(mt_nodedb_upsert_from_user(42,payload,sizeof(payload),3.5f,-80,1));
  }
  return NULL;
}
static void *read_mt(void *argument) {
  (void)argument;
  for(unsigned i=0;i<2000;++i) {
    uint16_t count=0;lora_node_t node;
    assert(lora_session_nodes_snapshot(0,&node,&count)==ESP_OK && count==1);
    assert(node.name[31]==0);
    for(unsigned j=1;j<31;++j)assert(node.name[j]==node.name[0]);
    assert(node.rssi==-80 && node.snr==3.5f);
  }
  return NULL;
}
static void *write_mc(void *argument) {
  (void)argument;
  for(unsigned i=0;i<2000;++i) {
    meshcore_contact_t contact={.is_used=true,.pub_key={7},.out_path_len=MESHCORE_OUT_PATH_UNKNOWN};
    memset(contact.name,i%2?'X':'Y',sizeof(contact.name));
    assert(meshcore_contact_upsert(&contact));
  }
  return NULL;
}
static void run_threads(void *(*writer)(void *),void *(*reader)(void *)) {
  pthread_t threads[3];
  assert(pthread_create(&threads[0],NULL,writer,(void *)1)==0);
  assert(pthread_create(&threads[1],NULL,writer,(void *)2)==0);
  assert(pthread_create(&threads[2],NULL,reader,NULL)==0);
  for(unsigned i=0;i<3;++i)assert(pthread_join(threads[i],NULL)==0);
}
static void *read_mc(void *argument) {
  (void)argument;
  for(unsigned i=0;i<2000;++i) {
    uint16_t count;lora_node_t node;
    assert(lora_session_nodes_snapshot(0,&node,&count)==ESP_OK && count==1);
    assert(node.name[31]==0 && node.rssi==0 && node.snr==0);
    for(unsigned j=1;j<31;++j)assert(node.name[j]==node.name[0]);
  }
  return NULL;
}
int main(void) {
  uint16_t count;mt_node_entry_t mt;meshcore_contact_t mc;lora_node_t node;
  assert(lora_session_nodes_snapshot(0,&node,&count)==ESP_ERR_INVALID_STATE);
  s_fail_allocate=true;assert(mt_nodedb_init()==ESP_ERR_NO_MEM);
  assert(mc_db_init()==ESP_ERR_NO_MEM);s_fail_allocate=false;
  assert(mt_nodedb_init()==ESP_OK);s_proto=LORA_PROTO_MESHTASTIC;
  assert(lora_session_nodes_snapshot(0,NULL,&count)==ESP_OK && count==0);
  assert(lora_session_nodes_snapshot(0,&node,&count)==ESP_ERR_NOT_FOUND);
  const uint8_t name[]={0x12,32,'X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X','X'};
  assert(mt_nodedb_upsert_from_user(42,name,sizeof(name),3.5f,-80,1));
  run_threads(write_mt,read_mt);
  assert(mt_nodedb_set_favorite(42,true));assert(mt_nodedb_set_ignored(42,true));
  assert(mt_nodedb_toggle_muted(42));mt_nodedb_learn_next_hop(42,7);
  assert(mt_nodedb_get_next_hop(42)==7);mt_nodedb_record_next_hop_failure(42);
  mt_nodedb_record_next_hop_failure(42);assert(mt_nodedb_get_next_hop(42)==0);
  assert(mt_nodedb_snapshot(0,&mt,&count)==ESP_OK && mt.is_favorite && mt.is_ignored && mt.is_muted);
  assert(mt_nodedb_remove(42));
  const uint8_t overflow[]={0x12,0xff,0xff,0xff,0xff,0x0f};
  assert(mt_nodedb_upsert_from_user(43,overflow,sizeof(overflow),0,0,0));
  assert(lora_session_nodes_snapshot(0,&node,&count)==ESP_OK && strcmp(node.name,"!0000002b")==0);
  assert(!mt_nodedb_upsert_from_user(44,NULL,1,0,0,0));
  assert(mc_db_init()==ESP_OK);s_proto=LORA_PROTO_MESHCORE;
  mc=(meshcore_contact_t){.is_used=true,.pub_key={7},.name="XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",.out_path_len=MESHCORE_OUT_PATH_UNKNOWN};
  assert(meshcore_contact_upsert(&mc));run_threads(write_mc,read_mc);
  uint8_t path[]={1,2,3};assert(mc_contact_update_path(mc.pub_key,3,path)==ESP_OK);
  assert(meshcore_contacts_snapshot(0,&mc,&count)==ESP_OK && mc.out_path_len==3 && mc.out_path[2]==3);
  assert(mc_contact_update_path(mc.pub_key,0xff,path)==ESP_ERR_INVALID_ARG);
  assert(meshcore_contact_reset_path(mc.pub_key));
  s_fail_lock=true;assert(mt_nodedb_snapshot(0,&mt,&count)==ESP_ERR_TIMEOUT);
  assert(meshcore_contacts_snapshot(0,&mc,&count)==ESP_ERR_TIMEOUT);
  assert(lora_session_nodes_snapshot(0,&node,&count)==ESP_ERR_TIMEOUT);s_fail_lock=false;
  uint8_t payload[256]={9,7};
  meshcore_packet_view_t packet={.payload=payload,.payload_len=164,.route_type=MC_RT_DIRECT};
  process_txt_msg(&packet);assert(s_decrypt_calls==1 && s_direct_messages==1);
  packet.payload_len=196;process_txt_msg(&packet);assert(s_decrypt_calls==1);
  payload[0]=7;packet.payload_len=163;process_grp_txt(&packet);
  assert(s_decrypt_calls==2 && s_group_messages==1);
  packet.payload_len=195;process_grp_txt(&packet);assert(s_decrypt_calls==2);
  payload[0]=9;s_is_path_payload=true;packet.payload_len=36;process_path_payload(&packet);
  assert(s_decrypt_calls==3);
  packet.payload_len=212;process_path_payload(&packet);assert(s_decrypt_calls==3);
  assert(meshcore_contact_remove(mc.pub_key));
  assert(mc_contact_update_path(mc.pub_key,3,path)==ESP_ERR_NOT_FOUND);
  assert(meshcore_contacts_snapshot(0,&mc,&count)==ESP_ERR_NOT_FOUND && count==0);
  vSemaphoreDelete(atomic_load(&s_nodes_lock));
  puts("PASS: actual Meshtastic/MeshCore databases, concurrent copied nodes, lock failures, path ownership, malformed protobuf and bounded authenticated payload handling (crypto/NVS/radio fakes)");
}
