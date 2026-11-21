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

// Host-only hardware fakes; the BLE lifecycle implementation itself is included unchanged.

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((ms) == 0 ? 0 : (ms) / 100 + 1)
#define NIMBLE_HS_STACK_SIZE 4096
#define SYS_PRIO_BLE_HOST 21
#define SYS_PRIO_SERVICE_HI 5
#define SYS_CORE_MAIN 0
#define ESP_BT_CONTROLLER_STATUS_IDLE 0
#define ESP_BT_CONTROLLER_STATUS_INITED 1
#define ESP_BT_CONTROLLER_STATUS_ENABLED 2
#define BLE_SCAN_LIST_SIZE 50
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 3
#define BLE_HS_FOREVER (-1)

typedef struct { pthread_mutex_t mutex; pthread_cond_t cond; unsigned count; } semaphore_t;
typedef semaphore_t *SemaphoreHandle_t;
typedef struct { pthread_t thread; void (*entry)(void *); void *arg; } task_t;
typedef task_t *TaskHandle_t;
typedef void (*bluetooth_service_sniffer_cb_t)(const uint8_t *,uint8_t,int,const uint8_t *,uint16_t);
typedef void (*bluetooth_service_tracker_cb_t)(int);
typedef struct { uint8_t data[32]; } bluetooth_service_scan_result_t;
struct ble_gap_disc_params { int filter_duplicates, passive, itvl, window; };
struct ble_gap_event { int unused; };
static struct { void (*reset_cb)(int); void (*sync_cb)(void); int store_status_cb; } ble_hs_cfg;
static int ble_store_util_status_rr;
static atomic_int sem_count, task_count, controller_state, host_inits, host_deinits;
static atomic_int sem_calls, fail_sem, init_error, nvs_error, name_error, infer_error;
static atomic_int disable_error, controller_error, host_deinit_error, stop_error;
static atomic_bool fail_host_task, fail_stop_task, no_sync, hold_stop, hold_exit;
static atomic_bool exit_requested, host_live, host_loaded, explorer_busy, explorer_error;
static atomic_bool scan_no_event;
static int discovery_error;

/* GLOBALS */

