#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "tos_api.h"
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
#define TOS_APP_MAX 1
#define APP_KILL_QUIESCE_MS 3000
#define APP_CLEANUP_RETRY_MS 20
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
typedef void *TaskHandle_t;
typedef enum {eRunning,eSuspended} eTaskState;
typedef struct {bool active,stop,cleaning;uint32_t caps;TaskHandle_t task;char name[24];} app_slot_t;
static const char *TAG="TEST";
static TaskHandle_t s_caller=(void *)1;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return s_caller;}
static void *s_mtx=(void *)1;
static app_slot_t s_apps[1];
static bool s_ui,s_rf,s_spi,s_allocated,s_context,s_grant,s_suspend_requested,s_is_suspended;
static bool s_fail_ui,s_fail_rf,s_fail_spi,s_fail_suspend;
static unsigned s_finishes,s_deletes,s_resumes,s_queries;
static int xSemaphoreTake(void *mutex,uint32_t timeout) {(void)mutex;(void)timeout;return 1;}
static void xSemaphoreGive(void *mutex) {(void)mutex;}
static void vTaskDelay(uint32_t delay) {assert(delay==20);}
static bool claim_cleanup(app_slot_t *app) {assert(!app->cleaning);app->cleaning=true;return true;}
static bool lvgl_glue_lock(uint32_t timeout) {assert(timeout==3000);return s_ui=!s_fail_ui;}
static void lvgl_glue_unlock(void) {assert(s_ui);s_ui=false;}
static bool host_local_rf_lock(uint32_t timeout) {assert(timeout==3000);return s_rf=!s_fail_rf;}
static void host_local_rf_unlock(void) {assert(s_rf);s_rf=false;}
static esp_err_t spi_bridge_lock(uint32_t timeout) {assert(timeout==3000);s_spi=!s_fail_spi;return s_spi?ESP_OK:ESP_ERR_TIMEOUT;}
static void spi_bridge_unlock(void) {assert(s_spi);s_spi=false;}
static void vTaskSuspend(TaskHandle_t task) {
  assert(task==(void *)42 && s_rf && s_spi && s_ui && s_allocated);
  s_suspend_requested=true;
}
static eTaskState eTaskGetState(TaskHandle_t task) {
  assert(task==(void *)42 && s_suspend_requested && s_allocated);++s_queries;
  if(s_fail_suspend || s_queries<3)return eRunning;
  s_is_suspended=true;return eSuspended;
}
static void vTaskResume(TaskHandle_t task) {
  assert(task==(void *)42 && s_suspend_requested && s_allocated);++s_resumes;
  s_suspend_requested=false;s_is_suspended=false;
}
static void finish_slot(app_slot_t *app) {
  assert(!s_ui && !s_rf && !s_spi);
  assert(s_is_suspended && s_allocated && s_context && s_grant && s_deletes==0);
  s_context=false;s_grant=false;++s_finishes;
  app->task=NULL;app->active=false;app->cleaning=false;
}
static void vTaskDelete(TaskHandle_t task) {
  assert(task==(void *)42 && s_is_suspended && s_allocated && !s_context && !s_grant);
  assert(s_finishes==1);++s_deletes;s_allocated=false;
  // A scheduler may immediately assign this same identity to a different app.
  s_allocated=true;s_context=true;s_grant=true;
  s_apps[0]=(app_slot_t){.active=true,.task=task,.name="new-app"};
}
/* MANAGER */
static void reset(void) {
  s_apps[0]=(app_slot_t){.active=true,.caps=TOS_CAP_UI,.task=(void *)42,.name="app"};
  s_ui=s_rf=s_spi=s_suspend_requested=s_is_suspended=false;
  s_fail_ui=s_fail_rf=s_fail_spi=s_fail_suspend=false;
  s_allocated=s_context=s_grant=true;s_finishes=s_deletes=s_resumes=s_queries=0;
}
int main(void) {
  reset();assert(tos_app_mgr_stop("app",0)==ESP_OK);
  assert(s_finishes==1 && s_deletes==1 && s_queries==3);
  assert(s_context && s_grant && s_allocated && s_apps[0].active); // New owner survives.
  for(unsigned failure=0;failure<4;++failure) {
    reset();s_fail_ui=failure==0;s_fail_rf=failure==1;s_fail_spi=failure==2;s_fail_suspend=failure==3;
    assert(tos_app_mgr_stop("app",0)==ESP_ERR_TIMEOUT);
    assert(s_apps[0].active && !s_apps[0].cleaning && s_context && s_grant);
    assert(s_finishes==0 && s_deletes==0 && !s_ui && !s_rf && !s_spi);
    assert(s_resumes==(failure==3?1u:0u));
  }
  reset();s_caller=(void *)42;
  assert(tos_app_mgr_stop("app",0)==ESP_ERR_INVALID_STATE);
  assert(s_finishes==0 && s_deletes==0 && !s_suspend_requested && !s_apps[0].cleaning);
  puts("PASS: actual forced app stop, cross-core suspend acknowledgement, lock rollback and context/grant cleanup before task identity reuse");
}
