// SPDX-License-Identifier: GPL-3.0-or-later
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
#include "resource_mgr.h"
#include "cc1101_lease.h"
#include "tos_api.h"
#include "host_link_local_rf.h"
static FILE *open_capture(const char *,const char *);
static void *allocate(size_t);
static void *allocate_zero(size_t,size_t);
static void release(void *);
static size_t copy_text(char *out,const char *text,size_t size) {
  size_t length=strlen(text),n=length<size-1?length:size-1;
  memcpy(out,text,n);out[n]=0;return length;
}
#define malloc allocate
#define calloc allocate_zero
#define free release
#define strlcpy copy_text
#define fopen open_capture
#include "tos_api_subghz.c"
#undef fopen
#undef malloc
#undef calloc
#undef free

typedef enum { NORMAL, FAIL_CHANNEL, FAIL_QUEUE, FAIL_ALLOC, FAIL_CALLBACK, FAIL_ENABLE,
  FAIL_RECEIVE, RX_TIMEOUT, RX_EMPTY, RX_TOO_LARGE, RX_BAD_POINTER, RX_UNKNOWN,
  FAIL_RECOVER, FAIL_PRESET, FAIL_POWER, FAIL_POWER_RELEASE, FAIL_DISABLE, FAIL_DELETE,
  FAIL_ENCODER, FAIL_ENCODER_DELETE, FAIL_TRANSMIT, FAIL_TX_WAIT, FAIL_IDLE,
  FAIL_TUNE, FAIL_RSSI, FAIL_SWEEP_CLEANUP } fault_t;