static void tick(void) { struct timespec delay={.tv_nsec=1000000}; nanosleep(&delay,NULL); }
static const char *esp_err_to_name(int error) { return "fake"; }
static SemaphoreHandle_t xSemaphoreCreateBinary(void) {
  if (++sem_calls == fail_sem) return NULL;
  semaphore_t *s=calloc(1,sizeof(*s)); assert(s);
  pthread_mutex_init(&s->mutex,NULL); pthread_cond_init(&s->cond,NULL); ++sem_count; return s;
}
static int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout) {
  assert(s); struct timespec until; clock_gettime(CLOCK_REALTIME,&until);
  until.tv_nsec += (long)timeout*1000000;
  until.tv_sec += until.tv_nsec/1000000000; until.tv_nsec %= 1000000000;
  pthread_mutex_lock(&s->mutex);
  while (!s->count && timeout) {
    int rc=pthread_cond_timedwait(&s->cond,&s->mutex,&until);
    if (rc==ETIMEDOUT) break;
    assert(rc==0);
  }
  int got=s->count!=0; if(got)--s->count;
  pthread_mutex_unlock(&s->mutex); return got;
}
static void xSemaphoreGive(SemaphoreHandle_t s) {
  assert(s); pthread_mutex_lock(&s->mutex); s->count=1;
  pthread_cond_broadcast(&s->cond); pthread_mutex_unlock(&s->mutex);
}
static void vSemaphoreDelete(SemaphoreHandle_t s) {
  assert(s && !host_live && !task_count);
  pthread_cond_destroy(&s->cond); pthread_mutex_destroy(&s->mutex); free(s); --sem_count;
}
static void *run_task(void *arg) { task_t *task=arg; task->entry(task->arg); return NULL; }
static int xTaskCreatePinnedToCore(void (*entry)(void *),const char *name,unsigned stack,
                                  void *arg,int priority,TaskHandle_t *out,int core) {
  assert(core==SYS_CORE_MAIN && (priority==SYS_PRIO_BLE_HOST || priority==SYS_PRIO_SERVICE_HI));
  if ((entry==host_task && fail_host_task) || (entry==stop_task && fail_stop_task))return 0;
  task_t *t=calloc(1,sizeof(*t)); assert(t); t->entry=entry; t->arg=arg; *out=t; ++task_count;
  assert(pthread_create(&t->thread,NULL,run_task,t)==0); return pdPASS;
}
static void vTaskDelete(TaskHandle_t task) {
  assert(task); assert(pthread_join(task->thread,NULL)==0); free(task); --task_count;
}
static void vTaskSuspend(void *task) { assert(task==NULL); pthread_exit(NULL); }
static int nvs_flash_init(void) { return nvs_error; }
static int esp_bt_controller_get_status(void) { return controller_state; }
static int nimble_port_init(void) {
  assert(controller_state==0 && !host_loaded); ++host_inits;
  if (init_error) return init_error;
  controller_state=2; host_loaded=true; return 0;
}
static int esp_nimble_deinit(void) {
  assert(host_loaded && !host_live); if(host_deinit_error)return host_deinit_error;
  host_loaded=false; ++host_deinits; return 0;
}
static int esp_bt_controller_disable(void) {
  assert(controller_state==2 && !host_loaded); if(disable_error)return disable_error;
  controller_state=1; return 0;
}
static int esp_bt_controller_deinit(void) {
  assert(controller_state==1 && !host_loaded); if(controller_error)return controller_error;
  controller_state=0; return 0;
}
static int ble_svc_gap_device_name_set(const char *name) { return name_error; }
static int ble_hs_id_infer_auto(int privacy,uint8_t *address) { *address=0; return infer_error; }
static void load_announce_config(char *name,uint8_t *max_conn) {}
static int gap_event(struct ble_gap_event *event,void *arg) { return 0; }
static bool gatt_explorer_is_busy(void) { return explorer_busy; }
static int gatt_explorer_deinit(void) { return explorer_error ? ESP_FAIL : ESP_OK; }
static void ble_hid_stack_reset(void) {}
static void nimble_port_run(void) {
  assert(host_loaded); exit_requested=false; host_live=true;
  if(!no_sync)ble_hs_cfg.sync_cb();
  while(!exit_requested || hold_exit)tick();
  host_live=false;
}
static int nimble_port_stop(void) {
  while(hold_stop)tick();
  if(stop_error)return stop_error;
  exit_requested=true; return 0;
}
static int bluetooth_service_stop_advertising(void) { s_adv_requested=false; return 0; }
static int bluetooth_service_start_advertising(void) { s_adv_requested=true; return 0; }
static int ble_gap_disc(uint8_t address,uint32_t duration,struct ble_gap_disc_params *params,
                         int (*callback)(struct ble_gap_event *,void *),void *arg) {
  if(discovery_error)return discovery_error;
  if(!scan_no_event)xSemaphoreGive(s_scan_sem); return 0;
}
static int ble_gap_disc_cancel(void) { return 0; }

/* FUNCTIONS */

