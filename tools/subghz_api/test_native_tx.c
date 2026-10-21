#include "test_platform.h"
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
static void *allocate(size_t);
static void *allocate_zero(size_t,size_t);
static void release(void *);
#define malloc allocate
#define calloc allocate_zero
#define free release
#include "subghz_transmitter.c"
#undef malloc
#undef calloc
#undef free

typedef struct { pthread_mutex_t lock;pthread_cond_t ready;unsigned count; } sem_t;
typedef struct { pthread_mutex_t lock;size_t size;unsigned head,count;uint8_t *items; } queue_t;
typedef struct { void (*entry)(void *);void *context; } task_t;
static pthread_t s_thread;
static bool s_thread_created;
static atomic_bool s_grant_live,s_channel_live,s_enabled,s_encoder_live,s_tx_entered,s_hold_tx,s_dma_pending;
static atomic_uintptr_t s_dma_pointer;
static atomic_uint s_alloc_count,s_received;
static unsigned s_fail_alloc;
static atomic_uint s_disable_fail,s_delete_fail,s_encoder_delete_fail,s_retries;
static unsigned s_fault;
static atomic_bool s_hold_start,s_start_entered;
static atomic_int s_start_error;
static void pause_tick(void) { struct timespec t={.tv_nsec=1000000};nanosleep(&t,NULL); }
static void *allocate(size_t size) {
  unsigned call=atomic_fetch_add(&s_alloc_count,1)+1;
  if(s_fail_alloc!=0 && call==s_fail_alloc)return NULL;
  void *p=malloc(size);assert(p);return p;
}
static void *allocate_zero(size_t count,size_t size) { void *p=allocate(count*size);if(p)memset(p,0,count*size);return p; }
static void release(void *p) {
  if((uintptr_t)p==atomic_load(&s_dma_pointer) && p!=NULL)
    assert(!s_dma_pending || (!s_channel_live && !s_encoder_live && !s_enabled));
  free(p);
}
SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  if(s_fault==1)return NULL;
  sem_t *s=calloc(1,sizeof(*s));assert(s);pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->ready,NULL);return s;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle,TickType_t wait) {
  sem_t *s=handle;pthread_mutex_lock(&s->lock);while(!s->count)pthread_cond_wait(&s->ready,&s->lock);
  --s->count;pthread_mutex_unlock(&s->lock);return pdTRUE;
}
void xSemaphoreGive(SemaphoreHandle_t handle) {
  sem_t *s=handle;pthread_mutex_lock(&s->lock);++s->count;pthread_cond_signal(&s->ready);pthread_mutex_unlock(&s->lock);
}
void vSemaphoreDelete(SemaphoreHandle_t handle) {
  sem_t *s=handle;pthread_mutex_destroy(&s->lock);pthread_cond_destroy(&s->ready);free(s);
}
QueueHandle_t xQueueCreate(unsigned count,size_t size) {
  if(s_fault==2)return NULL;
  queue_t *q=calloc(1,sizeof(*q));assert(q);q->size=size;q->items=calloc(count,size);assert(q->items);pthread_mutex_init(&q->lock,NULL);return q;
}
BaseType_t xQueueSend(QueueHandle_t handle,const void *item,TickType_t wait) {
  queue_t *q=handle;pthread_mutex_lock(&q->lock);
  bool ok=q->count<TX_QUEUE_SIZE;
  if(ok){memcpy(q->items+((q->head+q->count)%TX_QUEUE_SIZE)*q->size,item,q->size);++q->count;}
  pthread_mutex_unlock(&q->lock);return ok;
}
BaseType_t xQueueReceive(QueueHandle_t handle,void *out,TickType_t wait) {
  queue_t *q=handle;
  for(unsigned i=0;i<(wait?10u:1u);++i) {
    pthread_mutex_lock(&q->lock);
    if(q->count){memcpy(out,q->items+q->head*q->size,q->size);q->head=(q->head+1)%TX_QUEUE_SIZE;--q->count;pthread_mutex_unlock(&q->lock);++s_received;return pdTRUE;}
    pthread_mutex_unlock(&q->lock);if(wait)pause_tick();
  }
  return pdFALSE;
}
void vQueueDelete(QueueHandle_t handle) {
  assert(!s_channel_live && !s_encoder_live);queue_t *q=handle;assert(q->count==0);pthread_mutex_destroy(&q->lock);free(q->items);free(q);
}
static void *task_run(void *context) { task_t *task=context;task->entry(task->context);free(task);return NULL; }
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,uint32_t stack,void *context,int priority,TaskHandle_t *handle,int core) {
  assert(priority==SYS_PRIO_SERVICE_HI && core==SYS_CORE_RADIO && s_grant_live);
  if(s_fault==3)return pdFALSE;
  task_t *task=malloc(sizeof(*task));assert(task);*task=(task_t){.entry=entry,.context=context};
  assert(pthread_create(&s_thread,NULL,task_run,task)==0);s_thread_created=true;return pdPASS;
}
void vTaskDelete(TaskHandle_t handle) { assert(!s_grant_live && !s_channel_live && !s_encoder_live); }
void vTaskDelay(TickType_t ticks) {
  if(ticks==TX_CLEANUP_RETRY_MS){assert(s_grant_live);++s_retries;}
  pause_tick();
}
esp_err_t resource_acquire(const res_request_t *request,res_handle_t *out) {
  assert(!s_grant_live && request->owner_kind==RES_OWNER_UI);s_grant_live=true;*out=42;return ESP_OK;
}
esp_err_t resource_release(res_handle_t grant) { assert(s_grant_live && !s_channel_live && !s_encoder_live);s_grant_live=false;return ESP_OK; }
void cc1101_enable_async_mode(uint32_t frequency) { assert(s_grant_live); }
void cc1101_strobe(uint8_t command) { assert(s_grant_live); }
void cc1101_enter_tx_mode(void) { assert(s_grant_live); }
void led_signal_error(void) {}
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config,rmt_channel_handle_t *out) {
  assert(s_grant_live);s_start_entered=true;while(s_hold_start)pause_tick();if(s_fault==4)return ESP_FAIL;s_channel_live=true;*out=(void *)3;return ESP_OK;
}
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config,rmt_encoder_handle_t *out) {
  if(s_fault==5)return ESP_FAIL;s_encoder_live=true;*out=(void *)4;return ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t channel) { if(s_fault==6)return ESP_FAIL;s_enabled=true;return ESP_OK; }
