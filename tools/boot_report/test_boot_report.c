#include "test_platform.h"
static void *test_allocate(size_t);
static void test_release(void *);
static size_t test_copy(char *out,const char *text,size_t size) {
  size_t n=strlen(text),copy=n<size-1?n:size-1;memcpy(out,text,copy);out[copy]=0;return n;
}
#define malloc test_allocate
#define free test_release
#define strlcpy test_copy
#include "boot_report.c"
#undef malloc
#undef free
static esp_reset_reason_t s_reason;
static bool s_has_dump,s_no_memory,s_open,s_timer_live;
static unsigned s_allocations,s_sets,s_commits,s_closes;
static int s_fail_get,s_fail_set;
static esp_err_t s_open_error,s_commit_error,s_summary_error,s_erase_error;
static esp_err_t s_create_error,s_start_error,s_delete_error;
static uint32_t s_store[3],s_pending[3];
static bool s_present[3];
static esp_timer_create_args_t s_timer_args;
static int key_index(const char *key) {
  if(strcmp(key,"boot_total")==0)return 0;
  if(strcmp(key,"panic_total")==0)return 1;
  assert(strcmp(key,"last_reason")==0);return 2;
}
const char *esp_err_to_name(esp_err_t error) {(void)error;return "fixture";}
esp_reset_reason_t esp_reset_reason(void) {return s_reason;}
esp_err_t esp_core_dump_image_check(void) {return s_has_dump?ESP_OK:ESP_FAIL;}
esp_err_t esp_core_dump_get_summary(esp_core_dump_summary_t *out) {
  assert(s_has_dump && s_allocations==1);
  if(s_summary_error!=ESP_OK)return s_summary_error;
  memset(out,0,sizeof(*out));memset(out->exc_task,'x',sizeof(out->exc_task)-1);
  out->exc_pc=42;out->ex_info.mcause=7;out->ex_info.mtval=8;out->ex_info.ra=9;out->ex_info.sp=10;
  return ESP_OK;
}
esp_err_t esp_core_dump_image_erase(void) {
  if(s_erase_error==ESP_OK)s_has_dump=false;return s_erase_error;
}
static void *test_allocate(size_t size) {
  if(s_no_memory)return NULL;++s_allocations;return malloc(size);
}
static void test_release(void *p) {if(p){assert(s_allocations>0);--s_allocations;}free(p);}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out) {
  assert(!s_open && mode==NVS_READWRITE && strcmp(name,"boot_report")==0);
  if(s_open_error!=ESP_OK)return s_open_error;
  s_open=true;*out=1;memcpy(s_pending,s_store,sizeof(s_store));return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t nvs,const char *key,uint32_t *out) {
  assert(s_open && nvs==1);int i=key_index(key);
  if(s_fail_get==i+1)return ESP_FAIL;
  if(!s_present[i])return ESP_ERR_NVS_NOT_FOUND;
  *out=s_store[i];return ESP_OK;
}
esp_err_t nvs_set_u32(nvs_handle_t nvs,const char *key,uint32_t value) {
  assert(s_open && nvs==1);++s_sets;if(s_fail_set==(int)s_sets)return ESP_FAIL;
  s_pending[key_index(key)]=value;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t nvs) {
  assert(s_open && nvs==1);++s_commits;
  if(s_commit_error!=ESP_OK)return s_commit_error;
  memcpy(s_store,s_pending,sizeof(s_store));memset(s_present,1,sizeof(s_present));return ESP_OK;
}
void nvs_close(nvs_handle_t nvs) {assert(s_open && nvs==1);s_open=false;++s_closes;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
  assert(!s_timer_live);if(s_create_error!=ESP_OK)return s_create_error;
  s_timer_live=true;s_timer_args=*args;*out=(void *)1;return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t timeout) {
  assert(s_timer_live && timer==(void *)1 && timeout==20000000);return s_start_error;
}
esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
  assert(s_timer_live && timer==(void *)1);if(s_delete_error==ESP_OK)s_timer_live=false;return s_delete_error;
}
static void reset_boot(esp_reset_reason_t reason) {
  assert(!s_open && s_allocations==0);s_reason=reason;s_crash_captured=false;
  memset(&s_crash,0,sizeof(s_crash));s_boot_total=s_panic_total=0;
  s_is_bootloop=false;s_stable_timer=NULL;s_timer_live=false;
  s_no_memory=false;s_fail_get=s_fail_set=0;
  s_open_error=s_commit_error=s_summary_error=s_erase_error=ESP_OK;
  s_create_error=s_start_error=s_delete_error=ESP_OK;
  s_sets=s_commits=s_closes=s_errors=s_warnings=0;
}
static void capture(esp_reset_reason_t reason,bool has_dump) {
  reset_boot(reason);s_has_dump=has_dump;boot_report_capture_crash();
  assert(!s_open && s_allocations==0 && s_closes==1);
}
static void *clear_stable(void *unused) {
  (void)unused;for(unsigned i=0;i<10000;++i)boot_report_mark_stable();return NULL;
}
int main(void) {
  (void)test_allocate;(void)test_release;(void)test_copy;
  s_store[0]=40;s_store[1]=7;memset(s_present,1,sizeof(s_present));
  capture(ESP_RST_SW,true);
  assert(boot_report_has_crash() && boot_report_boot_count()==41 && boot_report_panic_total()==7);
  unsigned sets=s_sets;boot_report_capture_crash();assert(s_sets==sets);
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
  assert(boot_report_crash()->pc==42 && boot_report_crash()->task[15]==0);
#endif
  capture(ESP_RST_POWERON,true);assert(boot_report_boot_count()==42 && boot_report_panic_total()==7);
  capture(ESP_RST_PANIC,true);assert(boot_report_boot_count()==43 && boot_report_panic_total()==8);
  capture(ESP_RST_TASK_WDT,false);assert(boot_report_boot_count()==44 && boot_report_panic_total()==9);
  for(int key=1;key<=2;++key) {
    reset_boot(ESP_RST_PANIC);s_fail_get=key;boot_report_capture_crash();
    assert(s_sets==0 && s_commits==0 && s_closes==1 && s_warnings>0);
    assert(s_store[0]==44 && s_store[1]==9);
  }
  reset_boot(ESP_RST_PANIC);s_open_error=ESP_FAIL;boot_report_capture_crash();
  assert(s_sets==0 && s_closes==0 && s_warnings>0);
  for(int failure=1;failure<=3;++failure) {
    reset_boot(ESP_RST_PANIC);s_fail_set=failure;boot_report_capture_crash();
    assert(s_commits==0 && s_closes==1 && boot_report_boot_count()==44 && boot_report_panic_total()==9);
  }
  reset_boot(ESP_RST_PANIC);s_commit_error=ESP_FAIL;boot_report_capture_crash();
  assert(s_commits==1 && s_closes==1 && boot_report_boot_count()==44 && boot_report_panic_total()==9);
  memset(s_present,0,sizeof(s_present));capture(ESP_RST_SW,false);
  assert(boot_report_boot_count()==1 && boot_report_panic_total()==0);
  s_store[0]=s_store[1]=UINT32_MAX;memset(s_present,1,sizeof(s_present));capture(ESP_RST_PANIC,false);
  assert(boot_report_boot_count()==UINT32_MAX && boot_report_panic_total()==UINT32_MAX);
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
  reset_boot(ESP_RST_PANIC);s_has_dump=true;s_no_memory=true;boot_report_capture_crash();
  assert(boot_report_has_crash() && s_errors==1 && s_allocations==0 && boot_report_crash()->pc==0);
  reset_boot(ESP_RST_PANIC);s_has_dump=true;s_summary_error=ESP_FAIL;boot_report_capture_crash();
  assert(boot_report_has_crash() && s_allocations==0 && boot_report_crash()->pc==0);
#endif
  s_erase_error=ESP_FAIL;assert(boot_report_clear_crash()==ESP_FAIL && boot_report_has_crash());
  s_erase_error=ESP_OK;assert(boot_report_clear_crash()==ESP_OK && !boot_report_has_crash());
  s_rtc_magic=0;s_rtc_abnormal_count=UINT32_MAX;
  reset_boot(ESP_RST_PANIC);boot_report_track_bootloop();assert(boot_report_abnormal_boots()==1 && !boot_report_in_bootloop());
  reset_boot(ESP_RST_INT_WDT);boot_report_track_bootloop();assert(boot_report_abnormal_boots()==2 && !boot_report_in_bootloop());
  reset_boot(ESP_RST_BROWNOUT);boot_report_track_bootloop();assert(boot_report_abnormal_boots()==3 && boot_report_in_bootloop());
  s_timer_args.callback(s_timer_args.arg);assert(boot_report_abnormal_boots()==0 && boot_report_in_bootloop());
  pthread_t task;assert(pthread_create(&task,NULL,clear_stable,NULL)==0);
  for(unsigned i=0;i<10000;++i){(void)boot_report_abnormal_boots();assert(boot_report_in_bootloop());}
  assert(pthread_join(task,NULL)==0);
  reset_boot(ESP_RST_PANIC);boot_report_track_bootloop();assert(boot_report_abnormal_boots()==1 && !boot_report_in_bootloop());
  s_rtc_abnormal_count=UINT32_MAX;reset_boot(ESP_RST_CPU_LOCKUP);boot_report_track_bootloop();
  assert(boot_report_abnormal_boots()==UINT32_MAX && boot_report_in_bootloop());
  reset_boot(ESP_RST_SW);boot_report_track_bootloop();assert(boot_report_abnormal_boots()==0 && !boot_report_in_bootloop());
  reset_boot(ESP_RST_PANIC);s_create_error=ESP_FAIL;boot_report_track_bootloop();assert(s_stable_timer==NULL && s_warnings>0);
  reset_boot(ESP_RST_PANIC);s_start_error=ESP_FAIL;boot_report_track_bootloop();assert(s_stable_timer==NULL && !s_timer_live);
  reset_boot(ESP_RST_PANIC);s_start_error=s_delete_error=ESP_FAIL;boot_report_track_bootloop();
  assert(s_stable_timer!=NULL && s_timer_live && boot_report_in_bootloop());
  boot_report_reset();assert(boot_report_all_required_ok());
  for(unsigned i=0;i<BOOT_REPORT_MAX_STAGES;++i)boot_report_record("optional",false,ESP_OK);
  boot_report_record("required-overflow",true,ESP_FAIL);assert(!boot_report_all_required_ok());
  int count;assert(boot_report_stages(&count)!=NULL && count==BOOT_REPORT_MAX_STAGES);
  puts("PASS: real boot report, stale dump counts, NVS/heap/timer failures, saturation, reset sequence and concurrent stable latch");
  return 0;
}
