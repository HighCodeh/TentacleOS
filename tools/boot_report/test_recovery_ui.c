#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
typedef int esp_err_t;
typedef int lv_color_t;
typedef enum {SM_WORKING,SM_ERROR} sm_state_t;
typedef enum {ACTION_RESET_CONFIG,ACTION_RESET_ALL} sm_action_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define pdPASS 1
#define SYS_PRIO_SERVICE_HI 5
#define SYS_CORE_RADIO 0
#define LV_PALETTE_RED 1
#define pdMS_TO_TICKS(value) (value)
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
static void fixture_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
static const char *TAG="TEST";
static sm_state_t s_state;
static atomic_bool s_has_reset_error;
static jmp_buf s_jump;
static unsigned s_reboots,s_deletes,s_messages,s_worker_creates;
static bool s_can_lock=true,s_can_create=true;
static esp_err_t s_reset_result;
static esp_err_t tos_factory_reset_all(void) {return s_reset_result;}
static esp_err_t tos_factory_reset_config(void) {return s_reset_result;}
static bool ui_acquire(void) {return s_can_lock;}
static void ui_release(void) {}
static void vTaskDelete(void *task) {assert(task==NULL);++s_deletes;longjmp(s_jump,1);}
static void vTaskDelay(unsigned time) {assert(time==1200);}
static void esp_restart(void) {++s_reboots;longjmp(s_jump,2);}
static int lv_palette_main(int palette) {return palette;}
static const char *esp_err_to_name(esp_err_t error) {(void)error;return "fixture";}
static void build_message(const char *title,const char *body,const char *hint,lv_color_t accent) {(void)title;(void)body;(void)hint;(void)accent;++s_messages;}
static int ui_theme_get_accent(void) {return 0;}
static int xTaskCreatePinnedToCore(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,unsigned core) {
  (void)task;(void)name;(void)arg;assert(stack==8192 && priority==5 && handle==NULL && core==0);++s_worker_creates;return s_can_create?pdPASS:0;
}
#define FACTORY_TASK_STACK 8192
#define REBOOT_DELAY_MS 1200
/* MODULE */
int main(void) {
  s_can_create=false;start_reset(ACTION_RESET_CONFIG);assert(s_state==SM_ERROR && s_worker_creates==1 && s_messages==2);
  s_can_create=true;start_reset(ACTION_RESET_ALL);assert(s_state==SM_WORKING && s_worker_creates==2);
  s_reset_result=ESP_FAIL;
  if(setjmp(s_jump)==0)factory_task((void *)(intptr_t)ACTION_RESET_ALL);
  assert(s_state==SM_ERROR && s_reboots==0 && s_deletes==1 && !s_has_reset_error);
  s_can_lock=false;s_state=SM_WORKING;
  if(setjmp(s_jump)==0)factory_task(NULL);
  assert(s_reboots==0 && s_deletes==2 && s_has_reset_error);
  s_can_lock=true;s_reset_result=ESP_OK;
  if(setjmp(s_jump)==0)factory_task(NULL);
  assert(s_reboots==1);
  puts("PASS: real recovery reset worker checks creation/errors, retries UI error publication and reboots only on success");
}
