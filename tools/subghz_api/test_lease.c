#include "test_platform.h"
#include "cc1101.h"
#include "cc1101_lease.h"
#include "resource_mgr.h"
static uintptr_t s_task=1;
static unsigned s_calls;
static bool s_check_inflight,s_resource_busy,s_has_grant,s_race_operation;
static uint8_t buffer[8];
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)s_task; }
esp_err_t resource_acquire(const res_request_t *request,res_handle_t *out) {
  assert(request->id==RES_SUBGHZ && request->owner_kind==RES_OWNER_SYSTEM && !request->allow_preempt);
  if(s_resource_busy || s_has_grant)return ESP_ERR_INVALID_STATE;
  s_has_grant=true;*out=42;
  if(s_race_operation)assert(cc1101_lease_enter());
  return ESP_OK;
}
esp_err_t resource_release(res_handle_t grant) { assert(grant==42 && s_has_grant);s_has_grant=false;return ESP_OK; }
static const char *TAG="TEST_CC1101";
#define CC1101_BUS_LOCK_TIMEOUT_MS 1000
#define SPI_TRANS_USE_TXDATA 1
typedef struct { unsigned length,flags;uint8_t tx_data[8]; } spi_transaction_t;
static void *s_cc1101_spi=(void *)1;
static bool s_spi_lock_ok=true;
static esp_err_t s_spi_error;
static bool spi_bus_lock_take(uint32_t timeout) { return s_spi_lock_ok; }
static void spi_bus_lock_give(void) {}
static esp_err_t spi_device_transmit(void *spi,spi_transaction_t *transaction) { return s_spi_error; }
void ets_delay_us(uint32_t delay) {}
/* TRANSACTIONS */
/* WRAPPERS */
int main(void) {
  s_task=0;assert(cc1101_lease_acquire()==ESP_ERR_INVALID_STATE);s_task=1;
  s_resource_busy=true;assert(cc1101_lease_acquire()==ESP_ERR_INVALID_STATE);s_resource_busy=false;
  s_check_inflight=true;
  /* NATIVE_CALLS */
  assert(s_calls==/* COUNT */);s_check_inflight=false;
  s_race_operation=true;
  assert(cc1101_lease_acquire()==ESP_ERR_INVALID_STATE && !s_has_grant);
  cc1101_lease_leave();s_race_operation=false;
  assert(cc1101_lease_acquire()==ESP_OK && s_has_grant);
  assert(cc1101_lease_acquire()==ESP_OK);
  unsigned calls=s_calls;
  s_task=2;
  assert(cc1101_lease_acquire()==ESP_ERR_INVALID_STATE);
  assert(cc1101_lease_release()==ESP_ERR_INVALID_STATE);
  memset(buffer,0xff,sizeof(buffer));
  /* FOREIGN_CALLS */
  assert(s_calls==calls && buffer[0]==0 && buffer[2]==0);
  s_task=1;
  /* NATIVE_CALLS */
  assert(s_calls==calls+/* COUNT */);
  calls=s_calls;cc1101_lease_pause();
  /* FOREIGN_CALLS */
  assert(s_calls==calls);
  assert(cc1101_lease_acquire()==ESP_OK);
  spi_transaction_t transaction={0};
  s_spi_lock_ok=false;assert(cc1101_txn(&transaction)==ESP_ERR_TIMEOUT);
  assert(cc1101_lease_take_error()==ESP_ERR_TIMEOUT);
  s_spi_lock_ok=true;s_spi_error=ESP_FAIL;
  assert(cc1101_reset_radio()==ESP_FAIL && cc1101_lease_take_error()==ESP_FAIL);
  s_spi_error=ESP_OK;assert(cc1101_txn(&transaction)==ESP_OK);
  cc1101_lease_record_error(ESP_ERR_TIMEOUT);
  cc1101_lease_record_error(ESP_FAIL);
  assert(cc1101_lease_take_error()==ESP_ERR_TIMEOUT && cc1101_lease_take_error()==ESP_OK);
  assert(cc1101_lease_enter());assert(cc1101_lease_release()==ESP_ERR_INVALID_STATE);
  cc1101_lease_leave();assert(cc1101_lease_release()==ESP_OK && !s_has_grant);
  puts("PASS: all CC1101 wrappers reject foreign owners; native in-flight/start races and error latching");
}
