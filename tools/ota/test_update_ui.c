#include "test_ui_platform.h"
#include "system_update_ui.c"
theme_t current_theme;
static lv_obj_t s_objects[128],*s_active;
static unsigned s_objects_used,s_jobs,s_starts,s_checks,s_exits,s_timer_deletes;
static bool s_is_ui=true,s_has_file,s_fail_task,s_fail_timer;
static esp_err_t s_update_error;
static void (*s_worker_fn)(void *);
static void *s_worker_arg;
static lv_timer_t s_fake_timer;
static void ui_only(void) {assert(s_is_ui);}
lv_obj_t *lv_obj_create(lv_obj_t *parent) {(void)parent;ui_only();assert(s_objects_used<128);return &s_objects[s_objects_used++];}
lv_obj_t *lv_label_create(lv_obj_t *p) {return lv_obj_create(p);}
lv_obj_t *lv_bar_create(lv_obj_t *p) {return lv_obj_create(p);}
void lv_obj_remove_flag(lv_obj_t *p,int x) {(void)p;(void)x;ui_only();}
void lv_obj_set_style_bg_color(lv_obj_t *p,int x,int y) {(void)p;(void)x;(void)y;ui_only();}
void lv_obj_set_style_bg_opa(lv_obj_t *p,int x,int y) {(void)p;(void)x;(void)y;ui_only();}
void lv_obj_set_width(lv_obj_t *p,int x) {(void)p;(void)x;ui_only();}
void lv_obj_set_size(lv_obj_t *p,int x,int y) {(void)p;(void)x;(void)y;ui_only();}
void lv_obj_align(lv_obj_t *p,int x,int y,int z) {(void)p;(void)x;(void)y;(void)z;ui_only();}
void lv_label_set_text(lv_obj_t *p,const char *text) {ui_only();snprintf(p->text,sizeof(p->text),"%s",text);}
void lv_label_set_text_fmt(lv_obj_t *p,const char *fmt,...) {ui_only();va_list args;va_start(args,fmt);vsnprintf(p->text,sizeof(p->text),fmt,args);va_end(args);}
void lv_bar_set_value(lv_obj_t *p,int value,int anim) {(void)anim;ui_only();p->value=value;}
void lv_bar_set_range(lv_obj_t *p,int lo,int hi) {(void)p;ui_only();assert(lo==0 && hi==100);}
void lv_obj_add_event_cb(lv_obj_t *p,void (*cb)(lv_event_t *),int kind,void *arg) {(void)p;(void)cb;(void)arg;assert(kind==LV_EVENT_DELETE);}
lv_obj_t *lv_event_get_target(lv_event_t *event) {return event->target;}
lv_obj_t *lv_screen_active(void) {return s_active;}
lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *),int ms,void *arg) {(void)arg;assert(cb==refresh && ms==200);return s_fail_timer?NULL:&s_fake_timer;}
void lv_timer_delete(lv_timer_t *timer) {assert(timer==&s_fake_timer);++s_timer_deletes;}
void ui_chrome_header(lv_obj_t *p,const char *title,const char *icon) {(void)p;(void)icon;ui_only();assert(strcmp(title,"P4 UPDATE")==0);}
void ui_chrome_footer(lv_obj_t *p,const char *text) {(void)p;(void)text;ui_only();}
void ui_screen_load(lv_obj_t *screen) {s_active=screen;}
void ui_input_set_screen_handler(void (*cb)(const input_event_t *,void *),void *arg) {(void)arg;assert(cb==update_input);}
void ui_switch_screen(int screen) {assert(screen==SCREEN_DEV_MENU);++s_exits;}
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,uint32_t stack,void *arg,int priority,void *out,int core) {
  (void)out;assert(strcmp(name,"p4_update")==0 && stack==6144 && priority==SYS_PRIO_SERVICE_LO && core==SYS_CORE_RADIO);
  if(s_fail_task)return 0;assert(!s_worker_fn);s_worker_fn=fn;s_worker_arg=arg;++s_jobs;return pdPASS;
}
void vTaskDelete(void *task) {assert(!s_is_ui && task==NULL);}
const char *esp_err_to_name(esp_err_t error) {(void)error;return "actual error";}
const char *ota_get_current_version(void) {return "native-version";}
bool ota_update_available(void) {assert(!s_is_ui);++s_checks;return s_has_file;}
esp_err_t ota_start_update(ota_progress_cb_t cb) {
  assert(!s_is_ui);++s_starts;cb(42,"native progress");return s_update_error;
}
static void run_job(void) {assert(s_worker_fn);void (*fn)(void *)=s_worker_fn;void *arg=s_worker_arg;s_worker_fn=NULL;s_is_ui=false;fn(arg);s_is_ui=true;}
static void press(int button) {input_event_t event={.action=INPUT_ACTION_PRESS,.button=button};update_input(&event,NULL);}
static void destroy(void) {lv_event_t event={.target=s_screen};screen_deleted(&event);s_active=NULL;}
int main(void) {
  ui_system_update_open();assert(s_jobs==1 && s_starts==0 && s_checks==0);
  run_job();refresh(s_timer);assert(s_snapshot.phase==SYSTEM_UPDATE_MISSING && s_starts==0);
  assert(strstr(s_objects[1].text,"native-version")!=NULL);
  s_has_file=true;press(INPUT_BTN_OK);run_job();refresh(s_timer);
  assert(s_snapshot.phase==SYSTEM_UPDATE_READY && s_checks==2 && s_starts==0);
  s_update_error=ESP_FAIL;press(INPUT_BTN_OK);unsigned jobs=s_jobs;
  press(INPUT_BTN_OK);press(INPUT_BTN_BACK);assert(s_jobs==jobs && s_exits==0);
  run_job();refresh(s_timer);assert(s_starts==1 && s_snapshot.phase==SYSTEM_UPDATE_FAILED);
  assert(strstr(s_status->text,"actual error")!=NULL);
  press(INPUT_BTN_BACK);assert(s_exits==1);destroy();assert(s_screen==NULL && s_timer==NULL);
  ui_system_update_open();destroy();run_job();assert(!s_is_worker_busy && s_starts==1);
  ui_system_update_open();lv_obj_t *old=s_screen;ui_system_update_open();
  lv_event_t stale={.target=old};screen_deleted(&stale);assert(s_screen!=NULL && s_screen!=old);
  run_job();destroy();
  s_fail_task=true;ui_system_update_open();assert(s_snapshot.phase==SYSTEM_UPDATE_FAILED && !s_is_worker_busy);destroy();s_fail_task=false;
  s_fail_timer=true;ui_system_update_open();jobs=s_jobs;press(INPUT_BTN_OK);assert(s_jobs==jobs && !s_is_worker_busy);destroy();s_fail_timer=false;
  ui_system_update_open();run_job();press(INPUT_BTN_OK);s_update_error=ESP_OK;run_job();refresh(s_timer);
  assert(s_starts==2 && s_snapshot.phase==SYSTEM_UPDATE_REBOOTING && s_bar->value==100);
  press(INPUT_BTN_BACK);assert(s_exits==1);destroy();
  puts("PASS: actual P4 update UI, native SD check/install/progress/error, no timer simulation, task/timer failures and deletion-safe workers");
  return 0;
}
