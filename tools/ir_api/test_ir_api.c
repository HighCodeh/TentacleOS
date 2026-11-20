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
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "ir.h"
#include "ir_lease.h"
#include "ir_ac.h"
#include <limits.h>
#include <stdlib.h>
#include "tos_api.h"
#include "host_link_local_ir.h"
#include "host_link_local_rf.h"

static bool s_locked;
static void *s_caller=(void *)1;
// ABI tests use a fault-injectable IR service; paired hardware ownership is
// exercised against production code by tools/ir_remote/test_remote.py.
static void *s_rx_chan, *s_tx_chan, *s_tx_encoder;
static bool s_has_torch;
static bool s_fail_delete, s_fail_encoder;
static unsigned s_queue_freed, s_resources_freed, s_transmits;
static esp_err_t s_receive_error;
static ir_data_t s_sent;
const char *esp_err_to_name(esp_err_t e) { (void)e; return "fixture"; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return s_caller; }
static esp_err_t finish(esp_err_t result) {
  ir_lease_leave(s_rx_chan || s_tx_chan || s_tx_encoder || s_has_torch);
  return result;
}
esp_err_t ir_rx_init(void) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  s_rx_chan = (void *)1;
  return finish(ESP_OK);
}
esp_err_t ir_tx_init(void) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  s_tx_chan = (void *)2; s_tx_encoder = (void *)3;
  return finish(ESP_OK);
}
void ir_rx_prime(void) {}
void ir_rx_deinit(void) {
  if (!ir_lease_enter()) return;
  if (s_rx_chan && !s_fail_delete) { s_rx_chan = NULL; s_queue_freed++; s_resources_freed++; }
  (void)finish(ESP_OK);
}
void ir_tx_deinit(void) {
  if (!ir_lease_enter()) return;
  if (s_tx_chan && !s_fail_delete) { s_tx_chan = NULL; s_resources_freed++; }
  if (!s_tx_chan && !s_fail_encoder) s_tx_encoder = NULL;
  (void)finish(ESP_OK);
}
esp_err_t ir_flash_on(void) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  s_has_torch = true; return finish(ESP_OK);
}
esp_err_t ir_flash_off(void) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  s_has_torch = false; return finish(ESP_OK);
}
static esp_err_t receive_impl(ir_data_t *out,uint32_t timeout) {
 assert(s_rx_chan && timeout==500);void *saved=s_caller;s_caller=(void *)3;
 assert(ir_tx_init()==ESP_ERR_INVALID_STATE);ir_rx_deinit();assert(s_rx_chan);s_caller=saved;
 *out=(ir_data_t){.protocol=IR_PROTO_NEC42,.address=42,.command=7,.repeat=true};return s_receive_error;
}
static esp_err_t send_impl(const ir_data_t *data) {assert(s_tx_chan);s_sent=*data;++s_transmits;return ESP_OK;}
static rmt_symbol_word_t s_raw_sent[4096];
static const rmt_symbol_word_t *s_raw_pointer;
static size_t s_raw_count;
static uint32_t s_raw_carrier;
static esp_err_t s_raw_error;
static esp_err_t send_raw_impl(const rmt_symbol_word_t *p,size_t n,uint32_t hz) {
 s_raw_pointer=p;assert(n<=4096);memcpy(s_raw_sent,p,n*sizeof(*p));s_raw_count=n;s_raw_carrier=hz;return s_raw_error;
}
static esp_err_t get_last_raw_impl(rmt_symbol_word_t *p,size_t max,size_t *count) {
 assert(max>=2);p[0]=(rmt_symbol_word_t){.duration0=350,.level0=1,.duration1=700};
 p[1]=(rmt_symbol_word_t){.duration0=350,.level0=1};*count=2;return ESP_OK;
}
esp_err_t ir_receive(ir_data_t *out, uint32_t timeout) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  return finish(receive_impl(out, timeout));
}
esp_err_t ir_send(const ir_data_t *data) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  return finish(send_impl(data));
}
esp_err_t ir_send_raw(const rmt_symbol_word_t *p, size_t n, uint32_t hz) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  return finish(send_raw_impl(p, n, hz));
}
esp_err_t ir_get_last_raw(rmt_symbol_word_t *p, size_t n, size_t *out) {
  if (!ir_lease_enter()) return ESP_ERR_INVALID_STATE;
  return finish(get_last_raw_impl(p, n, out));
}
static ir_ac_state_t s_ac;
size_t ir_ac_encode(const ir_ac_state_t *state,rmt_symbol_word_t *symbols,size_t max) {
 assert(max>=2);s_ac=*state;symbols[0]=(rmt_symbol_word_t){.duration0=1200,.level0=1};return 1;
}
uint32_t ir_ac_carrier_freq(ir_ac_protocol_t protocol) {(void)protocol;return 38000;}
size_t ir_encode(const ir_data_t *data,rmt_symbol_word_t *symbols,size_t max) {
 assert(data && max>=2);symbols[0]=(rmt_symbol_word_t){.duration0=9000,.level0=1,.duration1=4500};return 1;
}
uint32_t ir_carrier_freq(ir_protocol_t protocol) {(void)protocol;return 38000;}
esp_err_t host_local_rf_call(host_local_rf_cb_t callback,void *context) {
 assert(s_locked);void *saved=s_caller;s_caller=(void *)2;
 esp_err_t error=callback(context);s_caller=saved;return error;
}

