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

#include "test_platform.h"
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
static void *allocate(size_t);

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
static atomic_uint s_transmissions;
static unsigned s_fail_alloc;
static atomic_uint s_disable_fail,s_delete_fail,s_encoder_delete_fail,s_retries;
static unsigned s_fault;
static atomic_uint s_radio_calls, s_radio_fail;
static atomic_bool s_hold_idle, s_idle_entered;
static esp_err_t radio_result(void) {
  unsigned call = atomic_fetch_add(&s_radio_calls, 1) + 1;
  return call == atomic_load(&s_radio_fail) ? ESP_FAIL : ESP_OK;
}
static subghz_tx_config_t s_expected = {.frequency_hz = TX_DEFAULT_FREQ, .preset = CC1101_PRESET_OOK_270KHZ};
static atomic_bool s_hold_start,s_start_entered;
static atomic_int s_start_error;
static void pause_tick(void) { struct timespec t={.tv_nsec=1000000};nanosleep(&t,NULL); }
static void *allocate(size_t size) {
  unsigned call=atomic_fetch_add(&s_alloc_count,1)+1;
  if(s_fail_alloc!=0 && call==s_fail_alloc)return NULL;
  void *p=malloc(size);assert(p);return p;
}
void *heap_caps_malloc(size_t size, unsigned int caps) { assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); return allocate(size); }
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
esp_err_t cc1101_init(void) { assert(s_grant_live);
  return radio_result();
}
esp_err_t cc1101_set_preset(cc1101_preset_t preset,uint32_t frequency) {
  assert(s_grant_live);
  if (preset == CC1101_PRESET_IDLE && s_tx_entered && s_hold_idle) {
    s_idle_entered = true;
    while (s_hold_idle) pause_tick();
  }
  if (preset != CC1101_PRESET_IDLE)
    assert(frequency==s_expected.frequency_hz && preset==s_expected.preset);
  return radio_result();
}
esp_err_t cc1101_set_radio_config(const cc1101_radio_config_t *config, uint32_t freq) {
  assert(cc1101_config_validate(config) == ESP_OK);
  assert(s_expected.has_radio_config);
  assert(config->mdmcfg4 == s_expected.radio.mdmcfg4);
  assert(config->mdmcfg3 == s_expected.radio.mdmcfg3);
  assert(config->deviatn == s_expected.radio.deviatn);
  assert(config->agcctrl2 == s_expected.radio.agcctrl2);
  return cc1101_set_preset(config->preset, freq);
}
esp_err_t cc1101_get_radio_config(cc1101_radio_config_t *out) {
  return cc1101_config_from_preset(CC1101_PRESET_OOK_800KHZ, out);
}

esp_err_t cc1101_write_reg(uint8_t reg,uint8_t value) { assert(s_grant_live);
  return radio_result();
}
esp_err_t cc1101_set_pa(int power) { assert(power==10);
  return radio_result();
}
esp_err_t cc1101_enable_async_mode(uint32_t frequency) { assert(s_grant_live);
  return radio_result();
}
esp_err_t cc1101_strobe(uint8_t command) { assert(s_grant_live);
  return radio_result();
}
esp_err_t cc1101_enter_tx_mode(void) { assert(s_grant_live);
  return radio_result();
}
static atomic_bool s_led_active;
void led_set_activity(uint32_t source, bool active) {
  assert(source == LED_ACTIVITY_SUBGHZ);
  s_led_active = active;
}
void led_signal_error(void) {}
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config,rmt_channel_handle_t *out) {
  assert(s_grant_live);s_start_entered=true;while(s_hold_start)pause_tick();if(s_fault==4)return ESP_FAIL;s_channel_live=true;*out=(void *)3;return ESP_OK;
}
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config,rmt_encoder_handle_t *out) {
  if (s_fault == 5) {
    return ESP_FAIL;
  }
  s_encoder_live=true;*out=(void *)4;return ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t channel) { if(s_fault==6)return ESP_FAIL;s_enabled=true;return ESP_OK; }
