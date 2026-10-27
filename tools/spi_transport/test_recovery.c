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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include "spi_protocol.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NO_MEM -4
#define ESP_ERR_INVALID_RESPONSE -5
#define ESP_ERR_INVALID_SIZE -6
#define ESP_ERR_INVALID_ARG -7
#define ESP_RST_SW 1
#define DMA_ATTR
#define RTC_NOINIT_ATTR
#define SPI_PAIR_MAX_BYTES 2080
#define SPI2_HOST 2
#define GPIO_BRIDGE_IRQ_PIN 3
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef uint32_t TickType_t;
typedef struct {size_t length,trans_len; const void *tx_buffer; void *rx_buffer;} spi_slave_transaction_t;
static spi_slave_transaction_t *queued[8], *unexpected;
static unsigned queue_count,queue_calls,fail_call,restarts,halts;
static int s_pair_lock,reset_reason;
static bool complete,short_transfer,s_is_restart_pending,s_is_download_pending;
static int64_t clock_us;
static jmp_buf reboot;
static int64_t esp_timer_get_time(void){return clock_us;}
static void vTaskDelay(uint32_t ms){clock_us+=(int64_t)ms*1000;}
static int esp_reset_reason(void){return reset_reason;}
static void esp_restart(void){restarts++;longjmp(reboot,1);}
static void vTaskDelete(void *p){(void)p;halts++;}
static void grok_wifi_stop(void){}
static void hb_muse_c5_service_revoke(void){}
static void gpio_set_level(int pin,int level){(void)pin;(void)level;}
static bool esp_ptr_word_aligned(const void *p){return !((uintptr_t)p&3);}
static bool esp_ptr_dma_capable(const void *p){return p!=NULL;}
static esp_err_t spi_slave_queue_trans(int host,spi_slave_transaction_t *t,uint32_t ticks){
  (void)host;assert(!ticks);queue_calls++;
  if(fail_call==queue_calls)return ESP_ERR_NO_MEM;
  assert(queue_count<8);queued[queue_count++]=t;return ESP_OK;
}
static esp_err_t spi_slave_get_trans_result(int host,spi_slave_transaction_t **out,uint32_t ticks){
  (void)host;
  if(unexpected){*out=unexpected;unexpected=NULL;return ESP_OK;}
  if(!complete || !queue_count){if(ticks!=portMAX_DELAY)clock_us+=(int64_t)ticks*1000;return ESP_ERR_TIMEOUT;}
  *out=queued[0];memmove(queued,queued+1,--queue_count*sizeof(*queued));
  (*out)->trans_len=short_transfer?0:(*out)->length;return ESP_OK;
}
/* PRODUCTION */
static void reset_test(void){
  memset(s_pending,0,sizeof(s_pending));s_has_unknown_owner=false;
  queue_count=0;queue_calls=0;fail_call=0;complete=true;short_transfer=false;unexpected=NULL;
  s_recovery_attempts=0;s_recovery_restarts=0;s_recovery_check=~s_recovery_restarts;
}
int main(void){
  uint32_t tx[80]={0},rx[80]={0};spi_slave_transaction_t a,b,other;
  reset_reason=0;recovery_budget_init();
  reset_test();
  // A second enqueue failure leaves TX owned; no descriptor may be overwritten.
  fail_call=2;
  assert(spi_slave_driver_queue_response_and_receive(&a,(void*)tx,sizeof(tx),&b,(void*)rx,sizeof(rx))==ESP_ERR_NO_MEM);
  assert(has_pending() && queue_count==1 && s_pending[2]==&a);
  size_t length=a.length;
  assert(spi_slave_driver_queue(&a,NULL,(void*)rx,4)==ESP_ERR_INVALID_STATE && a.length==length);
  complete=false;
  assert(spi_slave_driver_recover(30)==ESP_ERR_TIMEOUT && has_pending());
  complete=true;
  assert(spi_slave_driver_recover(30)==ESP_OK && !has_pending());
  // Identity mismatch still releases the returned known descriptor, not its peer.
  fail_call=0;
  assert(spi_slave_driver_queue_response_and_receive(&a,(void*)tx,sizeof(tx),&b,(void*)rx,sizeof(rx))==ESP_OK);
  assert(spi_slave_driver_wait_for(&b)==ESP_ERR_INVALID_RESPONSE && has_pending());
  assert(spi_slave_driver_recover(30)==ESP_OK && !has_pending());
  // A short transfer has returned ownership and can be recovered without reboot.
  assert(spi_slave_driver_queue(&a,(void*)tx,NULL,sizeof(tx))==ESP_OK);short_transfer=true;
  assert(spi_slave_driver_wait_for_ticks(&a,10)==ESP_ERR_INVALID_SIZE && !has_pending());
  short_transfer=false;
  assert(recover_transport("test",ESP_FAIL,123));
  assert(!has_pending() && !queue_count && !restarts);
  // Unknown completions must never make the owner ledger appear clean.
  unexpected=&other;
  assert(spi_slave_driver_wait_for(&a)==ESP_ERR_INVALID_RESPONSE);
  assert(spi_slave_driver_recover(30)==ESP_ERR_INVALID_STATE);
  assert(spi_slave_driver_queue(&a,NULL,(void*)rx,sizeof(rx))==ESP_ERR_INVALID_STATE);
  if(!setjmp(reboot)){recover_transport("unknown",ESP_FAIL,123);assert(false);}
  assert(restarts==1 && s_recovery_restarts==1);
  reset_reason=ESP_RST_SW;recovery_budget_init();assert(s_recovery_restarts==1);
  // RTC budget survives software restarts; the third failure parks, not reboots.
  if(!setjmp(reboot)){recover_transport("unknown",ESP_FAIL,123);assert(false);}
  assert(restarts==2 && s_recovery_restarts==2);recovery_budget_init();
  assert(!recover_transport("unknown",ESP_FAIL,123) && halts==1 && restarts==2);
  // Bounded TX wait retains DMA ownership when there is no master clock.
  reset_test();complete=false;
  assert(spi_slave_driver_queue(&a,(void*)tx,NULL,sizeof(tx))==ESP_OK);
  assert(spi_slave_driver_wait_for_ticks(&a,10)==ESP_ERR_TIMEOUT && has_pending());
  if(!setjmp(reboot)){recover_transport("stuck",ESP_FAIL,123);assert(false);}
  assert(has_pending() && queue_count==1);
  // Repeated successful recoveries still consume the per-boot safety budget.
  reset_test();
  for(unsigned i=0;i<SPI_RECOVERY_MAX_ATTEMPTS;i++)
    assert(recover_transport("retry",ESP_FAIL,123));
  if(!setjmp(reboot)){recover_transport("limit",ESP_FAIL,123);assert(false);}
  assert(s_recovery_restarts==1);
  // Notice timeout retains its descriptor until restart, never frees/reuses DMA.
  reset_test();complete=false;
  int64_t started=clock_us;
  if(!setjmp(reboot)){recover_transport("notice",ESP_FAIL,123);assert(false);}
  assert(has_pending() && queue_count==1);
  assert(clock_us-started==(int64_t)SPI_RECOVERY_TIMEOUT_MS*1000);
  // If the notice itself is short, returned ownership still permits rearming.
  reset_test();short_transfer=true;
  assert(recover_transport("short_notice",ESP_FAIL,123) && !has_pending());
  puts("PASS: production DMA ownership, partial enqueue, drain timeout, unknown completion, recovery and reboot budget");
}
