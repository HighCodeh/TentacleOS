#define _POSIX_C_SOURCE 200809L
#include "tos_api_lora.h"
#include "lora_session.h"
#include "sx1262.h"
#include "freertos/task.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct test_task {
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t condition;
  unsigned notifications;
  void (*entry)(void *);
  void *arg;
};
static _Thread_local TaskHandle_t s_self;
static struct test_task s_main, s_foreign, s_rf, s_tasks[1];
static unsigned s_task_count;
static pthread_mutex_t s_gate = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool s_is_stopped;
static atomic_bool s_fail_create;
static atomic_uint s_power, s_lost;
static uint32_t s_caps = TOS_CAP_RADIO_TX;
static int should_stop(void) { return atomic_load(&s_is_stopped); }
static const tos_api_t API = {.should_stop=should_stop};
const tos_api_t *tos_api_get(void) { return &API; }
bool tos_app_cap_check(uint32_t caps) { return (s_caps & caps)==caps; }
void tos_app_ctx_clear_resource_lost(void) { atomic_store(&s_lost,0); }
void tos_app_ctx_signal_resource_lost(void *owner) { if(owner!=NULL) atomic_fetch_add(&s_lost,1); }
bool host_local_rf_is_ready(void) { return true; }
bool host_local_rf_lock(uint32_t ms) {
  TickType_t start=xTaskGetTickCount();
  do { if(pthread_mutex_trylock(&s_gate)==0)return true;vTaskDelay(1); }
  while(xTaskGetTickCount()-start<ms);
  return false;
}
void host_local_rf_unlock(void) { assert(pthread_mutex_unlock(&s_gate)==0); }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return s_self; }
TickType_t xTaskGetTickCount(void) {
  struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
  return (TickType_t)((uint64_t)now.tv_sec*1000+now.tv_nsec/1000000);
}
void vTaskDelay(TickType_t ms) {
  struct timespec delay={.tv_sec=ms/1000,.tv_nsec=(long)(ms%1000)*1000000};nanosleep(&delay,NULL);
}
void taskYIELD(void) { vTaskDelay(1); }
static void *run_task(void *arg) { s_self=arg;s_self->entry(s_self->arg);return NULL; }
int xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned stack,void *arg,
                           unsigned priority,TaskHandle_t *out,unsigned core) {
  (void)name;assert(stack==4096 && priority==4 && core==0);
  if(atomic_load(&s_fail_create))return 0;
  assert(s_task_count<1);struct test_task *task=&s_tasks[s_task_count++];
  task->entry=entry;task->arg=arg;
  assert(pthread_mutex_init(&task->lock,NULL)==0);assert(pthread_cond_init(&task->condition,NULL)==0);
  *out=task;assert(pthread_create(&task->thread,NULL,run_task,task)==0);return 1;
}
void xTaskNotifyGive(TaskHandle_t task) {
  pthread_mutex_lock(&task->lock);++task->notifications;pthread_cond_signal(&task->condition);pthread_mutex_unlock(&task->lock);
}
static void unlock(void *arg) { pthread_mutex_unlock(arg); }
unsigned ulTaskNotifyTake(int clear,uint32_t ms) {
  assert(clear==1 && ms==UINT32_MAX);unsigned count;
  pthread_mutex_lock(&s_self->lock);pthread_cleanup_push(unlock,&s_self->lock);
  while(!s_self->notifications)pthread_cond_wait(&s_self->condition,&s_self->lock);
  count=s_self->notifications;s_self->notifications=0;pthread_cleanup_pop(1);return count;
}
static void *s_radio_owner;
static bool s_running;
static bool s_has_hal;
static sx1262_callbacks_t s_callbacks;
static sx1262_config_t s_config;
static _Atomic(lora_proto_t) s_proto;
static atomic_bool s_fail_deinit;
static esp_err_t s_start_error, s_tx_error;
static bool s_tx_timeout, s_mesh_partial;
static unsigned s_deinits, s_transmits, s_rx_starts;
static int s_packets;
static bool s_corrupt;
static void assert_worker(void) { assert(s_self==&s_rf); }
esp_err_t host_local_rf_call(esp_err_t (*cb)(void *),void *arg) {
  TaskHandle_t previous=s_self;s_self=&s_rf;esp_err_t error=cb(arg);s_self=previous;return error;
}
esp_err_t power_manager_no_sleep_acquire(void) { assert_worker();atomic_fetch_add(&s_power,1);return ESP_OK; }
esp_err_t power_manager_no_sleep_release(void) { assert_worker();assert(atomic_fetch_sub(&s_power,1)>0);return ESP_OK; }
esp_err_t lora_session_radio_reserve(void *owner) {
  assert_worker();if(s_radio_owner!=NULL)return ESP_ERR_INVALID_STATE;s_radio_owner=owner;return ESP_OK;
}
esp_err_t lora_session_radio_release(void *owner) {
  assert_worker();assert(s_radio_owner==owner);s_radio_owner=NULL;return ESP_OK;
}
esp_err_t sx1262_hal_create(sx1262_hal_t *hal) { assert_worker();memset(hal,0,sizeof(*hal));s_has_hal=true;return ESP_OK; }
esp_err_t sx1262_init(const sx1262_config_t *config) {
  assert_worker();assert(s_radio_owner!=NULL && s_has_hal);s_config=*config;
  assert(config->cr==1 && config->preamble_len==12 && config->is_crc_on && !config->is_implicit_hdr);
  return s_start_error;
}
esp_err_t sx1262_config_lora(const sx1262_config_t *config) { assert_worker();s_config=*config;return ESP_OK; }
esp_err_t sx1262_set_callbacks(const sx1262_callbacks_t *callbacks) { assert_worker();s_callbacks=*callbacks;return ESP_OK; }
esp_err_t sx1262_start(void) { assert_worker();s_running=true;return ESP_OK; }
bool sx1262_is_running(void) { assert_worker();return s_running; }
esp_err_t sx1262_deinit(void) {
  assert_worker();++s_deinits;if(atomic_load(&s_fail_deinit))return ESP_FAIL;
  s_running=false;s_has_hal=false;memset(&s_callbacks,0,sizeof(s_callbacks));return ESP_OK;
}
esp_err_t sx1262_receive_continuous(void) { assert(s_running);++s_rx_starts;return ESP_OK; }
esp_err_t sx1262_transmit(const uint8_t *data,uint8_t len,uint32_t timeout) {
  assert_worker();assert(s_running && len==3 && data[0]==1 && data[2]==3 && timeout==60000);++s_transmits;
  if(s_tx_error!=ESP_OK)return s_tx_error;
  if(s_tx_timeout)s_callbacks.on_timeout(s_callbacks.cb_ctx);else s_callbacks.on_tx_done(s_callbacks.cb_ctx);
  return ESP_OK;
}
esp_err_t sx1262_get_packet(sx1262_packet_t *packet) {
  assert(s_running && s_self==&s_main);
  if(!s_packets)return ESP_ERR_NOT_FOUND;
  --s_packets;*packet=(sx1262_packet_t){.buf={0xCA,0xFE},.len=2,.rssi_pkt_dbm=-70,.snr_pkt_db=5,.has_crc_error=s_corrupt};
  s_corrupt=false;return ESP_OK;
}
esp_err_t lora_session_start_owned(lora_proto_t proto,void *owner,bool *out_owned) {
  assert_worker();assert(owner!=NULL);*out_owned=false;if(s_proto!=LORA_PROTO_NONE || s_radio_owner!=NULL)return ESP_ERR_INVALID_STATE;
  if(s_start_error!=ESP_OK && !s_mesh_partial)return s_start_error;
  s_proto=proto;*out_owned=true;return s_start_error;
}
esp_err_t lora_session_stop_owned(lora_proto_t proto,void *owner) {
  assert_worker();assert(owner!=NULL && s_proto==proto);if(atomic_load(&s_fail_deinit))return ESP_FAIL;s_proto=LORA_PROTO_NONE;return ESP_OK;
}
static void wait_cleanup(void) {
  TickType_t start=xTaskGetTickCount();
  while(atomic_load(&s_power)!=0 || s_proto!=LORA_PROTO_NONE) {
    assert(xTaskGetTickCount()-start<3000);vTaskDelay(1);
  }
  assert(host_local_rf_lock(3000));host_local_rf_unlock();vTaskDelay(10);
}
int main(void) {
  s_self=&s_main;tos_lora_cfg_t config={.freq_hz=868000000,.sf=7,.bw=4,.cr=5,.tx_power_dbm=-9};
  uint8_t data[]={1,2,3};uint32_t id=99;tos_lora_packet_t packet;
  assert(tos_api_lora_rx_start(NULL)==ESP_ERR_INVALID_ARG);
  assert(tos_api_lora_rx_poll(NULL)==-ESP_ERR_INVALID_ARG);
  assert(tos_api_lora_configure(NULL)==ESP_ERR_INVALID_ARG);
  for(unsigned i=0;i<4;++i) {
    tos_lora_cfg_t bad=config;if(i==0)bad.freq_hz=0;if(i==1)bad.sf=6;if(i==2)bad.bw=0xFF;if(i==3)bad.cr=4;
    assert(tos_api_lora_configure(&bad)==ESP_ERR_INVALID_ARG);
  }
  s_caps=0;assert(tos_api_lora_configure(&config)==ESP_ERR_NOT_SUPPORTED);s_caps=TOS_CAP_RADIO_RX;
  atomic_store(&s_fail_create,true);assert(tos_api_lora_configure(&config)==ESP_ERR_NO_MEM);atomic_store(&s_fail_create,false);
  s_radio_owner=&s_foreign;unsigned deinits=s_deinits;
  assert(tos_api_lora_configure(&config)==ESP_ERR_INVALID_STATE);wait_cleanup();assert(s_deinits==deinits && s_radio_owner==&s_foreign);s_radio_owner=NULL;
  assert(tos_api_lora_configure(&config)==ESP_OK);
  assert(tos_api_lora_send(data,3)==ESP_ERR_NOT_SUPPORTED);
  assert(tos_api_lora_rx_start(&id)==ESP_OK && id!=0);
  assert(tos_api_lora_configure(&config)==ESP_ERR_INVALID_STATE);
  s_self=&s_foreign;assert(tos_api_lora_session_stop(id)==ESP_ERR_INVALID_STATE);assert(tos_api_lora_rx_poll(&packet)==-ESP_ERR_INVALID_STATE);s_self=&s_main;
  assert(tos_api_lora_rx_poll(&packet)==0 && packet.len==0);
  s_packets=2;s_corrupt=true;assert(tos_api_lora_rx_poll(&packet)==1 && packet.len==2 && packet.data[0]==0xCA && packet.rssi_dbm==-70);
  s_caps|=TOS_CAP_RADIO_TX;unsigned rx=s_rx_starts;
  assert(tos_api_lora_send(data,3)==ESP_OK && s_transmits==1 && s_rx_starts==rx+1);
  s_tx_error=ESP_ERR_TIMEOUT;assert(tos_api_lora_send(data,3)==ESP_ERR_TIMEOUT);s_tx_error=ESP_OK;
  assert(tos_api_lora_rx_poll(&packet)==0);
  atomic_store(&s_is_stopped,true);assert(tos_api_lora_session_stop(id)==ESP_OK);atomic_store(&s_is_stopped,false);wait_cleanup();
  assert(tos_api_lora_configure(&config)==ESP_OK);assert(tos_api_lora_session_stop(id)==ESP_ERR_INVALID_STATE);
  atomic_store(&s_fail_deinit,true);assert(tos_api_lora_session_stop(0)==ESP_FAIL);
  assert(tos_api_lora_configure(&config)==ESP_ERR_INVALID_STATE && s_radio_owner!=NULL && atomic_load(&s_power)==1);
  tos_api_lora_cancel_owner(&s_main);assert(tos_api_lora_session_stop(0)==ESP_ERR_INVALID_STATE);
  atomic_store(&s_fail_deinit,false);wait_cleanup();assert(s_radio_owner==NULL);
  s_start_error=ESP_FAIL;assert(tos_api_lora_configure(&config)==ESP_FAIL);wait_cleanup();assert(s_radio_owner==NULL);s_start_error=ESP_OK;
  assert(tos_api_lora_configure(&config)==ESP_OK);s_tx_timeout=true;
  assert(tos_api_lora_send(data,3)==ESP_ERR_TIMEOUT);wait_cleanup();s_tx_timeout=false;assert(atomic_load(&s_lost)>0);
  s_proto=LORA_PROTO_MESHCORE;assert(tos_api_lora_mesh_start(1,&id)==ESP_ERR_INVALID_STATE && id==0);vTaskDelay(20);assert(s_proto==LORA_PROTO_MESHCORE);s_proto=LORA_PROTO_NONE;
  assert(tos_api_lora_mesh_start(1,&id)==ESP_OK && id!=0);
  assert(tos_api_lora_configure(&config)==ESP_ERR_INVALID_STATE);
  assert(tos_api_lora_session_stop(0)==ESP_ERR_INVALID_STATE);
  assert(tos_api_lora_session_stop(id)==ESP_OK && s_proto==LORA_PROTO_NONE);
  assert(tos_api_lora_mesh_start(0,&id)==ESP_OK);tos_api_lora_cancel_owner(&s_foreign);assert(s_proto==LORA_PROTO_MESHTASTIC);
  tos_api_lora_cancel_owner(&s_main);wait_cleanup();
  s_start_error=ESP_FAIL;s_mesh_partial=true;assert(tos_api_lora_mesh_start(1,&id)==ESP_FAIL && id==0);wait_cleanup();s_start_error=ESP_OK;s_mesh_partial=false;
  assert(tos_api_lora_mesh_start(0,&id)==ESP_OK);assert(tos_api_lora_session_stop(id)==ESP_OK);
  pthread_cancel(s_tasks[0].thread);pthread_join(s_tasks[0].thread,NULL);
  puts("PASS: production LoRa raw/mesh adapters, permissions, snapshots, TX completion, owner tokens and retained cleanup");
  return 0;
}
