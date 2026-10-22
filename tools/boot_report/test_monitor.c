#include <assert.h>
#include <stdbool.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int esp_err_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
typedef struct { TaskHandle_t xHandle; UBaseType_t xTaskNumber; const char *pcTaskName; unsigned usStackHighWaterMark; } TaskStatus_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define pdPASS 1
#define configMAX_TASK_NAME_LEN 16
#define pdMS_TO_TICKS(value) (value)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
#define SYS_PRIO_MONITOR 1
#define SYS_CORE_RADIO 0
#define SYS_CORE_MAIN 0
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
#define ESP_LOGW(...) fixture_log(__VA_ARGS__)
#define ESP_LOGI(...) fixture_log(__VA_ARGS__)
#define ESP_ERROR_CHECK(expr) do { if ((expr) != ESP_OK) fixture_abort(); } while (0)
static void fixture_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
static jmp_buf s_jump;
static unsigned s_aborts, s_creates, s_allocations, s_beats, s_healthy, s_cycles;
static bool s_fail_create, s_fail_alloc, s_fail_add, s_fail_feed;
static size_t s_heap=100000, s_largest=50000;
static void fixture_abort(void) { ++s_aborts; longjmp(s_jump,1); }
static void *pvPortMalloc(size_t size) { if(s_fail_alloc)return NULL; ++s_allocations; return malloc(size); }
static void vPortFree(void *ptr) { assert(s_allocations); --s_allocations; free(ptr); }
static int xTaskCreatePinnedToCore(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,unsigned core) {
  (void)task;(void)arg;(void)handle;assert(strcmp(name,"SysMonitor")==0 && stack==4096 && priority==1 && core==0);
  ++s_creates;return s_fail_create?0:pdPASS;
}
static int esp_task_wdt_add(void *task) { assert(task==NULL);return s_fail_add?ESP_FAIL:ESP_OK; }
static int esp_task_wdt_reset(void) { return s_fail_feed?ESP_FAIL:ESP_OK; }
static void vTaskDelay(unsigned delay) { if(delay==2000 && ++s_cycles==1)longjmp(s_jump,2); }
static size_t heap_caps_get_free_size(unsigned caps) { (void)caps;return s_heap; }
static size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps;return s_largest; }
static size_t heap_caps_get_minimum_free_size(unsigned caps) { (void)caps;return s_heap; }
static unsigned esp_get_minimum_free_heap_size(void) { return (unsigned)s_heap; }
static unsigned esp_get_free_heap_size(void) { return (unsigned)s_heap; }
static unsigned uxTaskGetNumberOfTasks(void) { return 1; }
static unsigned uxTaskGetSystemState(TaskStatus_t *tasks,unsigned count,void *time) {
  (void)time;assert(count==1);*tasks=(TaskStatus_t){(void *)1,1,"healthy",512};return 1;
}
static void safeguard_alert(const char *title,const char *message) { assert(title && message); }
static void assets_manager_evict_cache(void) {}
static unsigned ui_render_beat(void) { return s_beats; }
static void boot_report_mark_healthy(void) { ++s_healthy; }
static bool storage_is_mounted(void) { return false; }
static int storage_check_health(void) { return ESP_OK; }
static void header_ui_request_sd_remount(void) {}
static unsigned i2c_recover_count(void) { return 0; }
#define abort fixture_abort
/* MODULE */
#undef abort
static void expect_task_abort(void) {
  unsigned previous=s_aborts;
  if(setjmp(s_jump)==0)monitor_task(NULL);
  assert(s_aborts==previous+1 && s_allocations==0);
}
int main(void) {
  (void)heap_caps_get_minimum_free_size;(void)esp_get_minimum_free_heap_size;
  (void)assets_manager_evict_cache;(void)ui_render_beat;(void)boot_report_mark_healthy;
  (void)storage_is_mounted;(void)storage_check_health;(void)header_ui_request_sd_remount;(void)i2c_recover_count;
  s_fail_create=true;assert(sys_monitor_start(false)==ESP_ERR_NO_MEM && s_creates==1 && s_allocations==0);
  s_fail_create=false;assert(sys_monitor_start(true)==ESP_OK && s_creates==2);
  assert(sys_monitor_start(false)==ESP_OK && s_creates==2);
  s_fail_add=true;expect_task_abort();s_fail_add=false;
  s_fail_feed=true;expect_task_abort();s_fail_feed=false;
  TaskStatus_t task={(void *)2,2,"same-name",1};
  for(unsigned i=0;i<STACK_ESCALATE_CYCLES-1;++i)assert(!check_task_stacks(&task,1));
  ++task.xTaskNumber;assert(!check_task_stacks(&task,1));
  assert(s_watch_count==1 && s_watch[0].streak==1);
  unsigned before=s_aborts;
  if(setjmp(s_jump)==0)for(unsigned i=0;i<STACK_ESCALATE_CYCLES;++i)check_task_stacks(&task,1);
  assert(s_aborts==before+1);
  assert(check_task_stacks(NULL,0) && s_watch_count==0);
  s_heap=1000;before=s_aborts;
  if(setjmp(s_jump)==0)for(unsigned i=0;i<HEAP_CRIT_CYCLES;++i)check_heap();
  assert(s_aborts==before+1);s_heap=100000;assert(check_heap());
#ifdef TEST_P4
  before=s_aborts;
  if(setjmp(s_jump)==0)for(unsigned i=0;i<UI_STALL_ESCALATE_CYCLES;++i)check_ui_liveness();
  assert(s_aborts==before+1);
  sys_monitor_expect_ui(false);for(unsigned i=0;i<20;++i)assert(check_ui_liveness());
  sys_monitor_expect_ui(true);++s_beats;assert(check_ui_liveness());
  assert(!check_ui_liveness());++s_beats;assert(check_ui_liveness());
  ++s_beats;
#endif
  if(setjmp(s_jump)==0)monitor_task(NULL);
  assert(s_allocations==0 && s_cycles==1);
#ifdef TEST_P4
  assert(s_healthy==1);
  s_fail_alloc=true;++s_beats;s_cycles=0;
  if(setjmp(s_jump)==0)monitor_task(NULL);
  assert(s_healthy==1 && s_allocations==0);
#endif
  puts("PASS: real monitor startup, duplicate start, WDT failures, task identity, heap escalation and first-render stall");
}
