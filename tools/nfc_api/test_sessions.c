#define _POSIX_C_SOURCE 200809L
#include "tos_api_nfc_session.h"
#include "nfc_listener.h"
#include "highboy_nfc.h"
#include "rf_frontend.h"
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
static struct test_task s_main, s_foreign, s_tasks[2];
static unsigned s_task_count;
static pthread_mutex_t s_gate = PTHREAD_MUTEX_INITIALIZER;
static atomic_bool s_is_stopped, s_fail_off, s_fail_config, s_fail_step, s_is_initialized;
static atomic_bool s_fail_create;
static atomic_uint s_power, s_lost, s_inits;
static uint32_t s_caps = TOS_CAP_RADIO_TX;
static const uint8_t UID[] = {0xDE,0xAD,0xBE,0xEF};
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
  (void)name;assert(stack==4096 && priority==5 && core==0);
  if(atomic_load(&s_fail_create))return 0;
  assert(s_task_count<2);struct test_task *task=&s_tasks[s_task_count++];
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
esp_err_t power_manager_no_sleep_acquire(void) { atomic_fetch_add(&s_power,1);return ESP_OK; }
esp_err_t power_manager_no_sleep_release(void) { assert(atomic_fetch_sub(&s_power,1)>0);return ESP_OK; }
esp_err_t highboy_nfc_init(const highboy_nfc_config_t *config) {
  (void)config;assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));atomic_store(&s_is_initialized,true);return ESP_OK;
}
esp_err_t highboy_nfc_field_off(void) {
  assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));return atomic_load(&s_fail_off)?ESP_FAIL:ESP_OK;
}
esp_err_t highboy_nfc_ping(void *out) { (void)out;return ESP_OK; }
void highboy_nfc_deinit(void) { assert(!atomic_load(&s_fail_off));atomic_store(&s_is_initialized,false); }
bool highboy_nfc_is_initialized(void) { return atomic_load(&s_is_initialized); }
void mfc_emu_card_data_init(mfc_emu_card_data_t *out,const nfc_iso14443a_data_t *card,int type) {
  assert(type==1);memset(out,0,sizeof(*out));memcpy(out->uid,card->uid,card->uid_len);
}
hb_nfc_err_t mfc_emu_init(const mfc_emu_card_data_t *card) {
  assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));assert(memcmp(card->uid,UID,4)==0);
  atomic_fetch_add(&s_inits,1);return HB_NFC_OK;
}
hb_nfc_err_t mfc_emu_configure_target(void) { return atomic_load(&s_fail_config)?HB_NFC_ERR_INTERNAL:HB_NFC_OK; }
hb_nfc_err_t mfc_emu_start(void) { return HB_NFC_OK; }
int mfc_emu_run_step(void) { assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));return atomic_load(&s_fail_step)?MFC_EMU_STATE_ERROR:1; }
void mfc_emu_stop(void) { assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ)); }
static void wait_idle(void) {
  TickType_t start=xTaskGetTickCount();
  while(atomic_load(&s_power)!=0) { assert(xTaskGetTickCount()-start<5000);vTaskDelay(1); }
  vTaskDelay(20);
}
int main(void) {
  s_self=&s_main;uint32_t id=55;
  assert(tos_api_nfc_session_start(UID,4,NULL)==ESP_ERR_INVALID_ARG);
  assert(tos_api_nfc_session_start(NULL,4,&id)==ESP_ERR_INVALID_ARG && id==0);
  assert(tos_api_nfc_session_start(UID,7,&id)==ESP_ERR_INVALID_ARG);
  s_caps=0;assert(tos_api_nfc_session_start(UID,4,&id)==ESP_ERR_NOT_SUPPORTED);s_caps=TOS_CAP_RADIO_TX;
  atomic_store(&s_fail_create,true);assert(tos_api_nfc_session_start(UID,4,&id)==ESP_ERR_NO_MEM && id==0);atomic_store(&s_fail_create,false);
  assert(rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ)==ESP_OK);
  unsigned inits=atomic_load(&s_inits);
  assert(tos_api_nfc_session_start(UID,4,&id)==ESP_ERR_INVALID_STATE && id==0);
  assert(atomic_load(&s_inits)==inits && atomic_load(&s_power)==0);
  assert(rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ)==ESP_OK);
  assert(tos_api_nfc_session_start(UID,4,&id)==ESP_OK && id!=0);
  assert(!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));
  mfc_emu_card_data_t card={0};memcpy(card.uid,UID,4);
  assert(nfc_listener_start_emu(&card)!=HB_NFC_OK);
  assert(atomic_load(&s_inits)==inits+1);
  uint32_t ignored;assert(tos_api_nfc_session_start(UID,4,&ignored)==ESP_ERR_INVALID_STATE && ignored==0);
  s_self=&s_foreign;assert(tos_api_nfc_session_stop(id)==ESP_ERR_INVALID_STATE);s_self=&s_main;
  tos_api_nfc_session_cancel_owner(&s_foreign);assert(!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));
  atomic_store(&s_is_stopped,true);assert(tos_api_nfc_session_stop(id)==ESP_OK);atomic_store(&s_is_stopped,false);
  assert(rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ));wait_idle();
  uint32_t old=id;assert(tos_api_nfc_session_start(UID,4,&id)==ESP_OK && id!=old);
  assert(tos_api_nfc_session_stop(old)==ESP_ERR_INVALID_STATE);
  atomic_store(&s_fail_off,true);assert(tos_api_nfc_session_stop(id)==ESP_ERR_TIMEOUT);
  assert(!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ) && atomic_load(&s_power)==1);
  assert(tos_api_nfc_session_start(UID,4,&ignored)==ESP_ERR_INVALID_STATE);
  tos_api_nfc_session_cancel_owner(&s_main);atomic_store(&s_fail_off,false);wait_idle();
  assert(tos_api_nfc_session_stop(id)==ESP_ERR_INVALID_STATE);
  assert(tos_api_nfc_session_start(UID,4,&id)==ESP_OK);
  atomic_store(&s_fail_step,true);wait_idle();assert(atomic_load(&s_lost)>0);atomic_store(&s_fail_step,false);
  atomic_store(&s_fail_config,true);assert(tos_api_nfc_session_start(UID,4,&id)==ESP_FAIL && id==0);wait_idle();atomic_store(&s_fail_config,false);
  assert(nfc_listener_start_emu(&card)==HB_NFC_OK && nfc_listener_is_running());
  assert(tos_api_nfc_session_start(UID,4,&id)==ESP_ERR_INVALID_STATE && id==0);
  assert(nfc_listener_is_running());nfc_listener_stop();assert(!nfc_listener_is_active());wait_idle();
  assert(nfc_listener_start_emu(&card)==HB_NFC_OK);
  atomic_store(&s_fail_off,true);nfc_listener_stop();assert(nfc_listener_is_active());
  assert(nfc_listener_start_emu(&card)!=HB_NFC_OK);
  atomic_store(&s_fail_off,false);wait_idle();assert(!nfc_listener_is_active());
  assert(tos_api_nfc_session_start(UID,4,&id)==ESP_OK);
  tos_api_nfc_session_cancel_owner(&s_main);wait_idle();
  for(unsigned i=0;i<s_task_count;++i) { pthread_cancel(s_tasks[i].thread);pthread_join(s_tasks[i].thread,NULL); }
  puts("PASS: production NFC app/listener ownership, failure retention, task reuse and cancellation");
  return 0;
}
