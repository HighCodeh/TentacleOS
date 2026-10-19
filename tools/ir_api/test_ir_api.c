#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "freertos/task.h"
#include "ir.h"
#include "ir_lease.h"
#include "tos_api.h"
#include "host_link_local_ir.h"
#include "host_link_local_rf.h"

static void *s_caller=(void *)1;
static void *s_rx_chan,*s_tx_chan,*s_tx_encoder,*s_rx_queue;
static bool s_is_rx_inited,s_is_tx_inited;
static int s_rx_res,s_tx_res;
#define RES_HANDLE_NONE 0
static uint32_t s_current_carrier;
static bool s_rx_armed;
static bool s_fail_delete,s_fail_encoder;
static unsigned s_queue_freed,s_resources_freed,s_transmits;
static esp_err_t s_receive_error;
static ir_data_t s_sent;
static esp_err_t rmt_disable(void *channel) {assert(channel);return ESP_OK;}
static esp_err_t rmt_del_channel(void *channel) {assert(channel);return s_fail_delete?ESP_FAIL:ESP_OK;}
static esp_err_t rmt_del_encoder(void *encoder) {assert(encoder);return s_fail_encoder?ESP_FAIL:ESP_OK;}
static void vQueueDelete(void *queue) {assert(queue);++s_queue_freed;}
static void resource_release(int handle) {if(handle)++s_resources_freed;}
#define ESP_LOGE(tag,...) ((void)0)
#define ESP_LOGI(tag,...) ((void)0)
const char *esp_err_to_name(esp_err_t e) {(void)e;return "fixture";}
TaskHandle_t xTaskGetCurrentTaskHandle(void) {return s_caller;}
/* PRODUCTION_WRAPPERS */
/* PRODUCTION_RX_DEINIT */
/* PRODUCTION_TX_DEINIT */
static esp_err_t rx_init_impl(void) {s_rx_chan=(void *)1;s_rx_queue=(void *)2;s_rx_res=1;s_is_rx_inited=true;return ESP_OK;}
static void rx_prime_impl(void) {}
static esp_err_t tx_init_impl(void) {s_tx_chan=(void *)3;s_tx_encoder=(void *)4;s_tx_res=2;s_is_tx_inited=true;return ESP_OK;}
static esp_err_t flash_on_impl(void) {return ESP_OK;}
static esp_err_t flash_off_impl(void) {return ESP_OK;}
static esp_err_t receive_impl(ir_data_t *out,uint32_t timeout) {
 assert(s_rx_chan && timeout==500);void *saved=s_caller;s_caller=(void *)3;
 assert(ir_tx_init()==ESP_ERR_INVALID_STATE);ir_rx_deinit();assert(s_rx_chan);s_caller=saved;
 *out=(ir_data_t){.protocol=IR_PROTO_NEC42,.address=42,.command=7,.repeat=true};return s_receive_error;
}
static esp_err_t send_impl(const ir_data_t *data) {assert(s_tx_chan);s_sent=*data;++s_transmits;return ESP_OK;}
static esp_err_t send_raw_impl(const rmt_symbol_word_t *p,size_t n,uint32_t hz) {(void)p;(void)n;(void)hz;return ESP_OK;}
static esp_err_t get_last_raw_impl(rmt_symbol_word_t *p,size_t max,size_t *count) {(void)p;(void)max;(void)count;return ESP_OK;}

extern const tos_ir_api_t tos_ir_api_impl;
static uint32_t s_caps;
static bool s_stop,s_stop_on_lock,s_can_lock=true,s_locked;
static int should_stop(void) {return s_stop;}
const tos_api_t *tos_api_get(void) {static const tos_api_t api={.should_stop=should_stop};return &api;}
bool tos_app_cap_check(uint32_t cap) {return (s_caps&cap)==cap;}
bool host_local_rf_lock(uint32_t timeout) {assert(timeout==3000);if(!s_can_lock)return false;assert(!s_locked);s_locked=true;if(s_stop_on_lock)s_stop=true;return true;}
void host_local_rf_unlock(void) {assert(s_locked);s_locked=false;}
esp_err_t host_local_rf_execute(host_rf_request_t *r) {assert(s_locked);void *saved=s_caller;s_caller=(void *)2;esp_err_t error=host_local_ir_execute(r);s_caller=saved;return error;}
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
 puts("PASS: production IR lease/wrappers, native contention, typed ABI, protocol conversion, stop, timeout and retained cleanup");
 return 0;
}
