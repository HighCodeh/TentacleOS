#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tos_api.h"
#include "tos_app_ctx.h"
#include "lora_session.h"
#include "subghz_receiver.h"

esp_err_t tos_api_subghz_receive(tos_subghz_signal_t *out,uint32_t freq,uint8_t preset,uint32_t timeout) {
  (void)out;(void)freq;(void)preset;(void)timeout;return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t tos_api_subghz_tx_raw(const int32_t *timings,int count,uint32_t freq) {
  (void)timings;(void)count;(void)freq;return ESP_ERR_NOT_SUPPORTED;
}
extern const tos_subghz_api_t tos_subghz_api_impl;
extern const tos_lora_api_t tos_lora_api_impl;
static bool s_allowed, s_stopped, s_ready, s_timeout, s_stop_after_lock, s_locked;
static unsigned s_locks, s_reads;
static tos_app_ctx_t s_apps[2];
static tos_app_ctx_t *s_current;
static uint32_t s_capture_sequence=1;
static bool s_has_capture=true;
static int should_stop(void) { return s_stopped; }
static const tos_api_t s_api={.should_stop=should_stop};
const tos_api_t *tos_api_get(void) { return &s_api; }
bool tos_app_cap_check(uint32_t cap) { assert(cap==TOS_CAP_RADIO_RX); return s_allowed; }
bool host_local_rf_is_ready(void) { return s_ready; }
bool host_local_rf_lock(uint32_t timeout) {
  assert(timeout==100 && !s_locked); ++s_locks;
  if(s_timeout) return false;
  s_locked=true;
  if(s_stop_after_lock) s_stopped=true;
  return true;
}
void host_local_rf_unlock(void) { assert(s_locked); s_locked=false; }
tos_app_ctx_t *tos_app_ctx_current(void) { assert(s_locked); return s_current; }
bool subghz_receiver_get_result(subghz_rx_result_t *out) {
  assert(s_locked); ++s_reads;
  memset(out,0,sizeof(*out)); memset(out->name_buf,'x',sizeof(out->name_buf));
  out->seq=s_capture_sequence; out->decoded=true; out->freq=433920000;
  out->data=(subghz_data_t){.raw_value=42,.serial=7,.bit_count=24,.btn=3};
  return s_has_capture;
}
uint16_t lora_session_msg_since(uint32_t *sequence,lora_msg_t *out,uint16_t max) {
  assert(s_locked && max==1); ++s_reads;
  if(*sequence==2) return 0;
  ++*sequence; memset(out,'x',sizeof(*out)); out->outgoing=false;
  return 1;
}

int main(void) {
  tos_subghz_signal_t signal;
  tos_lora_msg_t message;
  assert(tos_subghz_api_impl.rx_start==NULL && tos_subghz_api_impl.session_stop==NULL);
  assert(tos_subghz_api_impl.tx_raw!=NULL && tos_subghz_api_impl.receive!=NULL && tos_subghz_api_impl.spectrum_start==NULL);
  assert(tos_lora_api_impl.mesh_start==NULL && tos_lora_api_impl.mesh_send_text==NULL);
  assert(tos_lora_api_impl.configure==NULL && tos_lora_api_impl.rx_start==NULL);
  assert(tos_subghz_api_impl.rx_poll(NULL)==-ESP_ERR_INVALID_ARG);
  assert(tos_lora_api_impl.mesh_poll(NULL)==-ESP_ERR_INVALID_ARG);
  assert(tos_subghz_api_impl.rx_poll(&signal)==-ESP_ERR_NOT_SUPPORTED);
  assert(tos_lora_api_impl.mesh_poll(&message)==-ESP_ERR_NOT_SUPPORTED);
  assert(s_reads==0 && s_locks==0);
  s_allowed=true;
  assert(tos_subghz_api_impl.rx_poll(&signal)==-ESP_ERR_INVALID_STATE);
  s_ready=true; s_stopped=true;
  assert(tos_lora_api_impl.mesh_poll(&message)==-ESP_ERR_INVALID_STATE);
  s_stopped=false; s_timeout=true;
  assert(tos_subghz_api_impl.rx_poll(&signal)==-ESP_ERR_TIMEOUT);
  s_timeout=false; s_stop_after_lock=true;
  assert(tos_lora_api_impl.mesh_poll(&message)==-ESP_ERR_INVALID_STATE);
  assert(!s_locked && s_reads==0);
  s_stop_after_lock=false; s_stopped=false;
  for(unsigned app=0;app<2;++app) {
    s_current=&s_apps[app];
    assert(tos_subghz_api_impl.rx_poll(&signal)==1);
    assert(signal.protocol[sizeof(signal.protocol)-1]==0 && signal.decoded);
    assert(signal.freq_hz==433920000 && signal.value==42 && signal.serial==7);
    assert(signal.bit_count==24 && signal.btn==3);
    assert(tos_subghz_api_impl.rx_poll(&signal)==0);
    assert(signal.value==0);
    assert(tos_lora_api_impl.mesh_poll(&message)==1);
    assert(message.who[sizeof(message.who)-1]==0 && message.text[sizeof(message.text)-1]==0);
    assert(tos_lora_api_impl.mesh_poll(&message)==1);
    assert(tos_lora_api_impl.mesh_poll(&message)==0);
  }
  s_capture_sequence=2;
  assert(tos_subghz_api_impl.rx_poll(&signal)==1);
  s_has_capture=false;
  assert(tos_subghz_api_impl.rx_poll(&signal)==0 && !s_locked);
  s_has_capture=true; s_current=NULL;
  assert(tos_subghz_api_impl.rx_poll(&signal)==1);
  assert(tos_subghz_api_impl.rx_poll(&signal)==0);
  assert(tos_lora_api_impl.mesh_poll(&message)==1);
  puts("Radio view: capability/cancellation gates, private cursors, bounded snapshots passed");
  return 0;
}
