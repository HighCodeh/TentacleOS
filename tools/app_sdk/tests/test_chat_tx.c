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
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef pthread_mutex_t *SemaphoreHandle_t;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) {
  SemaphoreHandle_t mutex=malloc(sizeof(*mutex));assert(mutex);
  assert(pthread_mutex_init(mutex,NULL)==0);return mutex;
}
static void vSemaphoreDelete(SemaphoreHandle_t mutex) {
  assert(pthread_mutex_destroy(mutex)==0);free(mutex);
}
static int xSemaphoreTake(SemaphoreHandle_t mutex,uint32_t timeout) {
  (void)timeout;return pthread_mutex_lock(mutex)==0;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(pthread_mutex_unlock(mutex)==0); }
static atomic_bool s_start_entered, s_start_release;
static unsigned s_start_calls, s_power_holds, s_deinits, s_backend_calls;
static esp_err_t s_start_error, s_backend_error;
static char s_last_text[160];
static esp_err_t meshtastic_app_start(void) {
  ++s_start_calls;s_start_entered=true;
  while(!s_start_release) {}
  return s_start_error;
}
static esp_err_t meshcore_app_start(void) { ++s_start_calls;return s_start_error; }
static esp_err_t power_manager_no_sleep_acquire(void) { ++s_power_holds;return ESP_OK; }
static esp_err_t power_manager_no_sleep_release(void) { assert(s_power_holds);--s_power_holds;return ESP_OK; }
static esp_err_t sx1262_deinit(void) { ++s_deinits;return ESP_OK; }
static esp_err_t meshtastic_mesh_send_text(const char *text,uint32_t to) {
  assert(to==UINT32_MAX);++s_backend_calls;strcpy(s_last_text,text);return s_backend_error;
}
static esp_err_t meshcore_send_grp_txt(uint8_t channel,const char *text) {
  assert(channel==0);++s_backend_calls;strcpy(s_last_text,text);return s_backend_error;
}
/* NATIVE_TX */
static void *start(void *ctx) {
  (void)ctx;assert(lora_session_start(LORA_PROTO_MESHTASTIC)==ESP_OK);return NULL;
}
static atomic_uint s_accepted;
static void *produce(void *ctx) {
  (void)ctx;
  if(lora_session_send_text("concurrent")==ESP_OK) ++s_accepted;
  return NULL;
}
int main(void) {
  assert(lora_session_send_text(NULL)==ESP_ERR_INVALID_ARG);
  assert(lora_session_send_text("")==ESP_ERR_INVALID_ARG);
  assert(lora_session_send_text("hello")==ESP_ERR_INVALID_STATE);
  assert(lora_session_start(LORA_PROTO_NONE)==ESP_ERR_INVALID_ARG);
  s_start_error=ESP_FAIL;
  assert(lora_session_start(LORA_PROTO_MESHCORE)==ESP_FAIL);
  assert(lora_session_active()==LORA_PROTO_NONE && s_power_holds==0 && s_deinits==1);
  s_start_error=ESP_OK;s_start_calls=s_deinits=0;
  pthread_t task;
  assert(pthread_create(&task,NULL,start,NULL)==0);
  while(!s_start_entered) {}
  assert(lora_session_active()==LORA_PROTO_NONE);
  assert(lora_session_start(LORA_PROTO_MESHCORE)==ESP_ERR_INVALID_STATE);
  assert(lora_session_send_text("hello")==ESP_ERR_INVALID_STATE);
  s_start_release=true;
  assert(pthread_join(task,NULL)==0);
  assert(s_start_calls==1 && s_power_holds==1 && s_deinits==0);
  assert(lora_session_start(LORA_PROTO_MESHTASTIC)==ESP_OK && s_start_calls==1);
  assert(lora_session_start(LORA_PROTO_MESHCORE)==ESP_ERR_INVALID_STATE);
  char text[161];memset(text,'x',sizeof(text));text[160]=0;
  assert(lora_session_send_text(text)==ESP_ERR_INVALID_ARG);
  text[159]=0;
  assert(lora_session_send_text(text)==ESP_OK);text[0]='z';
  lora_session_process_tx(LORA_PROTO_NONE);
  lora_session_process_tx(LORA_PROTO_MESHCORE);
  assert(s_backend_calls==0 && s_tx_count==1);
  lora_session_process_tx(LORA_PROTO_MESHTASTIC);
  assert(s_backend_calls==1 && strlen(s_last_text)==159 && s_last_text[0]=='x' && s_count==1);
  for(unsigned i=0;i<TX_QUEUE_DEPTH;++i) {
    char queued[32];snprintf(queued,sizeof(queued),"entry-%u",i);
    assert(lora_session_send_text(queued)==ESP_OK);
  }
  assert(lora_session_send_text("overflow")==ESP_ERR_TIMEOUT);
  for(unsigned i=0;i<TX_QUEUE_DEPTH;++i) {
    lora_session_process_tx(LORA_PROTO_MESHTASTIC);
    char expected[32];snprintf(expected,sizeof(expected),"entry-%u",i);
    assert(strcmp(s_last_text,expected)==0);
  }
  assert(s_tx_count==0 && s_count==1+TX_QUEUE_DEPTH);
  s_backend_error=ESP_ERR_TIMEOUT;
  assert(lora_session_send_text("fail")==ESP_OK);
  lora_session_process_tx(LORA_PROTO_MESHTASTIC);
  assert(s_count==1+TX_QUEUE_DEPTH && s_tx_count==0);
  s_backend_error=ESP_OK;
  pthread_t producers[16];
  for(unsigned i=0;i<16;++i) assert(pthread_create(&producers[i],NULL,produce,NULL)==0);
  for(unsigned i=0;i<16;++i) assert(pthread_join(producers[i],NULL)==0);
  assert(s_accepted==TX_QUEUE_DEPTH && s_tx_count==TX_QUEUE_DEPTH);
  for(unsigned i=0;i<TX_QUEUE_DEPTH;++i) lora_session_process_tx(LORA_PROTO_MESHTASTIC);
  s_proto=LORA_PROTO_MESHCORE;
  assert(lora_session_send_text("meshcore")==ESP_OK);
  lora_session_process_tx(LORA_PROTO_MESHCORE);
  assert(strcmp(s_last_text,"meshcore")==0 && s_tx_count==0);
  vSemaphoreDelete(s_lock);s_lock=NULL;
  puts("LoRa queued TX: concurrent startup/producers, copied bounds, saturation, FIFO, backend failure and native dispatch passed");
  return 0;
}