static void clean(void) {
 assert(!bluetooth_service_is_initialized() && !bluetooth_service_is_running());
 assert(!host_loaded && !host_live && controller_state==0 && task_count==0 && sem_count==0);
}
static void ready(void) {
 assert(bluetooth_service_init()==0); assert(bluetooth_service_start()==0);
 assert(bluetooth_service_is_initialized() && bluetooth_service_is_running());
 assert(task_count==1 && sem_count==4 && controller_state==2);
}
int main(void) {
 clean(); assert(bluetooth_service_deinit()==0); clean();
 controller_state=2; host_loaded=true;
 assert(bluetooth_service_init()==ESP_ERR_INVALID_STATE && host_loaded && controller_state==2);
 controller_state=0; host_loaded=false; clean();
 nvs_error=ESP_FAIL; assert(bluetooth_service_init()==ESP_FAIL); clean(); nvs_error=0;
 for(int i=1;i<=4;++i) {
   sem_calls=0; fail_sem=i; assert(bluetooth_service_init()==ESP_ERR_NO_MEM); clean();
 }
 fail_sem=0; init_error=ESP_FAIL; assert(bluetooth_service_init()==ESP_FAIL); clean(); init_error=0;
 name_error=1; assert(bluetooth_service_init()==ESP_FAIL); clean(); name_error=0;
 fail_host_task=true; assert(bluetooth_service_start()==ESP_ERR_NO_MEM); clean(); fail_host_task=false;
 no_sync=true; assert(bluetooth_service_start()==ESP_ERR_TIMEOUT); clean(); no_sync=false;
 infer_error=1; assert(bluetooth_service_start()==ESP_FAIL); clean(); infer_error=0;
 for(int i=0;i<5;++i) {
   ready(); int initialized=host_inits;
   assert(bluetooth_service_init()==0 && bluetooth_service_start()==0 && host_inits==initialized);
   assert(bluetooth_service_deinit()==0); clean();
 }
 ready(); on_reset(17); assert(!bluetooth_service_is_running());
 assert(bluetooth_service_init()==0); assert(bluetooth_service_start()==0);
 assert(bluetooth_service_stop()==0 && !bluetooth_service_is_running());
 assert(bluetooth_service_start()==0); assert(bluetooth_service_deinit()==0); clean();
 ready(); explorer_busy=true; assert(bluetooth_service_deinit()==ESP_ERR_INVALID_STATE);
 assert(bluetooth_service_is_running()); explorer_busy=false;
 assert(bluetooth_service_deinit()==0); clean();
 ready(); fail_stop_task=true; assert(bluetooth_service_deinit()==ESP_ERR_NO_MEM);
 assert(host_live && sem_count==4); fail_stop_task=false;
 assert(bluetooth_service_deinit()==0); clean();
 ready(); stop_error=1; assert(bluetooth_service_deinit()==ESP_FAIL);
 assert(host_live && sem_count==4); stop_error=0; assert(bluetooth_service_deinit()==0); clean();
 ready(); hold_stop=true; assert(bluetooth_service_deinit()==ESP_ERR_TIMEOUT);
 assert(task_count==2 && host_loaded && sem_count==4);
 hold_stop=false; assert(bluetooth_service_deinit()==0); clean();
 ready(); hold_exit=true; assert(bluetooth_service_deinit()==ESP_ERR_TIMEOUT);
 assert(task_count==1 && host_loaded && sem_count==4);
 hold_exit=false; assert(bluetooth_service_deinit()==0); clean();
 ready(); host_deinit_error=ESP_FAIL; assert(bluetooth_service_deinit()==ESP_FAIL);
 assert(host_loaded && !host_live && sem_count==4); host_deinit_error=0;
 assert(bluetooth_service_deinit()==0); clean();
 ready(); disable_error=ESP_FAIL; assert(bluetooth_service_deinit()==ESP_FAIL);
 int deinits=host_deinits; assert(!host_loaded && controller_state==2);
 disable_error=0; assert(bluetooth_service_init()==0); assert(host_deinits==deinits);
 assert(bluetooth_service_deinit()==0); clean();
 ready(); controller_error=ESP_FAIL; assert(bluetooth_service_deinit()==ESP_FAIL);
 deinits=host_deinits; assert(!host_loaded && controller_state==1);
 controller_error=0; assert(bluetooth_service_deinit()==0 && host_deinits==deinits); clean();
 atomic_flag_test_and_set(&s_lifecycle_busy);
 assert(bluetooth_service_init()==ESP_ERR_INVALID_STATE && bluetooth_service_deinit()==ESP_ERR_INVALID_STATE);
 atomic_flag_clear(&s_lifecycle_busy);
 ready(); bluetooth_service_scan(100); assert(bluetooth_service_is_running());
 scan_no_event=true; bluetooth_service_scan(100); assert(!bluetooth_service_is_running());
 scan_no_event=false; assert(bluetooth_service_start()==0);
 assert(bluetooth_service_deinit()==0); clean();
 ready(); discovery_error=1;
 assert(bluetooth_service_start_sniffer(NULL)==ESP_FAIL && !bluetooth_service_is_running());
 discovery_error=0; assert(bluetooth_service_start()==0);
 uint8_t address[6]={0}; discovery_error=1;
 assert(bluetooth_service_start_tracker(address,NULL)==ESP_FAIL && !bluetooth_service_is_running());
 discovery_error=0; assert(bluetooth_service_start()==0);
 assert(bluetooth_service_deinit()==0); clean();
 puts("PASS: BLE allocation/start/reset/stop failures, bounded retained cleanup, phased retry, scan timeout and repeated lifecycle");
}
