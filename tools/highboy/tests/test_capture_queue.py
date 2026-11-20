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

"""Exercise the production queue with a blocked writer and concurrent shutdown."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
SERVICE = ROOT / "firmware_p4/components/Service/wifi"
MOCK = r"""
#ifndef TEST_MOCK_H
#define TEST_MOCK_H
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <stdatomic.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_NO_MEM -4
#define ESP_ERR_TIMEOUT -5
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SYS_PRIO_SERVICE_LO 1
#define SYS_CORE_RADIO 0
#define portMAX_DELAY -1
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
typedef struct {pthread_mutex_t lock;pthread_cond_t cond;int count;} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
typedef pthread_t TaskHandle_t;
static atomic_bool fail_alloc,fail_task;
static SemaphoreHandle_t init_sem(StaticSemaphore_t *s,int count) {
 pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->cond,NULL);s->count=count;return s;
}
static SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s) {return init_sem(s,1);}
static SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *s) {return init_sem(s,0);}
static int xSemaphoreTake(SemaphoreHandle_t s,int wait) {
 pthread_mutex_lock(&s->lock);
 while(!s->count) {if(!wait){pthread_mutex_unlock(&s->lock);return 0;}pthread_cond_wait(&s->cond,&s->lock);}
 s->count--;pthread_mutex_unlock(&s->lock);return 1;
}
static void xSemaphoreGive(SemaphoreHandle_t s) {
 pthread_mutex_lock(&s->lock);s->count++;pthread_cond_signal(&s->cond);pthread_mutex_unlock(&s->lock);
}
static void *heap_caps_calloc(size_t n,size_t size,int caps) {(void)caps;return fail_alloc?NULL:calloc(n,size);}
static void led_signal_error(void) {}
static void led_signal_warning(void) {}
static void xTaskNotifyGive(TaskHandle_t t) {(void)t;}
static void ulTaskNotifyTake(int clear,int ms) {(void)clear;usleep(ms*1000);}
static void vTaskDeleteWithCaps(void *p) {(void)p;pthread_exit(NULL);}
struct launch {void (*fn)(void *);void *arg;};
static void *launch(void *p) {struct launch args=*(struct launch *)p;free(p);args.fn(args.arg);return NULL;}
static int xTaskCreatePinnedToCoreWithCaps(void (*fn)(void *),const char *name,int size,void *arg,
 int prio,TaskHandle_t *task,int core,int caps) {
 (void)name;(void)size;(void)prio;(void)core;(void)caps;if(fail_task)return 0;
 struct launch *args=malloc(sizeof(*args));assert(args);*args=(struct launch){fn,arg};
 return pthread_create(task,NULL,launch,args)==0;
}
#endif
"""
HARNESS = r"""
#include "mock.h"
#include "wifi_capture_queue.c"
static pthread_mutex_t test_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond=PTHREAD_COND_INITIALIZER;
static bool entered,released,write_fail;
static unsigned written;
static atomic_bool stopped;
static int stop_result;
static struct timeval before,after;
static esp_err_t write_frame(const wifi_capture_queue_frame_t *frame) {
 pthread_mutex_lock(&test_lock);entered=true;pthread_cond_broadcast(&test_cond);
 while(!released)pthread_cond_wait(&test_cond,&test_lock);
 assert(frame->len==1 && frame->data[0]==written);
 assert(frame->received_at.tv_sec>=before.tv_sec && frame->received_at.tv_sec<=after.tv_sec);
 written++;pthread_mutex_unlock(&test_lock);return write_fail?ESP_FAIL:ESP_OK;
}
static void *stopper(void *p) {(void)p;stop_result=wifi_capture_queue_stop();stopped=true;return NULL;}
static void run(bool fail) {
 entered=false;released=false;write_fail=fail;written=0;stopped=false;
 gettimeofday(&before,NULL);assert(wifi_capture_queue_start(write_frame)==ESP_OK);
 pthread_t worker=s_worker;uint8_t data=0;
 assert(wifi_capture_queue_push(&data,1)==ESP_OK);
 pthread_mutex_lock(&test_lock);while(!entered)pthread_cond_wait(&test_cond,&test_lock);pthread_mutex_unlock(&test_lock);
 for(unsigned i=1;i<WIFI_CAPTURE_QUEUE_CAPACITY;i++) {data=i;assert(wifi_capture_queue_push(&data,1)==ESP_OK);}
 data=255;assert(wifi_capture_queue_push(&data,1)==ESP_ERR_NO_MEM);
 gettimeofday(&after,NULL);
 wifi_capture_queue_stats_t stats;wifi_capture_queue_get_stats(&stats);
 assert(stats.pending==64 && stats.peak==64 && stats.dropped==1);
 pthread_t stop_thread;pthread_create(&stop_thread,NULL,stopper,NULL);
 usleep(30000);assert(!stopped);
 pthread_mutex_lock(&test_lock);released=true;pthread_cond_broadcast(&test_cond);pthread_mutex_unlock(&test_lock);
 pthread_join(stop_thread,NULL);pthread_join(worker,NULL);
 assert(stop_result==(fail?ESP_FAIL:ESP_OK));assert(written==(fail?1:64));
 wifi_capture_queue_get_stats(&stats);assert(stats.pending==0 && stats.discarded==(fail?64:0));
 assert(wifi_capture_queue_stop()==stop_result);
 assert(wifi_capture_queue_push(&data,1)==ESP_ERR_INVALID_STATE);
}
int main(void) {
 assert(wifi_capture_queue_start(NULL)==ESP_ERR_INVALID_ARG);
 fail_alloc=true;assert(wifi_capture_queue_start(write_frame)==ESP_ERR_NO_MEM);fail_alloc=false;
 fail_task=true;assert(wifi_capture_queue_start(write_frame)==ESP_ERR_NO_MEM);fail_task=false;
 run(false);run(true);run(false);
 puts("PASS: queue copy/FIFO/timestamps, overflow, drain, SD failure and restart");
}
"""
with tempfile.TemporaryDirectory(prefix="capture-queue-") as folder:
    root=Path(folder)
    (root/"mock.h").write_text(MOCK)
    for name in ["esp_err.h","esp_heap_caps.h","esp_log.h","led_control.h","sys_prio.h",
                 "freertos/FreeRTOS.h","freertos/idf_additions.h","freertos/semphr.h","freertos/task.h"]:
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "mock.h"\n')
    (root/"test.c").write_text(HARNESS)
    binary=root/"test"
    subprocess.run(["cc","-std=gnu11","-Wall","-Wextra","-Werror","-Wno-int-conversion",
                    "-fsanitize=address,undefined","-fno-sanitize-recover=all","-pthread",
                    f"-I{root}",f"-I{SERVICE}",f"-I{SERVICE/'include'}",str(root/"test.c"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=15)