esp_err_t rmt_disable(rmt_channel_handle_t channel) {
  assert(s_enabled && s_grant_live);if(s_disable_fail){--s_disable_fail;return ESP_FAIL;}s_enabled=false;return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t channel) {
  assert(!s_enabled);if(s_delete_fail){--s_delete_fail;return ESP_FAIL;}s_channel_live=false;return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {
  assert(!s_channel_live);if(s_encoder_delete_fail){--s_encoder_delete_fail;return ESP_FAIL;}s_encoder_live=false;return ESP_OK;
}
esp_err_t rmt_transmit(rmt_channel_handle_t channel,rmt_encoder_handle_t encoder,const void *data,size_t size,const rmt_transmit_config_t *config) {
  assert(s_enabled && s_grant_live);s_dma_pointer=(uintptr_t)data;s_dma_pending=true;s_tx_entered=true;
  const rmt_symbol_word_t *symbols=data;assert(symbols[0].duration0==32767 && !symbols[0].level0);
  return s_fault==7?ESP_FAIL:ESP_OK;
}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel,int wait) {
  while(s_hold_tx)pause_tick();
  if(s_fault==8)return ESP_ERR_TIMEOUT;s_dma_pending=false;return ESP_OK;
}
static void join_worker(void) { if(s_thread_created){assert(pthread_join(s_thread,NULL)==0);s_thread_created=false;}assert(s_state==SUBGHZ_TX_IDLE && !s_grant_live); }
static void *initialize_tx(void *unused) { s_start_error=subghz_tx_init();return NULL; }
int main(void) {
  for(s_fault=1;s_fault<=6;++s_fault) { assert(subghz_tx_init()!=ESP_OK);join_worker(); }
  s_fault=0;s_hold_start=true;s_start_entered=false;
  pthread_t initializer;assert(pthread_create(&initializer,NULL,initialize_tx,NULL)==0);
  while(!s_start_entered)pause_tick();subghz_tx_stop();
  assert(s_state==SUBGHZ_TX_STOPPING && s_grant_live);s_hold_start=false;
  assert(pthread_join(initializer,NULL)==0 && s_start_error==ESP_ERR_INVALID_STATE);join_worker();
  s_fault=0;s_alloc_count=0;s_fail_alloc=1;assert(subghz_tx_init()==ESP_OK);
  int32_t timings[]={INT32_MIN,350,-700};
  assert(subghz_tx_send_raw(timings,3)==ESP_ERR_NO_MEM);subghz_tx_stop();join_worker();s_fail_alloc=0;
  for(s_fault=0;s_fault<=8;s_fault+=7) {
    s_tx_entered=false;s_alloc_count=0;assert(subghz_tx_init()==ESP_OK);
    assert(subghz_tx_send_raw(timings,3)==ESP_OK);
    while(!s_tx_entered)pause_tick();subghz_tx_stop();join_worker();
  }
  s_fault=8;s_tx_entered=false;s_hold_tx=true;s_retries=0;s_disable_fail=s_delete_fail=s_encoder_delete_fail=2;
  assert(subghz_tx_init()==ESP_OK);assert(subghz_tx_send_raw(timings,3)==ESP_OK);
  while(!s_tx_entered)pause_tick();subghz_tx_stop();
  assert(s_state==SUBGHZ_TX_STOPPING && s_grant_live && s_channel_live && s_dma_pending);
  assert(subghz_tx_send_raw(timings,3)==ESP_ERR_INVALID_STATE && subghz_tx_init()==ESP_ERR_INVALID_STATE);
  s_hold_tx=false;join_worker();assert(s_retries==6);
  s_fault=0;s_fail_alloc=2;s_alloc_count=0;s_received=0;
  assert(subghz_tx_init()==ESP_OK);assert(subghz_tx_send_raw(timings,3)==ESP_OK);
  while(!s_received)pause_tick();subghz_tx_stop();join_worker();
  puts("PASS: real native TX worker, startup errors, in-flight stop, RMT cleanup retries, allocation and INT32_MIN");
}
