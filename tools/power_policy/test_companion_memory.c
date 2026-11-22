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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spi_protocol.h"
#define ESP_OK 0
#define ESP_ERR_NO_MEM -1
#define ESP_ERR_NOT_SUPPORTED -2
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SYS_PRIO_REALTIME 10
#define SYS_CORE_MAIN 0
#define pdPASS 1
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
typedef int esp_err_t;
#define WIFI_SCAN_LIST_SIZE 64
static const char *TAG="TEST";
static bool allocation_fail, is_isr, is_enabled=true, task_fail, driver_fail;
static bool s_use_irq;
static unsigned allocations, driver_starts, task_starts, lock_depth;
static int s_stream_mux;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!lock_depth);lock_depth++;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(lock_depth==1);lock_depth--;}while(0)
static void *heap_caps_calloc(size_t count,size_t size,int caps) {
 assert(!lock_depth && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));allocations++;
 return allocation_fail?NULL:calloc(count,size);
}
static bool xPortInIsrContext(void){return is_isr;}
static bool spi_bridge_stream_is_enabled(spi_id_t id){(void)id;return is_enabled;}
static int spi_slave_driver_init(void){driver_starts++;return driver_fail?-3:ESP_OK;}
static void hb_muse_c5_service_register(void){}
static void session_manager_init(void){}
static int wifi_quick_scan_init(void){return ESP_OK;}
static void bridge_task(void *arg){(void)arg;}
static int xTaskCreatePinnedToCore(void (*task)(void *),const char *name,unsigned stack,void *arg,int priority,void *out,int core) {
 assert(task==bridge_task && name && stack && !arg && !out && priority==10 && core==0);task_starts++;return !task_fail;
}
/* PRODUCTION */
int main(void) {
 uint8_t value=1, buffer[300];
 assert(!spi_bridge_stream_push(SPI_ID_SYSTEM_LOG,&value,1));
 allocation_fail=true;
 assert(spi_bridge_slave_init_mode(SPI_BRIDGE_MODE_POLL)==ESP_ERR_NO_MEM && driver_starts==0 && task_starts==0);
 assert(!ensure_scan_storage() && s_scan_storage==NULL);
 allocation_fail=false;
 assert(spi_bridge_slave_init_mode(SPI_BRIDGE_MODE_POLL)==ESP_OK && driver_starts==1 && task_starts==1);
 assert(ensure_scan_storage());unsigned before=allocations;assert(ensure_scan_storage() && allocations==before);
 s_scan_storage->records[63].channel=13;s_scan_storage->details[63].channel=36;s_scan_storage->steps[63].channel=6;
 assert(s_scan_storage->records[63].channel==13 && s_scan_storage->details[63].channel==36);
 is_isr=true;assert(!spi_bridge_stream_push(SPI_ID_SYSTEM_LOG,&value,1));is_isr=false;
 is_enabled=false;assert(!spi_bridge_stream_push(SPI_ID_SYSTEM_LOG,&value,1));is_enabled=true;
 assert(!spi_bridge_stream_push(SPI_ID_SYSTEM_LOG,NULL,1));
 for(unsigned cycle=0;cycle<3;cycle++) {
  for(unsigned i=0;i<32;i++){value=(uint8_t)(i+cycle);assert(spi_bridge_stream_push(SPI_ID_SYSTEM_LOG,&value,1));}
  assert(!spi_bridge_stream_push(SPI_ID_SYSTEM_LOG,&value,1));
  assert(stream_pop_into(buffer,0,3)==0 && s_stream_count==32);
  for(unsigned i=0;i<32;i++){assert(stream_pop_into(buffer,7,sizeof(buffer))==4);assert(buffer[10]==i+cycle);}
  assert(stream_pop_into(buffer,0,sizeof(buffer))==0 && !lock_depth);
 }
 task_fail=true;assert(spi_bridge_slave_init_mode(SPI_BRIDGE_MODE_POLL)==ESP_ERR_NO_MEM && allocations==before);
 free(s_scan_storage);free(s_stream_queue);
 puts("PASS: C5 PSRAM allocation failure/retry, bounded persistent scan storage, task failure, ISR rejection, FIFO wrap and backpressure");
}