esp_err_t rmt_disable(rmt_channel_handle_t channel) {
  assert(s_enabled && s_grant_live);if(s_tx_entered && s_disable_fail){--s_disable_fail;return ESP_FAIL;}s_enabled=false;return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t channel) {
  assert(!s_enabled);if(s_delete_fail){--s_delete_fail;return ESP_FAIL;}s_channel_live=false;return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {
  assert(!s_channel_live);if(s_encoder_delete_fail){--s_encoder_delete_fail;return ESP_FAIL;}s_encoder_live=false;return ESP_OK;
}
esp_err_t rmt_transmit(rmt_channel_handle_t channel,rmt_encoder_handle_t encoder,const void *data,size_t size,const rmt_transmit_config_t *config) {
  assert(s_enabled && s_grant_live && s_led_active);s_dma_pointer=(uintptr_t)data;s_dma_pending=true;s_tx_entered=true;
  const rmt_symbol_word_t *symbols=data;assert(size==2*sizeof(*symbols));
  assert(symbols[0].duration0==32767 && !symbols[0].level0);
  assert(symbols[0].duration1==17233 && !symbols[0].level1);
  assert(symbols[1].duration0==350 && symbols[1].level0);
  assert(symbols[1].duration1==700 && !symbols[1].level1);
  ++s_transmissions;
  return s_fault==7?ESP_FAIL:ESP_OK;
}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel,int wait) {
  while(s_hold_tx)pause_tick();
  if (s_fault == 8) {
    return ESP_ERR_TIMEOUT;
  }
  s_dma_pending=false;return ESP_OK;
}
static void join_worker(void) { if(s_thread_created){assert(pthread_join(s_thread,NULL)==0);s_thread_created=false;}assert(s_state==SUBGHZ_TX_IDLE && !s_grant_live && !s_led_active); }
static void *initialize_tx(void *unused) { s_start_error=subghz_tx_init();return NULL; }
static void test_stream_encoder(void);
int main(void) {
  test_stream_encoder();
  for(s_fault=1;s_fault<=6;++s_fault) { assert(subghz_tx_init()!=ESP_OK);join_worker(); }
  s_fault=0;s_hold_start=true;s_start_entered=false;
  pthread_t initializer;assert(pthread_create(&initializer,NULL,initialize_tx,NULL)==0);
  while (!s_start_entered) {
    pause_tick();
  }
  subghz_tx_stop();
  assert(s_state==SUBGHZ_TX_STOPPING && s_grant_live);s_hold_start=false;
  assert(pthread_join(initializer,NULL)==0 && s_start_error==ESP_ERR_INVALID_STATE);join_worker();
  s_fault=0;s_alloc_count=0;s_fail_alloc=1;assert(subghz_tx_init()==ESP_OK);
  int32_t timings[]={-50000,350,-700};
  assert(subghz_tx_send_raw(timings,3)==ESP_ERR_NO_MEM);subghz_tx_stop();join_worker();s_fail_alloc=0;
  for(s_fault=0;s_fault<=8;s_fault+=7) {
    s_tx_entered=false;s_alloc_count=0;assert(subghz_tx_init()==ESP_OK);
    assert(subghz_tx_send_raw(timings,3)==ESP_OK);
    while (!s_tx_entered) {
      pause_tick();
    }
    subghz_tx_stop();join_worker();
  }
  s_fault=8;s_tx_entered=false;s_hold_tx=true;s_retries=0;s_disable_fail=s_delete_fail=s_encoder_delete_fail=2;
  assert(subghz_tx_init()==ESP_OK);assert(subghz_tx_send_raw(timings,3)==ESP_OK);
  while (!s_tx_entered) {
    pause_tick();
  }
  subghz_tx_stop();
  assert(s_state==SUBGHZ_TX_STOPPING && s_grant_live && s_channel_live && s_dma_pending);
  assert(subghz_tx_send_raw(timings,3)==ESP_ERR_INVALID_STATE && subghz_tx_init()==ESP_ERR_INVALID_STATE);
  s_hold_tx=false;join_worker();assert(s_retries==6);
  s_fault=0;s_fail_alloc=2;s_alloc_count=0;s_received=0;
  assert(subghz_tx_init()==ESP_OK);assert(subghz_tx_send_raw(timings,3)==ESP_OK);
  while (!s_received) {
    pause_tick();
  }
  subghz_tx_stop();join_worker();
  s_fault=0;s_fail_alloc=0;s_expected=(subghz_tx_config_t){.frequency_hz=868350000,.preset=CC1101_PRESET_2FSK_47KHZ};
  assert(cc1101_config_from_preset(s_expected.preset, &s_expected.radio) == ESP_OK);
  assert(cc1101_config_step(&s_expected.radio, CC1101_CONFIG_LNA_GAIN, 1) == ESP_OK);
  assert(cc1101_config_step(&s_expected.radio, CC1101_CONFIG_DEVIATION, 1) == ESP_OK);
  s_expected.has_radio_config = true;
  s_tx_entered=false;assert(subghz_tx_init()==ESP_OK);
  subghz_tx_config_t invalid_radio = s_expected;
  invalid_radio.radio.deviatn = 0x88;
  assert(subghz_tx_send_configured(timings, 3, &invalid_radio) == ESP_ERR_INVALID_ARG);
  invalid_radio = s_expected;
  invalid_radio.radio.preset = CC1101_PRESET_OOK_270KHZ;
  assert(subghz_tx_send_configured(timings, 3, &invalid_radio) == ESP_ERR_INVALID_ARG);
  assert(subghz_tx_send_configured(timings,3,&s_expected)==ESP_OK);
  while(!s_tx_entered) { pause_tick(); }
  subghz_tx_stop();join_worker();
  s_expected.repeat_until_stopped = true;
  s_transmissions = 0; s_tx_entered = false;
  assert(subghz_tx_init() == ESP_OK);
  assert(subghz_tx_send_configured(timings, 3, &s_expected) == ESP_OK);
  while (s_transmissions < 3) pause_tick();
  unsigned held_radio_calls = s_radio_calls;
  unsigned held_allocations = s_alloc_count;
  unsigned held_transmissions = s_transmissions;
  while (s_transmissions < held_transmissions + 3) pause_tick();
  assert(s_radio_calls == held_radio_calls && s_alloc_count == held_allocations);
  subghz_tx_status_t held_status; subghz_tx_get_status(&held_status);
  assert(held_status.is_busy && held_status.error == ESP_OK);
  subghz_tx_stop(); join_worker();
  subghz_tx_get_status(&held_status);
  assert(!held_status.is_busy && held_status.error == ESP_OK);
  s_expected.repeat_until_stopped = false;
  // Radio startup errors must stop before RMT is created.
  for (unsigned call = 1; call <= 6; ++call) {
    s_radio_calls = 0; s_radio_fail = call; s_tx_entered = false;
    assert(subghz_tx_init() == ESP_FAIL);
    join_worker();
    subghz_tx_status_t status; subghz_tx_get_status(&status);
    assert(status.error == ESP_FAIL && !status.is_busy && !s_tx_entered);
  }
  s_radio_fail = 0;
  // Each configured-frame register/state failure prevents transmission.
  for (unsigned call = 1; call <= 7; ++call) {
    s_radio_calls = 0; s_tx_entered = false;
    assert(subghz_tx_init() == ESP_OK);
    s_radio_fail = atomic_load(&s_radio_calls) + call;
    assert(subghz_tx_send_configured(timings, 3, &s_expected) == ESP_OK);
    join_worker();
    subghz_tx_status_t status; subghz_tx_get_status(&status);
    assert(status.error == ESP_FAIL && !status.is_busy && !s_tx_entered);
    s_radio_fail = 0;
  }
  // Enqueue and RMT completion alone must not report success before radio IDLE.
  s_tx_entered = false; s_hold_tx = true; s_hold_idle = true; s_idle_entered = false;
  assert(subghz_tx_init() == ESP_OK);
  assert(subghz_tx_send_configured(timings, 3, &s_expected) == ESP_OK);
  while (!s_tx_entered) pause_tick();
  subghz_tx_status_t status; subghz_tx_get_status(&status);
  assert(status.is_busy && status.error == ESP_OK);
  s_hold_tx = false;
  while (!s_idle_entered) pause_tick();
  subghz_tx_get_status(&status);
  assert(status.is_busy && status.error == ESP_OK && s_grant_live);
  s_hold_idle = false;
  do { subghz_tx_get_status(&status); pause_tick(); } while (status.is_busy);
  assert(status.error == ESP_OK);
  subghz_tx_stop(); join_worker();
  // An IDLE failure after DMA completion is still a failed job.
  s_tx_entered = false;
  assert(subghz_tx_init() == ESP_OK);
  s_radio_fail = atomic_load(&s_radio_calls) + 8;
  assert(subghz_tx_send_configured(timings, 3, &s_expected) == ESP_OK);
  join_worker(); subghz_tx_get_status(&status);
  assert(s_tx_entered && !status.is_busy && status.error == ESP_FAIL);
  s_radio_fail = 0;
  int32_t invalid[]={INT32_MIN};size_t words=0;
  subghz_tx_item_t bad={.timings=invalid,.count=1};assert(make_symbols(&bad,&words)==NULL);
  puts("PASS: long gaps and queued frequency/preset; real native TX worker, startup/configuration errors, real completion, in-flight stop, RMT cleanup retries, allocation and INT32_MIN");
}