static _Atomic(fault_t) s_fault;
static unsigned s_allocations,s_fail_allocation,s_callbacks;
static atomic_uint s_calls;
static atomic_bool s_allowed=true,s_stopped,s_stop_locked,s_lock_timeout,s_ready=true;
static bool s_gate;
static atomic_bool s_pause_session;
static pthread_mutex_t s_gate_mutex=PTHREAD_MUTEX_INITIALIZER;
static atomic_bool s_resource_busy;
static bool s_resource_live,s_channel_live,s_channel_enabled,s_encoder_live,s_queue_live;
static unsigned s_power;
static _Thread_local uintptr_t s_task=1;
static unsigned s_tunes,s_reads,s_settles,s_sample_delays,s_yields,s_sweep_phase;
static uint32_t s_expected_start=433420000,s_expected_step=12500;
static const rmt_rx_event_callbacks_t *s_callbacks_ptr;
static rmt_rx_event_callbacks_t s_callbacks_copy;
static TaskHandle_t worker(void) { return (void *)2; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)s_task; }
static void assert_worker(void) { assert(xTaskGetCurrentTaskHandle()==worker() && s_resource_live && s_gate); }
static void *allocate(size_t size) {
  ++s_allocations;
  if(s_fault==FAIL_ALLOC && s_allocations==s_fail_allocation) return NULL;
  void *p=malloc(size);assert(p!=NULL);return p;
}
static void *allocate_zero(size_t count,size_t size) {
  void *p=allocate(count*size);if(p!=NULL) memset(p,0,count*size);return p;
}
static void release(void *p) {
  if(p==NULL)return;
  if(p==s_symbols || p==s_pulses) assert(!s_channel_live && !s_channel_enabled && !s_encoder_live);
  free(p);
}
void *heap_caps_aligned_alloc(size_t alignment,size_t size,unsigned caps) { return allocate(size); }
void *heap_caps_malloc(size_t size,unsigned caps) { return allocate(size); }
static int should_stop(void) { return s_stopped; }
static const tos_api_t s_api={.should_stop=should_stop};
const tos_api_t *tos_api_get(void) { return &s_api; }
static atomic_uint s_lost_events,s_clear_events;
void tos_app_ctx_signal_resource_lost(void *task) { assert(task==(void *)1 || task==(void *)4);++s_lost_events; }
void tos_app_ctx_clear_resource_lost(void) { ++s_clear_events; }
bool tos_app_cap_check(uint32_t cap) { assert(cap==TOS_CAP_RADIO_RX || cap==TOS_CAP_RADIO_TX || cap==(TOS_CAP_RADIO_TX|TOS_CAP_FS_READ));return s_allowed; }
bool host_local_rf_is_ready(void) { return s_ready; }
bool host_local_rf_lock(uint32_t timeout) {
  if(s_lock_timeout || (s_task==3 && s_pause_session))return false;
  assert(pthread_mutex_lock(&s_gate_mutex)==0);assert(!s_gate);s_gate=true;
  if (s_stop_locked) {
    s_stopped = true;
  }
  return true;
}
void host_local_rf_unlock(void) { assert(s_gate);s_gate=false;assert(pthread_mutex_unlock(&s_gate_mutex)==0); }
esp_err_t host_local_rf_call(host_local_rf_cb_t callback,void *context) {
  assert(s_gate && (s_task==1 || s_task==3));++s_calls;uintptr_t caller=s_task;s_task=2;
  esp_err_t error=callback(context);s_task=caller;return error;
}
esp_err_t resource_acquire(const res_request_t *request,res_handle_t *out) {
  assert(request->id==RES_SUBGHZ && request->owner_kind==RES_OWNER_SYSTEM && !request->allow_preempt);
  if(s_resource_busy || s_resource_live)return ESP_ERR_INVALID_STATE;
  s_resource_live=true;*out=42;return ESP_OK;
}
esp_err_t resource_release(res_handle_t grant) {
  assert_worker();assert(grant==42 && !s_channel_live && !s_queue_live && !s_encoder_live && s_power==0);
  s_resource_live=false;return ESP_OK;
}
esp_err_t power_manager_no_sleep_acquire(void) {
  assert_worker();if(s_fault==FAIL_POWER)return ESP_FAIL;assert(s_power==0);++s_power;return ESP_OK;
}
esp_err_t power_manager_no_sleep_release(void) {
  assert_worker();assert(s_power==1 && !s_channel_live && !s_encoder_live);
  if (s_fault == FAIL_POWER_RELEASE) {
    return ESP_FAIL;
  }
  --s_power;
  return ESP_OK;
}
esp_err_t cc1101_recover(void) { assert_worker();return s_fault==FAIL_RECOVER?ESP_FAIL:ESP_OK; }
void cc1101_set_preset(cc1101_preset_t preset,uint32_t freq) {
  assert_worker();
#ifdef SUBGHZ_TEST_SESSIONS
  s_tunes=s_reads=s_settles=s_sample_delays=s_yields=0;
#endif
  if(s_fault==FAIL_PRESET)cc1101_lease_record_error(ESP_ERR_TIMEOUT);
}
void cc1101_strobe(uint8_t command) {
  assert_worker();assert(command==CC1101_SIDLE || command==CC1101_SRX);
  if(command==CC1101_SIDLE)s_sweep_phase=0;else {assert(s_sweep_phase==1);s_sweep_phase=2;}if(s_fault==FAIL_IDLE || (s_fault==FAIL_SWEEP_CLEANUP && s_reads==240))cc1101_lease_record_error(ESP_ERR_TIMEOUT);
}
void cc1101_set_frequency(uint32_t frequency) {
  assert_worker();assert(s_power==1);
  assert(s_sweep_phase==0);s_sweep_phase=1;
  assert(frequency==s_expected_start+s_tunes*s_expected_step);++s_tunes;
  if(s_fault==FAIL_TUNE && s_tunes==17)cc1101_lease_record_error(ESP_FAIL);
}
uint8_t cc1101_read_reg(uint8_t reg) {
  assert_worker();assert(s_sweep_phase==3 && reg==(CC1101_RSSI|0x40));
  const uint8_t values[]={30,32,31};uint8_t value=values[s_reads%3];++s_reads;
  if(s_fault==FAIL_RSSI && s_reads==32)cc1101_lease_record_error(ESP_FAIL);
  return value;
}
float cc1101_convert_rssi(uint8_t raw) { return (float)raw-100; }
void ets_delay_us(uint32_t delay) {
  assert_worker();if(delay==400){assert(s_sweep_phase==2);s_sweep_phase=3;++s_settles;}
  else {assert(s_sweep_phase==3 && delay==20);++s_sample_delays;}
}
void vTaskDelay(TickType_t ticks) {
  if(s_task!=2) {
    assert(ticks==SUBGHZ_SESSION_RETRY_MS);
    struct timespec delay={.tv_nsec=1000000};nanosleep(&delay,NULL);return;
  }
  assert_worker();assert(s_sweep_phase==3 && ticks==1 && s_reads%3==0);++s_yields;
}
void cc1101_enter_tx_mode(void) { assert_worker(); }
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config,rmt_channel_handle_t *out) {
  assert_worker();assert(!s_channel_live && config->flags.with_dma && config->gpio_num==GPIO_CC1101_GDO0_PIN);
  if (s_fault == FAIL_CHANNEL) {
    return ESP_FAIL;
  }
  s_channel_live=true;*out=(void *)3;return ESP_OK;
}
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config,rmt_channel_handle_t *out) {
  assert_worker();assert(!s_channel_live);if(s_fault==FAIL_CHANNEL)return ESP_FAIL;
  s_channel_live=true;*out=(void *)3;return ESP_OK;
}
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t channel,const rmt_rx_event_callbacks_t *callbacks,void *context) {
  assert(s_channel_live && s_queue_live && context==s_queue);s_callbacks_copy=*callbacks;s_callbacks_ptr=&s_callbacks_copy;
  return s_fault==FAIL_CALLBACK?ESP_FAIL:ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t channel) {
  assert_worker();if(s_fault==FAIL_ENABLE)return ESP_FAIL;s_channel_enabled=true;return ESP_OK;
}
esp_err_t rmt_disable(rmt_channel_handle_t channel) {
  assert_worker();assert(s_channel_enabled);
  if(s_fault==FAIL_DISABLE) {
    if(s_queue_live){rmt_rx_done_event_data_t event={0};s_callbacks_ptr->on_recv_done(s_channel,&event,s_queue);}
    return ESP_FAIL;
  }
  s_channel_enabled=false;return ESP_OK;
}
esp_err_t rmt_del_channel(rmt_channel_handle_t channel) {
  assert_worker();assert(!s_channel_enabled);if(s_fault==FAIL_DELETE)return ESP_FAIL;
  s_channel_live=false;return ESP_OK;
}
QueueHandle_t xQueueCreate(unsigned count,size_t size) {
  if (s_fault == FAIL_QUEUE) {
    return NULL;
  }
  s_queue_live=true;return (void *)4;
}
void vQueueDelete(QueueHandle_t queue) { assert(!s_channel_live && !s_channel_enabled);s_queue_live=false; }
BaseType_t xQueueSendFromISR(QueueHandle_t queue,const void *event,BaseType_t *wake) {
  assert(s_queue_live);++s_callbacks;return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue,void *output,TickType_t timeout) {
  assert_worker();assert(s_channel_enabled && timeout>0 && timeout<=1000);
#ifdef SUBGHZ_TEST_SESSIONS
  struct timespec delay={.tv_nsec=1000000};nanosleep(&delay,NULL);
#endif
  if(s_fault==RX_TIMEOUT)return pdFALSE;
  for(unsigned i=0;i<3;++i)s_symbols[i]=(rmt_symbol_word_t){.duration0=350,.level0=true,.duration1=700,.level1=false};
  rmt_rx_done_event_data_t event={.received_symbols=s_symbols,.num_symbols=3};
  if(s_fault==RX_EMPTY)event.num_symbols=0;
  if(s_fault==RX_TOO_LARGE)event.num_symbols=SUBGHZ_SYMBOLS_MAX+1;
  if(s_fault==RX_BAD_POINTER)event.received_symbols=NULL;
  s_callbacks_ptr->on_recv_done(s_channel,&event,s_queue);
  memcpy(output,&event,sizeof(event));return pdTRUE;
}
esp_err_t rmt_receive(rmt_channel_handle_t channel,void *buffer,size_t size,const rmt_receive_config_t *config) {
  assert_worker();assert(s_channel_enabled && buffer==s_symbols);return s_fault==FAIL_RECEIVE?ESP_FAIL:ESP_OK;
}
bool subghz_protocol_registry_decode_all(const int32_t *pulses,size_t count,subghz_data_t *out) {
  assert(count==6 && pulses[0]==350 && pulses[1]==-700);
  *out=(subghz_data_t){.protocol_name="CAME",.raw_value=42,.serial=7,.bit_count=24,.btn=3};
  return s_fault!=RX_UNKNOWN;
}
esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config,rmt_encoder_handle_t *out) {
  if (s_fault == FAIL_ENCODER) {
    return ESP_FAIL;
  }
  s_encoder_live=true;*out=(void *)5;return ESP_OK;
}
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {
  assert(!s_channel_live && !s_channel_enabled);if(s_fault==FAIL_ENCODER_DELETE)return ESP_FAIL;
  s_encoder_live=false;return ESP_OK;
}
esp_err_t rmt_transmit(rmt_channel_handle_t channel,rmt_encoder_handle_t encoder,const void *data,size_t size,const rmt_transmit_config_t *config) {
  assert(s_channel_enabled && s_encoder_live && data==s_symbols && size==2*sizeof(*s_symbols));
  assert(s_symbols[0].duration0==350 && s_symbols[0].level0 && s_symbols[0].duration1==700 && !s_symbols[0].level1);
  assert(s_symbols[1].duration0==350 && s_symbols[1].duration1==0);
  return s_fault==FAIL_TRANSMIT?ESP_FAIL:ESP_OK;
}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel,int timeout) {
  assert(timeout==SUBGHZ_TX_WAIT_MS);return s_fault==FAIL_TX_WAIT?ESP_ERR_TIMEOUT:ESP_OK;
}
static void clean(void) {
#ifdef SUBGHZ_TEST_SESSIONS
  assert(pthread_mutex_lock(&s_gate_mutex)==0);
  assert(pthread_mutex_unlock(&s_gate_mutex)==0);
#endif
  assert(!s_resource_live && !s_channel_live && !s_encoder_live && !s_queue_live && !s_gate && s_power==0);
  assert(s_symbols==NULL && s_pulses==NULL);
}
static void receive_case(fault_t fault,bool retained) {
  s_fault=fault;s_allocations=0;tos_subghz_signal_t out;
  esp_err_t error=tos_api_subghz_receive(&out,433920000,CC1101_PRESET_OOK_800KHZ,500);
  if(fault==NORMAL || fault==RX_UNKNOWN) {
    assert(error==ESP_OK && out.freq_hz==433920000);
    assert(out.decoded==(fault==NORMAL));
    if(out.decoded)assert(strcmp(out.protocol,"CAME")==0 && out.value==42 && out.serial==7);
  } else { assert(error!=ESP_OK && out.freq_hz==0); }
  if(retained) {
    assert(s_resource_live && !cc1101_lease_enter());
    s_task=2;assert(!cc1101_lease_enter());s_task=1;
    if(s_channel_live)assert(s_symbols!=NULL && s_queue_live);
    s_fault=NORMAL;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_OK);
  }
  clean();
}
uint8_t cc1101_get_active_preset_id(void) { return 6; }
static size_t encode_capture(const subghz_data_t *data,int32_t *out,size_t capacity) {
  assert(data->raw_value==42 && data->bit_count==12 && capacity>=3);
  out[0]=350;out[1]=-700;out[2]=350;return 3;
}
const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name) {
  static const subghz_protocol_t protocol={.encode=encode_capture};
  return strcmp(name,"CAME")==0?&protocol:NULL;
}
size_t subghz_protocol_registry_encode(const char *name,const subghz_data_t *data,int32_t *out,size_t capacity) {
  return encode_capture(data,out,capacity);
}
static FILE *open_capture(const char *path,const char *mode) {
  assert(s_gate && strcmp(mode,"rb")==0);
  if(strcmp(path,"/sdcard/missing.sub")==0)return NULL;
  FILE *file=tmpfile();assert(file!=NULL);
  if(strcmp(path,"/sdcard/large.sub")==0) {
    for(unsigned i=0;i<8193;++i)fputc('x',file);
  } else if(strcmp(path,"/sdcard/nul.sub")==0) {
    fwrite("xx\0xx",1,5,file);
  } else if(strcmp(path,"/sdcard/decoded.sub")==0) {
    fputs("Frequency: 433920000\nPreset: 6\nProtocol: CAME\nBit: 12\nKey: 00 00 00 00 00 00 00 2A\n",file);
  } else if(strcmp(path,"/sdcard/band.sub")==0) {
    fputs("Frequency: 350000000\nPreset: 6\nRAW_Data: 350 -700 350\n",file);
  } else if(strcmp(path,"/sdcard/preset.sub")==0) {
    fputs("Frequency: 433920000\nPreset: 255\nRAW_Data: 350 -700 350\n",file);
  } else if(strcmp(path,"/sdcard/pulses.sub")==0) {
    fputs("Frequency: 433920000\nPreset: 6\nRAW_Data: 99999\n",file);
  } else if(strcmp(path,"/sdcard/too-many.sub")==0) {
    fputs("Frequency: 433920000\nPreset: 6\nRAW_Data:",file);
    for(unsigned i=0;i<513;++i)fputs(" 350",file);
  } else {
    fputs("Frequency: 433920000\nPreset: 6\nRAW_Data: 350 -700 350\n",file);
  }
  rewind(file);return file;
}
static void test_replay(void) {
  s_fault=NORMAL;
  unsigned calls=s_calls;
  const char *bad_paths[]={NULL,"/sdcard/","/sdcard//one.sub","/sdcard/../one.sub",
    "/sdcard/a/./one.sub","/sdcard/a/","/sdcard/a\\one.sub","/other/one.sub"};
  for(unsigned i=0;i<sizeof(bad_paths)/sizeof(bad_paths[0]);++i)
    assert(tos_api_subghz_replay(bad_paths[i])==ESP_ERR_INVALID_ARG);
  s_allowed=false;assert(tos_api_subghz_replay("/sdcard/one.sub")==ESP_ERR_NOT_SUPPORTED);
  s_allowed=true;s_stopped=true;assert(tos_api_subghz_replay("/sdcard/one.sub")==ESP_ERR_INVALID_STATE);
  s_stopped=false;
  assert(tos_api_subghz_replay("/sdcard/missing.sub")==ESP_ERR_NOT_FOUND);
  assert(tos_api_subghz_replay("/sdcard/large.sub")==ESP_ERR_INVALID_SIZE);
  assert(tos_api_subghz_replay("/sdcard/nul.sub")==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_replay("/sdcard/band.sub")==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_replay("/sdcard/preset.sub")==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_replay("/sdcard/pulses.sub")==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_replay("/sdcard/too-many.sub")==ESP_ERR_INVALID_ARG);
  assert(s_calls==calls);clean();
  for(unsigned i=1;i<=2;++i) {
    s_fault=FAIL_ALLOC;s_fail_allocation=i;s_allocations=0;
    assert(tos_api_subghz_replay("/sdcard/one.sub")==ESP_ERR_NO_MEM);clean();
  }
  s_fault=NORMAL;assert(tos_api_subghz_replay("/sdcard/one.sub")==ESP_OK);clean();
  assert(tos_api_subghz_replay("/sdcard/decoded.sub")==ESP_OK);clean();
  s_fault=FAIL_DISABLE;assert(tos_api_subghz_replay("/sdcard/one.sub")!=ESP_OK);
  assert(s_resource_live && s_symbols!=NULL);
  s_fault=NORMAL;assert(tos_api_subghz_replay("/sdcard/one.sub")==ESP_OK);clean();
}
#ifndef SUBGHZ_TEST_SESSIONS
BaseType_t xTaskCreatePinnedToCore(void (*task)(void *),const char *name,uint32_t stack,void *arg,
                                   int priority,TaskHandle_t *out,int core) {
  (void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)out;(void)core;return 0;
}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t timeout) {(void)clear;(void)timeout;return 0;}
void xTaskNotifyGive(TaskHandle_t task) {(void)task;}
#endif
#ifndef SUBGHZ_TEST_SESSIONS
int main(void) {
  tos_subghz_signal_t out;int32_t timings[]={350,-700,350};
  int32_t raw[1024];memset(raw,0x5a,sizeof(raw));
  assert(tos_api_subghz_get_raw(raw,1024)==0);
  assert(tos_api_subghz_get_raw(NULL,1)==-ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_get_raw(raw,0)==-ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_get_raw(raw,1025)==-ESP_ERR_INVALID_ARG);
  s_allowed=false;assert(tos_api_subghz_get_raw(raw,6)==-ESP_ERR_NOT_SUPPORTED);
  s_allowed=true;s_stopped=true;assert(tos_api_subghz_get_raw(raw,6)==-ESP_ERR_INVALID_STATE);
  s_stopped=false;

  assert(tos_api_subghz_receive(NULL,433920000,6,500)==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_receive(&out,350000000,6,500)==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_receive(&out,433920000,0,500)==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_receive(&out,433920000,6,1001)==ESP_ERR_INVALID_ARG);
  s_allowed=false;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_ERR_NOT_SUPPORTED);
  assert(tos_api_subghz_tx_raw(timings,3,433920000)==ESP_ERR_NOT_SUPPORTED);
  s_allowed=true;s_stopped=true;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_ERR_INVALID_STATE);
  s_stopped=false;s_ready=false;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_ERR_INVALID_STATE);
  s_ready=true;s_lock_timeout=true;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_ERR_TIMEOUT);
  s_lock_timeout=false;s_stop_locked=true;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_ERR_INVALID_STATE);
  s_stop_locked=false;s_stopped=false;assert(s_calls==0 && !s_gate);
  s_resource_busy=true;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_ERR_INVALID_STATE);s_resource_busy=false;
  for(fault_t f=NORMAL;f<=FAIL_DELETE;++f) {
    bool retained=f==FAIL_POWER_RELEASE || f==FAIL_DISABLE || f==FAIL_DELETE;
    if(f==FAIL_ALLOC){s_fail_allocation=1;receive_case(f,false);s_fail_allocation=2;}
    receive_case(f,retained);
  }
  receive_case(FAIL_IDLE,true);
  assert(tos_api_subghz_get_raw(raw,5)==-ESP_ERR_INVALID_SIZE);
  assert(raw[0]==0x5a5a5a5a);
  assert(tos_api_subghz_get_raw(raw,6)==6);
  for(unsigned i=0;i<6;++i)assert(raw[i]==(i%2==0?350:-700));
  int32_t *previous=s_last_raw;
  s_fault=FAIL_DISABLE;
  assert(tos_api_subghz_receive(&out,433920000,6,500)!=ESP_OK);
  assert(s_last_raw==previous && tos_api_subghz_get_raw(raw,6)==6);
  s_fault=NORMAL;assert(tos_api_subghz_receive(&out,433920000,6,500)==ESP_OK);clean();
  for(fault_t f=NORMAL;f<=FAIL_IDLE;++f) {
    if(f!=NORMAL && f!=FAIL_ENCODER && f!=FAIL_ENCODER_DELETE && f!=FAIL_TRANSMIT && f!=FAIL_TX_WAIT && f!=FAIL_DISABLE && f!=FAIL_DELETE)continue;
    s_fault=f;
    esp_err_t error=tos_api_subghz_tx_raw(timings,3,433920000);
    assert((error==ESP_OK)==(f==NORMAL));
    if(f==FAIL_ENCODER_DELETE || f==FAIL_DISABLE || f==FAIL_DELETE) {
      assert(s_resource_live && s_symbols!=NULL && !cc1101_lease_enter());
      s_fault=NORMAL;assert(tos_api_subghz_tx_raw(timings,3,433920000)==ESP_OK);
    }
    clean();
  }
  int32_t invalid[]={INT32_MIN};s_fault=NORMAL;
  unsigned calls=s_calls;
  assert(tos_api_subghz_tx_raw(invalid,1,433920000)==ESP_ERR_INVALID_ARG);
  invalid[0]=0;assert(tos_api_subghz_tx_raw(invalid,1,433920000)==ESP_ERR_INVALID_ARG);
  int32_t long_signal[512];for(unsigned i=0;i<512;++i)long_signal[i]=32767;
  assert(tos_api_subghz_tx_raw(long_signal,512,433920000)==ESP_ERR_INVALID_ARG && s_calls==calls);
  s_fault=FAIL_ALLOC;s_fail_allocation=1;s_allocations=0;
  assert(tos_api_subghz_tx_raw(timings,3,433920000)==ESP_ERR_NO_MEM);clean();
  s_fail_allocation=2;s_allocations=0;
  assert(tos_api_subghz_tx_raw(timings,3,433920000)==ESP_ERR_NO_MEM);clean();
  tos_subghz_spectrum_t spectrum;
  assert(tos_api_subghz_spectrum_capture(NULL,433920000,1000000)==ESP_ERR_INVALID_ARG);
  const uint32_t invalid_ranges[][2]={{433920000,0},{433920000,79},{433920000,20000001},
    {300000000,80},{348000000,80},{387000000,1000},{464000000,1000},
    {779000000,1000},{928000000,1000},{350000000,1000000},{UINT32_MAX,UINT32_MAX}};
  calls=s_calls;
  for(unsigned i=0;i<sizeof(invalid_ranges)/sizeof(invalid_ranges[0]);++i) {
    memset(&spectrum,0xff,sizeof(spectrum));
    assert(tos_api_subghz_spectrum_capture(&spectrum,invalid_ranges[i][0],invalid_ranges[i][1])==ESP_ERR_INVALID_ARG);
    assert(spectrum.start_hz==0);
  }
  assert(s_calls==calls);
  s_allowed=false;
  assert(tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000)==ESP_ERR_NOT_SUPPORTED);
  s_allowed=true;s_stopped=true;
  assert(tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000)==ESP_ERR_INVALID_STATE);
  s_stopped=false;s_ready=false;
  assert(tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000)==ESP_ERR_INVALID_STATE);
  s_ready=true;s_lock_timeout=true;
  assert(tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000)==ESP_ERR_TIMEOUT);
  s_lock_timeout=false;s_stop_locked=true;
  assert(tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000)==ESP_ERR_INVALID_STATE);
  s_stop_locked=false;s_stopped=false;assert(!s_gate && s_calls==calls);
  s_resource_busy=true;
  assert(tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000)==ESP_ERR_INVALID_STATE);
  s_resource_busy=false;
  const fault_t sweep_faults[]={NORMAL,FAIL_TUNE,FAIL_RSSI,FAIL_SWEEP_CLEANUP,FAIL_POWER_RELEASE};
  for(unsigned i=0;i<sizeof(sweep_faults)/sizeof(sweep_faults[0]);++i) {
    s_fault=sweep_faults[i];s_tunes=s_reads=s_settles=s_sample_delays=s_yields=0;
    memset(&spectrum,0xff,sizeof(spectrum));
    esp_err_t error=tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000);
    if(s_fault==NORMAL) {
      assert(error==ESP_OK && spectrum.start_hz==433420000 && spectrum.step_hz==12500);
      assert(s_tunes==80 && s_reads==240 && s_settles==80 && s_sample_delays==240 && s_yields==80);
      for(unsigned bin=0;bin<80;++bin)assert(spectrum.dbm[bin]==-68.0f);
    } else {
      assert(error!=ESP_OK);
      const tos_subghz_spectrum_t zero={0};assert(memcmp(&zero,&spectrum,sizeof(zero))==0);
      if(s_fault==FAIL_TUNE)assert(s_tunes==17 && s_reads==48);
      if(s_fault==FAIL_RSSI)assert(s_tunes==11 && s_reads==32);
    }
    if(s_fault==FAIL_SWEEP_CLEANUP || s_fault==FAIL_POWER_RELEASE) {
      assert(s_resource_live && s_power==1 && !cc1101_lease_enter());
      s_fault=NORMAL;s_tunes=s_reads=s_settles=s_sample_delays=s_yields=0;
      assert(tos_api_subghz_spectrum_capture(&spectrum,433920000,1000000)==ESP_OK);
    }
    clean();
  }
  const uint32_t valid_ranges[][2]={{300000040,80},{347999960,80},{387000040,81},
    {463999960,80},{779000040,80},{927999960,80},{433920000,20000000}};
  s_fault=NORMAL;
  for(unsigned i=0;i<sizeof(valid_ranges)/sizeof(valid_ranges[0]);++i) {
    s_expected_start=valid_ranges[i][0]-valid_ranges[i][1]/2;
    s_expected_step=valid_ranges[i][1]/80;
    s_tunes=s_reads=s_settles=s_sample_delays=s_yields=0;
    assert(tos_api_subghz_spectrum_capture(&spectrum,valid_ranges[i][0],valid_ranges[i][1])==ESP_OK);
    assert(spectrum.start_hz==s_expected_start && spectrum.step_hz==s_expected_step);
    assert(s_tunes==80 && s_reads==240);clean();
  }
  puts("PASS: 80-bin spectrum, exact tuning/sample grid, range/capability/cancellation gates, no partial output, retained SPI/power cleanup");
  test_replay();
  free(s_last_raw);s_last_raw=NULL;s_last_raw_count=0;
  puts("PASS: bounded Sub-GHz RX/TX, capabilities, cancellation, native contention, errors and retained RMT/SPI/power cleanup");
}

#else
#include "test_sessions.c"
#endif
