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

"""Exercise production Companion queue initialization with allocation and task failures."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / "firmware_p4/components/Service/host_link"

def function(text, name):
    start = text.index("esp_err_t " + name + "(void) {")
    return text[start:text.index("\n}", start) + 2]

fixture = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
typedef int esp_err_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef struct {int unused;} StaticQueue_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_FAIL 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdPASS 1
#define portMAX_DELAY 0
#define SYS_PRIO_SERVICE_HI 1
#define SYS_PRIO_BACKGROUND 1
#define SYS_CORE_RADIO 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define HOST_LINK_RX_DEPTH 64
#define HOST_LOG_QUEUE_DEPTH 24
#define HOST_LOG_TASK_STK 6144
static bool fail_alloc, fail_queue, fail_task;
static unsigned allocations, hooks, tasks;
static void *heap_caps_malloc(size_t n,unsigned caps) {
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(fail_alloc)return NULL;
 void *p=malloc(n);assert(p);allocations++;return p;
}
static void heap_caps_free(void *p){if(p){assert(allocations);allocations--;free(p);}}
static QueueHandle_t xQueueCreateStatic(unsigned n,size_t size,uint8_t *data,StaticQueue_t *ctrl){
 assert(n&&size&&data&&ctrl);return fail_queue?NULL:ctrl;
}
static void vQueueDelete(QueueHandle_t q){assert(q);}
static void xSemaphoreTakeRecursive(void *s,int wait){(void)wait;assert(s);}
static void xSemaphoreGiveRecursive(void *s){assert(s);}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,unsigned stack,
 void *arg,int prio,TaskHandle_t *out,int core){
 (void)fn;(void)name;(void)stack;(void)arg;(void)prio;(void)core;
 if(fail_task)return 0;*out=(void*)1;tasks++;return pdPASS;
}
static void *esp_log_set_vprintf(void *f){assert(f);hooks++;return f;}
static void host_link_worker(void *p){(void)p;}
static void log_task(void *p){(void)p;}
static void *log_vprintf=(void*)1,*s_prev_vprintf;
typedef struct {uint8_t data[76];} rx_item_t;
typedef struct {uint8_t data[248];} log_line_t;
static bool s_initialized=true,s_started;
static void *s_lock=(void*)1,*s_worker,*s_log_task;
static QueueHandle_t s_rx_queue,s_log_queue;
static StaticQueue_t s_rx_queue_control,s_log_queue_control;
static uint8_t *s_rx_queue_storage,*s_log_queue_storage;
"""
tests = r"""
int main(void){
 fail_alloc=true;assert(host_link_start_worker()==ESP_ERR_NO_MEM);
 assert(host_link_log_init()==ESP_ERR_NO_MEM);assert(!allocations&&!hooks);
 fail_alloc=false;fail_queue=true;
 assert(host_link_start_worker()==ESP_ERR_NO_MEM);
 assert(host_link_log_init()==ESP_ERR_NO_MEM);assert(!allocations&&!hooks);
 fail_queue=false;fail_task=true;
 assert(host_link_start_worker()==ESP_ERR_NO_MEM);
 assert(host_link_log_init()==ESP_FAIL);assert(!allocations&&!hooks);
 assert(!s_rx_queue&&!s_log_queue&&!s_rx_queue_storage&&!s_log_queue_storage);
 fail_task=false;
 assert(host_link_start_worker()==ESP_OK);assert(host_link_log_init()==ESP_OK);
 assert(allocations==2&&tasks==2&&hooks==1);
 assert(host_link_start_worker()==ESP_OK);assert(host_link_log_init()==ESP_OK);
 assert(allocations==2&&tasks==2&&hooks==1);
 heap_caps_free(s_rx_queue_storage);heap_caps_free(s_log_queue_storage);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    code = fixture + function((SERVICE / "host_link.c").read_text(), "host_link_start_worker")
    code += function((SERVICE / "host_link_log.c").read_text(), "host_link_log_init") + tests
    (path / "test.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Companion queue PSRAM allocation, failure cleanup and retry tests passed.")