esp_err_t rmt_new_simple_encoder(const rmt_simple_encoder_config_t *config, rmt_encoder_handle_t *out) { return ESP_FAIL; }
void subghz_raw_file_acquire(subghz_raw_file_t *file) { assert(!file); }
void subghz_raw_file_release(subghz_raw_file_t *file) { assert(!file); }
esp_err_t subghz_raw_reader_open(const subghz_raw_file_t *file, subghz_raw_reader_t *reader) { return ESP_FAIL; }
static size_t s_file_cursor, s_file_count;
esp_err_t subghz_raw_reader_next(subghz_raw_reader_t *reader, int32_t *pulse, bool *eof) {
  *eof = s_file_cursor == s_file_count;
  if (!*eof) { *pulse = (s_file_cursor & 1) ? -700 : 350; ++s_file_cursor; }
  return ESP_OK;
}
esp_err_t subghz_raw_reader_rewind(subghz_raw_reader_t *reader) { s_file_cursor = 0; return ESP_OK; }
void subghz_raw_reader_close(subghz_raw_reader_t *reader) { assert(!reader->file); }

static void test_stream_encoder(void) {
  s_state = SUBGHZ_TX_RUNNING;
  for (unsigned repeat = 0; repeat < 2; ++repeat) {
    subghz_tx_stream_t stream = {.repeat = repeat};
    stream.symbols = calloc(TX_STREAM_SYMBOLS, sizeof(*stream.symbols));
    assert(stream.symbols);
    s_file_cursor = 0; s_file_count = 131073;
    size_t checked = 0;
    bool done = false;
    while (!done && checked < 3 * s_file_count) {
      assert(fill_stream(&stream) == ESP_OK);
      rmt_symbol_word_t output[64];
      size_t count = stream_encode(NULL, 0, 0, 64, output, &done, &stream);
      for (size_t i=0; i<count; ++i) {
        for (unsigned half=0; half<2; ++half) {
          unsigned duration = half ? output[i].duration1 : output[i].duration0;
          bool level = half ? output[i].level1 : output[i].level0;
          if (!duration) { assert(!repeat && done && checked == s_file_count); break; }
          size_t index = checked % s_file_count;
          assert(duration == ((index & 1) ? 700 : 350));
          assert(level == !(index & 1));
          ++checked;
        }
      }
      assert(!atomic_load(&stream.underrun));
    }
    if (!repeat) assert(done && checked == s_file_count);
    else assert(!done && checked >= 3*s_file_count);
    close_stream(&stream);
  }
  subghz_tx_stream_t empty = {0};
  rmt_symbol_word_t output; bool done = false;
  assert(stream_encode(NULL,0,0,1,&output,&done,&empty)==0);
  assert(done && atomic_load(&empty.underrun));
  s_state = SUBGHZ_TX_IDLE;
  puts("PASS: RAW stream encoder >131072 pulses, ring wraps, odd tail, contiguous repeats and explicit underrun");
}
