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
#include "lora_session.h"
#include "tos_api.h"
#define EXT_RAM_BSS_ATTR
#define ESP_LOGE(tag, ...) ((void)(tag))
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef pthread_mutex_t *SemaphoreHandle_t;
static atomic_uint s_allocations, s_deletions, s_created;
static atomic_bool s_should_race;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  SemaphoreHandle_t mutex=malloc(sizeof(*mutex)); assert(mutex!=NULL);
  assert(pthread_mutex_init(mutex,NULL)==0);
  ++s_allocations; ++s_created;
  if(s_should_race) while(s_created<2) {}
  return mutex;
}
static void vSemaphoreDelete(SemaphoreHandle_t mutex) {
  assert(pthread_mutex_destroy(mutex)==0); free(mutex); ++s_deletions;
}
static int xSemaphoreTake(SemaphoreHandle_t mutex,uint32_t timeout) {
  (void)timeout; assert(mutex!=NULL); return pthread_mutex_lock(mutex)==0;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(pthread_mutex_unlock(mutex)==0); }
/* NATIVE_RING */
static void *write_messages(void *argument) {
  (void)argument;
  for(unsigned i=0;i<1000;++i) lora_session_on_rx_text("peer","hello");
  return NULL;
}
int main(void) {
  pthread_t threads[2]; s_should_race=true;
  assert(pthread_create(&threads[0],NULL,write_messages,NULL)==0);
  assert(pthread_create(&threads[1],NULL,write_messages,NULL)==0);
  for(unsigned i=0;i<2;++i) assert(pthread_join(threads[i],NULL)==0);
  assert(s_allocations==2 && s_deletions==1 && s_total==2000);
  assert(lora_session_msg_count()==MSG_RING);
  uint32_t first=0, second=0; lora_msg_t messages[MSG_RING];
  assert(lora_session_msg_since(&first,messages,MSG_RING)==MSG_RING);
  assert(strcmp(messages[0].text,"hello")==0 && first==2000);
  assert(lora_session_msg_since(&first,messages,MSG_RING)==0);
  assert(lora_session_msg_since(&second,messages,1)==1 && second==1977);
  s_total=UINT32_MAX-1; s_head=s_count=0;
  lora_session_on_rx_text("before","wrap");
  lora_session_on_rx_text("after","wrap");
  first=UINT32_MAX-1;
  assert(lora_session_msg_since(&first,messages,MSG_RING)==2 && first==0);
  assert(strcmp(messages[0].who,"before")==0 && strcmp(messages[1].who,"after")==0);
  lora_session_on_rx_radio("radio", "measured", -87, 9);
  assert(lora_session_msg_since(&first,messages,MSG_RING)==1);
  assert(messages[0].has_rf && messages[0].rssi_dbm==-87 && messages[0].snr_db==9);
  lora_session_on_rx_text("mqtt", "unknown");
  assert(lora_session_msg_since(&first,messages,MSG_RING)==1);
  assert(!messages[0].has_rf && messages[0].rssi_dbm==0);
  vSemaphoreDelete(s_lock); s_lock=NULL;
  puts("Chat ring: simultaneous first use, retained window, independent cursors and wrap passed");
  return 0;
}
