#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define SX1262_STOP_WAIT_MS 500
#define APP_STOP_WAIT_MS 10000
#define APP_STOP_POLL_MS 10
#define MESH_STOP_WAIT_MS 10000
#define SX1262_STDBY_RC 0
#define SX1262_OP_SET_STANDBY 0x80
#define SX1262_ANT_OFF 0
#define pdMS_TO_TICKS(t) (t)
static _Atomic(TaskHandle_t) s_irq_task_handle;
static atomic_bool s_is_running;
static bool s_is_initialized;
static unsigned s_ticks,s_delays,s_ack_delay,s_hal_frees,s_radio_clears,s_irq_clears,s_stop_rx_calls;
static esp_err_t s_spi_error,s_hal_error;
static TaskHandle_t s_stream_task_handle;
static atomic_uint s_stream_epoch;
static TaskHandle_t s_self=(void *)9;
static bool s_advance_epoch;
static _Atomic(TaskHandle_t) *s_wait_handle;
static void antenna_off(void *ctx,int mode) { assert(ctx==(void *)1 && mode==0 && s_irq_task_handle==NULL); }
static struct { struct { void *spi_transfer;void *ctx;void (*set_antenna)(void *,int); } hal; } s_config;
static struct { void *callback; } s_callbacks;
static TickType_t xTaskGetTickCount(void) { return s_ticks; }
static void *xTaskGetCurrentTaskHandle(void) { return s_self; }
static void vTaskDelay(TickType_t ticks) {
  s_ticks+=ticks;++s_delays;
  if(s_ack_delay!=0 && s_delays>=s_ack_delay && s_wait_handle!=NULL)atomic_store(s_wait_handle,NULL);
  if(s_advance_epoch)atomic_fetch_add(&s_stream_epoch,1);
}
static esp_err_t sx1262_cmd_write(void *hal,uint8_t op,uint8_t *data,unsigned len) {
  assert(hal==&s_config.hal && op==0x80 && data[0]==0 && len==1 && s_irq_task_handle==NULL);return s_spi_error;
}
static esp_err_t sx1262_hal_destroy(void) { assert(s_irq_task_handle==NULL);++s_hal_frees;return s_hal_error; }
static void sx1262_radio_init(void *hal,void *config) { assert(hal==NULL && config==NULL);++s_radio_clears; }
static void sx1262_irq_init(void *hal,void *config,void *callbacks) { assert(hal==NULL && config==NULL && callbacks==NULL);++s_irq_clears; }
/* DRIVER */
static void sx1262_stop_rx(void) { ++s_stop_rx_calls; }
static atomic_bool s_mesh_running;
static _Atomic(TaskHandle_t) s_mesh_task_handle;
#define s_is_running s_mesh_running
/* MESH */
#undef s_is_running
static _Atomic(TaskHandle_t) s_poll_task;
static atomic_bool s_is_polling;
static unsigned s_bridge_stops,s_meshcore_stops;
static esp_err_t s_bridge_error;
static esp_err_t meshtastic_phone_bridge_stop(void) { assert(s_poll_task==NULL);++s_bridge_stops;return s_bridge_error; }
static esp_err_t meshcore_phone_bridge_stop(void) { assert(s_poll_task==NULL);++s_bridge_stops;return s_bridge_error; }
static void meshcore_stop(void) { ++s_meshcore_stops; }
/* MESHTASTIC */
/* MESHCORE */
#define BRIDGE_SPI_TIMEOUT_MS 20
#define SPI_ID_MESH_TORADIO_STREAM 1
#define SPI_ID_MCORE_RX_STREAM 2
static bool s_bridge_initialized,s_bridge_running;
static _Atomic(TaskHandle_t) s_bridge_task;
static void *s_saved_log;
static unsigned s_log_restores,s_outbound_clears,s_unregisters;
static void spi_bridge_unregister_stream_cb(int id) { assert(id==1 || id==2);++s_unregisters; }
static esp_err_t meshtastic_phone_bridge_ble_stop(void) { assert(s_bridge_task==NULL);return s_bridge_error; }
static esp_err_t meshtastic_phone_bridge_wifi_stop(void) { assert(s_bridge_task==NULL);return s_bridge_error; }
static esp_err_t meshcore_phone_bridge_ble_stop(void) { assert(s_bridge_task==NULL);return s_bridge_error; }
static void esp_log_set_vprintf(void *callback) { assert(callback!=NULL);++s_log_restores; }
static void meshcore_phoneapi_set_outbound(void *callback,void *ctx) { assert(callback==NULL && ctx==NULL);++s_outbound_clears; }
/* BRIDGES */
static void reset_wait(_Atomic(TaskHandle_t) *handle,unsigned delay) {
 s_ticks=s_delays=0;s_ack_delay=delay;s_wait_handle=handle;if(handle!=NULL)atomic_store(handle,(void *)1);
}
int main(void) {
 s_is_running=true;s_is_initialized=true;s_config.hal.spi_transfer=(void *)1;s_config.hal.ctx=(void *)1;s_config.hal.set_antenna=antenna_off;s_callbacks.callback=(void *)2;
 reset_wait(&s_irq_task_handle,0);assert(sx1262_deinit()==ESP_ERR_TIMEOUT && s_irq_task_handle!=NULL && s_hal_frees==0 && s_callbacks.callback!=NULL);
 reset_wait(&s_irq_task_handle,3);s_spi_error=ESP_FAIL;assert(sx1262_deinit()==ESP_FAIL && s_hal_frees==0 && s_is_initialized);
 s_spi_error=ESP_OK;s_hal_error=ESP_FAIL;assert(sx1262_deinit()==ESP_FAIL && s_config.hal.spi_transfer!=NULL && s_radio_clears==0);
 s_hal_error=ESP_OK;assert(sx1262_deinit()==ESP_OK && !s_is_initialized && s_config.hal.spi_transfer==NULL && s_callbacks.callback==NULL && s_radio_clears==1 && s_irq_clears==1);
 s_stream_task_handle=(void *)3;reset_wait(NULL,0);assert(spi_bridge_stream_barrier(10)==ESP_ERR_TIMEOUT);
 s_advance_epoch=true;assert(spi_bridge_stream_barrier(10)==ESP_OK);s_advance_epoch=false;
 s_self=s_stream_task_handle;assert(spi_bridge_stream_barrier(10)==ESP_ERR_INVALID_STATE);s_self=(void *)9;
 s_stream_task_handle=NULL;assert(spi_bridge_stream_barrier(10)==ESP_OK);
 s_is_polling=true;reset_wait(&s_poll_task,0);assert(meshtastic_app_stop()==ESP_ERR_TIMEOUT && s_bridge_stops==0);
 reset_wait(&s_poll_task,3);s_bridge_error=ESP_FAIL;assert(meshtastic_app_stop()==ESP_FAIL && s_bridge_stops==1 && s_stop_rx_calls==0);
 reset_wait(&s_poll_task,1);s_bridge_error=ESP_OK;s_mesh_task_handle=NULL;assert(meshtastic_app_stop()==ESP_OK && s_stop_rx_calls==1);
 s_mesh_running=true;reset_wait(&s_mesh_task_handle,0);assert(meshtastic_mesh_stop()==ESP_ERR_TIMEOUT && s_mesh_task_handle!=NULL && s_stop_rx_calls==1);
 reset_wait(&s_mesh_task_handle,2);assert(meshtastic_mesh_stop()==ESP_OK && s_stop_rx_calls==2);
 reset_wait(&s_poll_task,1);assert(meshcore_app_stop()==ESP_OK && s_meshcore_stops==1);
 s_bridge_initialized=true;s_bridge_running=true;s_saved_log=(void *)1;reset_wait(&s_bridge_task,0);
 assert(actual_mt_stop()==ESP_ERR_TIMEOUT && s_bridge_initialized && s_saved_log!=NULL && !s_bridge_running);
 reset_wait(&s_bridge_task,1);s_bridge_error=ESP_FAIL;
 assert(actual_mt_stop()==ESP_FAIL && s_bridge_initialized && s_log_restores==0);
 s_bridge_error=ESP_OK;assert(actual_mt_stop()==ESP_OK && !s_bridge_initialized && s_log_restores==1);
 s_bridge_initialized=true;reset_wait(&s_bridge_task,0);
 assert(actual_mc_stop()==ESP_ERR_TIMEOUT && s_bridge_initialized && s_outbound_clears==0);
 reset_wait(&s_bridge_task,1);s_bridge_error=ESP_FAIL;
 assert(actual_mc_stop()==ESP_FAIL && s_bridge_initialized && s_outbound_clears==0);
 s_bridge_error=ESP_OK;assert(actual_mc_stop()==ESP_OK && !s_bridge_initialized && s_outbound_clears==1 && s_unregisters==6);
 puts("PASS: actual IRQ/mesh/poll stop, delayed acknowledgement, callback barrier and HAL retention on failures");
 return 0;
}