extern const tos_ir_api_t tos_ir_api_impl;
static uint32_t s_caps;
static bool s_stop,s_stop_on_lock,s_can_lock=true;
static int should_stop(void) {return s_stop;}
const tos_api_t *tos_api_get(void) {static const tos_api_t api={.should_stop=should_stop};return &api;}
bool tos_app_cap_check(uint32_t cap) {return (s_caps&cap)==cap;}
bool host_local_rf_lock(uint32_t timeout) {assert(timeout==3000);if(!s_can_lock)return false;assert(!s_locked);s_locked=true;if(s_stop_on_lock)s_stop=true;return true;}
void host_local_rf_unlock(void) {assert(s_locked);s_locked=false;}
esp_err_t host_local_rf_execute(host_rf_request_t *r) {assert(s_locked);void *saved=s_caller;s_caller=(void *)2;esp_err_t error=host_local_ir_execute(r);s_caller=saved;return error;}
static bool s_fail_allocate;
void *ir_test_calloc(size_t count,size_t size) {return s_fail_allocate?NULL:calloc(count,size);}
void *ir_test_malloc(size_t size) {return s_fail_allocate?NULL:malloc(size);}
FILE *ir_test_open(const char *path,const char *mode) {
 (void)mode;
 const char *content=NULL;
 if(strcmp(path,"/sdcard/one.ir")==0)content="name: one\ntype: parsed\nprotocol: NEC\naddress: 2A\ncommand: 07\n";
 if(strcmp(path,"/sdcard/raw.ir")==0)content="name: one\ntype: raw\nfrequency: 38000\ndata: 350 700 350\n";
 if(strcmp(path,"/sdcard/two.ir")==0)content="name: one\ntype: parsed\nprotocol: NEC\nname: two\ntype: parsed\nprotocol: NEC\n";
 if(strcmp(path,"/sdcard/bad.ir")==0)content="name: one\ntype: raw\nfrequency: 38000\ndata: 99999\n";
 if(content==NULL)return NULL;
 FILE *file=tmpfile();assert(file);assert(fwrite(content,1,strlen(content),file)==strlen(content));rewind(file);return file;
}
int main(void) {
 tos_ir_code_t code={.protocol=TOS_IR_NEC,.address=42,.command=7,.repeat=false},out;
 assert(tos_ir_api_impl.send(NULL)==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.receive(NULL,500)==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.send(&code)==ESP_ERR_NOT_SUPPORTED);
 s_caps=TOS_CAP_RADIO_RX;assert(tos_ir_api_impl.receive(&out,0)==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.receive(&out,1001)==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.receive(&out,500)==ESP_OK && out.protocol==TOS_IR_NEC42 && out.address==42 && out.repeat);
 s_receive_error=ESP_ERR_TIMEOUT;assert(tos_ir_api_impl.receive(&out,500)==ESP_ERR_TIMEOUT);s_receive_error=0;
 s_fail_delete=true;assert(tos_ir_api_impl.receive(&out,500)==ESP_ERR_INVALID_STATE && s_rx_chan);
 s_fail_delete=false;assert(tos_ir_api_impl.receive(&out,500)==ESP_OK && !s_rx_chan);
 s_caps|=TOS_CAP_RADIO_TX;assert(tos_ir_api_impl.send(&code)==ESP_OK && s_transmits==1 && s_sent.protocol==IR_PROTO_NEC);
 s_caller=(void *)3;assert(ir_rx_init()==ESP_OK);s_caller=(void *)1;
 assert(tos_ir_api_impl.send(&code)==ESP_ERR_INVALID_STATE && s_rx_chan);
 s_caller=(void *)3;ir_rx_deinit();assert(!s_rx_chan);s_caller=(void *)1;
 s_can_lock=false;assert(tos_ir_api_impl.send(&code)==ESP_ERR_TIMEOUT);s_can_lock=true;
 s_stop_on_lock=true;assert(tos_ir_api_impl.send(&code)==ESP_ERR_INVALID_STATE && !s_locked);s_stop_on_lock=false;s_stop=false;
 s_fail_delete=true;assert(tos_ir_api_impl.send(&code)==ESP_ERR_INVALID_STATE && s_tx_chan);
 assert(ir_rx_init()==ESP_ERR_INVALID_STATE);
 s_caller=(void *)3;ir_tx_deinit();assert(s_tx_chan);s_caller=(void *)1;
 s_fail_delete=false;assert(tos_ir_api_impl.send(&code)==ESP_OK && !s_tx_chan && !s_tx_encoder);
 assert(ir_flash_on()==ESP_OK);assert(tos_ir_api_impl.send(&code)==ESP_ERR_INVALID_STATE);assert(ir_flash_off()==ESP_OK);
 s_fail_encoder=true;assert(tos_ir_api_impl.send(&code)==ESP_ERR_INVALID_STATE && s_tx_encoder && !s_tx_chan);
 s_fail_encoder=false;assert(tos_ir_api_impl.send(&code)==ESP_OK && !s_tx_encoder);
 assert(s_queue_freed>=3 && s_resources_freed>=3);
 int32_t pulses[]={350,-700,350};
 assert(tos_ir_api_impl.send_raw(pulses,3,38000)==ESP_OK);
 assert(s_raw_count==2 && s_raw_carrier==38000 && s_raw_sent[0].duration0==350 && s_raw_sent[0].level0==1 && s_raw_sent[0].duration1==700 && !s_raw_sent[0].level1 && s_raw_sent[1].duration1==0);
 pulses[0]=INT32_MIN;assert(tos_ir_api_impl.send_raw(pulses,3,38000)==ESP_ERR_INVALID_ARG);pulses[0]=350;
 assert(tos_ir_api_impl.send_raw(pulses,3,9999)==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.send_raw(pulses,1025,38000)==ESP_ERR_INVALID_ARG);
 int32_t too_long[28];for(unsigned i=0;i<28;++i)too_long[i]=32767;
 assert(tos_ir_api_impl.send_raw(too_long,28,38000)==ESP_ERR_INVALID_SIZE);
 int32_t received[8];assert(tos_ir_api_impl.get_last_raw(received,8)==3 && received[0]==350 && received[1]==-700 && received[2]==350);
 assert(tos_ir_api_impl.get_last_raw(received,2)==-ESP_ERR_INVALID_SIZE);
 tos_ir_ac_t ac={.protocol=TOS_IR_AC_HAIER,.power=true,.mode=TOS_IR_AC_MODE_HEAT,.temp_c=23,.fan=TOS_IR_AC_FAN_HIGH};
 assert(tos_ir_api_impl.send_ac(&ac)==ESP_OK && s_ac.protocol==IR_AC_PROTO_HAIER && s_ac.mode==IR_AC_MODE_HEAT && s_ac.temp_c==23);
 ac.mode=TOS_IR_AC_MODE_DRY;assert(tos_ir_api_impl.send_ac(&ac)==ESP_OK && s_ac.mode==IR_AC_MODE_DRY);
 ac.protocol=255;assert(tos_ir_api_impl.send_ac(&ac)==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.send_file("/sdcard2/a.ir")==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.send_file("/sdcard/../a.ir")==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.send_file("/sdcard/./a.ir")==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.send_file("/sdcard/a/")==ESP_ERR_INVALID_ARG);
 assert(tos_ir_api_impl.send_file("/sdcard/a.ir")==ESP_ERR_NOT_SUPPORTED);
 s_caps|=TOS_CAP_FS_READ;
 assert(tos_ir_api_impl.send_file("/sdcard/nonexistent-test.ir")==ESP_ERR_NOT_FOUND);
 assert(tos_ir_api_impl.send_file("/sdcard/one.ir")==ESP_OK && s_raw_sent[0].duration0==9000);
 assert(tos_ir_api_impl.send_file("/sdcard/raw.ir")==ESP_OK && s_raw_count==2 && s_raw_sent[1].duration0==350);
 assert(tos_ir_api_impl.send_file("/sdcard/two.ir")==ESP_ERR_INVALID_SIZE);
 assert(tos_ir_api_impl.send_file("/sdcard/bad.ir")==ESP_ERR_INVALID_SIZE);
 s_fail_allocate=true;assert(tos_ir_api_impl.send_raw(pulses,3,38000)==ESP_ERR_NO_MEM);s_fail_allocate=false;
 s_raw_error=ESP_ERR_TIMEOUT;s_fail_delete=true;
 assert(tos_ir_api_impl.send_raw(pulses,3,38000)==ESP_ERR_INVALID_STATE && s_tx_chan);
 assert(s_raw_pointer[0].duration0==350);
 s_fail_delete=false;s_raw_error=0;
 assert(tos_ir_api_impl.send(&code)==ESP_OK);
 assert(tos_ir_api_impl.send_raw(pulses,3,0)==ESP_OK);
 s_caps=0;assert(tos_ir_api_impl.send_raw(pulses,3,38000)==ESP_ERR_NOT_SUPPORTED);
 assert(tos_ir_api_impl.get_last_raw(received,8)==-ESP_ERR_NOT_SUPPORTED);
 puts("PASS: bounded raw IR, signed timings, AC enum conversion, SD path/capability checks and retained backend cleanup");
 puts("PASS: production IR lease and ABI, mocked native contention, typed ABI, protocol conversion, stop, timeout and retained cleanup");
 return 0;
}
