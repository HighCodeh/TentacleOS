#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Execute the production BLE entry/cleanup paths with injected failures."""
from pathlib import Path
import os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3]
BT=ROOT/'firmware_p4/components/Applications/bluetooth'
def run(label,source):
 with tempfile.TemporaryDirectory(prefix=label) as tmp:
  p=Path(tmp);(p/'test.c').write_text(source)
  subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-variable','-fsanitize=undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
common=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 100
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define SYS_PRIO_BACKGROUND_LO 1
#define SYS_CORE_RADIO 0
#define SYS_CORE_MAIN 0
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int allocations,fail_at,calls,start_error,starts;
static void *allocated[32];static int allocation_caps[32];
static int caps_of(void *p){for(int i=0;i<32;i++)if(allocated[i]==p)return allocation_caps[i];assert(0);return 0;}
static void *allocate(size_t n,int caps){calls++;if(calls==fail_at)return NULL;allocations++;void*p=calloc(1,n);for(int i=0;i<32;i++)if(!allocated[i]){allocated[i]=p;allocation_caps[i]=caps;return p;}assert(0);return NULL;}
static void release(void *p){if(p){int found=0;for(int i=0;i<32;i++)if(allocated[i]==p){allocated[i]=NULL;found=1;break;}assert(found);allocations--;free(p);}}
#define heap_caps_malloc allocate
#define free release
typedef void (*packet_cb_t)(const uint8_t*,uint8_t,int,const uint8_t*,uint16_t);
static packet_cb_t callback;
static int bluetooth_service_start_sniffer(packet_cb_t cb){starts++;callback=cb;return start_error;}
static void bluetooth_service_stop_sniffer(void){callback=NULL;}
"""
sniffer=r"""
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef struct {int dummy;} StaticQueue_t;
typedef struct {const uint8_t *addr;uint8_t addr_type;int rssi;const uint8_t *data;uint8_t len;} ble_sniffer_adv_t;
typedef void (*ble_sniffer_observer_t)(const ble_sniffer_adv_t*);
static int task_fail,queue_fail,sends,tasks,queue_deleted;
static QueueHandle_t xQueueCreateStatic(int n,size_t size,uint8_t *data,StaticQueue_t *control){assert(caps_of(control)==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));assert(caps_of(data)==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return queue_fail?NULL:control;}
static int xTaskCreatePinnedToCoreWithCaps(void(*fn)(void*),const char*name,int stack,void*ctx,int prio,TaskHandle_t *handle,int core,int caps){assert(stack==4096&&core==0);assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(task_fail)return 0;*handle=(void*)1;tasks++;return pdPASS;}
static void vTaskDeleteWithCaps(TaskHandle_t t){assert(tasks==1);tasks--;}
static void vQueueDelete(QueueHandle_t q){assert(q);assert(tasks==0);queue_deleted++;}
static int xQueueSend(QueueHandle_t q,const void*p,int ticks){assert(q);sends++;return 1;}
static int xQueueReceive(QueueHandle_t q,void*p,int ticks){return 0;}
void ble_sniffer_stop(void);
"""
source=re.sub(r'^#include.*\n','', (BT/'ble_sniffer.c').read_text(),flags=re.M)
checks=r"""
int main(void){
 for(int failure=1;failure<=2;failure++){
  calls=0;fail_at=failure;assert(ble_sniffer_start()==ESP_ERR_NO_MEM);assert(allocations==0&&tasks==0);assert(starts==0);
 }
 fail_at=0;queue_fail=1;assert(ble_sniffer_start()==ESP_ERR_NO_MEM);assert(allocations==0&&tasks==0);
 queue_fail=0;task_fail=1;assert(ble_sniffer_start()==ESP_ERR_NO_MEM);assert(allocations==0&&tasks==0);
 task_fail=0;start_error=ESP_FAIL;assert(ble_sniffer_start()==ESP_FAIL);assert(allocations==0&&tasks==0);
 start_error=0;assert(ble_sniffer_start()==ESP_OK);assert(allocations==2&&tasks==1);
 int old=starts;assert(ble_sniffer_start()==ESP_OK&&starts==old);
 uint8_t a[6]={0},data[40]={0};packet_cb_t late=callback;late(a,0,-50,data,sizeof(data));assert(sends==1);
 ble_sniffer_stop();assert(allocations==0&&tasks==0&&s_sniffer_queue==NULL);
 late(a,0,-50,data,sizeof(data));assert(sends==1);ble_sniffer_stop();assert(allocations==0);
 return 0;
}
"""
run('ble-sniffer-',common+sniffer+source+checks)
c5=r"""
typedef int spi_id_t;
#define SPI_SESSION_INVALID_ID 0
#define SPI_ID_BT_APP_SNIFFER 1
typedef struct {uint8_t addr[6],addr_type;int8_t rssi;uint8_t len,data[31];} spi_ble_sniffer_frame_t;
static bool stream_enabled;
static int spi_bridge_stream_is_enabled(int op){return stream_enabled;}
static void spi_bridge_stream_enable(int op,bool enable){assert(op==SPI_ID_BT_APP_SNIFFER);stream_enabled=enable;}
static void spi_bridge_stream_push(int op,const uint8_t *data,size_t n){}
static void session_manager_try_emit(uint32_t id,const uint8_t *data,size_t n){}
"""
source=re.sub(r'^#include.*\n','', (ROOT/'firmware_c5/components/Applications/bluetooth/ble_sniffer.c').read_text(),flags=re.M)
c5_checks=checks.replace(' return 0;\n}', '''
 for(int cycle=0;cycle<30;cycle++){
  assert(ble_sniffer_start()==ESP_OK);assert(!stream_enabled);ble_sniffer_bind_session(17);
  assert(s_session_id==17&&stream_enabled);late=callback;ble_sniffer_session_killed(SPI_ID_BT_APP_SNIFFER);
  assert(s_session_id==SPI_SESSION_INVALID_ID&&!stream_enabled&&allocations==0&&tasks==0);
  old=sends;late(a,0,-50,data,sizeof(data));assert(sends==old);
 }
 return 0;
}''')
run('ble-sniffer-c5-',common+sniffer+c5+source+c5_checks)
# Reset/getters must be safe before the first start; failed start must be retryable.
exposure=r"""
typedef void *SemaphoreHandle_t;
static int mutex_fail,mutex_takes;
static SemaphoreHandle_t xSemaphoreCreateMutex(void){return mutex_fail?NULL:(void*)1;}
static int xSemaphoreTake(SemaphoreHandle_t m,int ticks){assert(m);mutex_takes++;return pdTRUE;}
static void xSemaphoreGive(SemaphoreHandle_t m){assert(m);}
static uint64_t esp_timer_get_time(void){return 0;}
typedef struct {uint8_t addr[6];int rssi;uint32_t last_seen;} exposure_notification_device_t;
"""
source=re.sub(r'^#include.*\n','', (BT/'exposure_notification.c').read_text(),flags=re.M)
checks=r"""
int main(void){uint16_t count=99;
 exposure_notification_reset();assert(exposure_notification_get_count()==0);
 assert(exposure_notification_get_list(&count)==NULL&&count==0&&mutex_takes==0);
 assert(exposure_notification_get_list(NULL)==NULL);
 mutex_fail=1;assert(exposure_notification_start()==ESP_ERR_NO_MEM);assert(!s_is_running&&starts==0);
 mutex_fail=0;fail_at=1;assert(exposure_notification_start()==ESP_ERR_NO_MEM);assert(!s_is_running&&starts==0);
 fail_at=0;start_error=ESP_FAIL;assert(exposure_notification_start()==ESP_FAIL);assert(!s_is_running);
 start_error=0;assert(exposure_notification_start()==ESP_OK);assert(s_is_running);
 assert(caps_of(s_device_list)&MALLOC_CAP_SPIRAM);
 exposure_notification_stop();exposure_notification_reset();assert(!s_is_running);
 free(s_device_list);assert(allocations==0);return 0;
}
"""
run('ble-exposure-',common+exposure+source+checks)
print('P4/C5 BLE memory placement, allocation failure, retry, late callback cleanup and P4 cold Exposure entry: passed')
