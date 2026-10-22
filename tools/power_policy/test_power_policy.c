#include "test_platform.h"
#include "power_manager.c"
static unsigned s_pm_holds,s_configures,s_creates;
static bool s_live,s_sleep;
static esp_err_t s_create_error,s_acquire_error,s_release_error,s_delete_error,s_configure_error;
static atomic_bool s_calling,s_config_entered,s_config_release=true;
const char *esp_err_to_name(esp_err_t error) {(void)error;return "fixture";}
esp_err_t esp_pm_lock_create(int type,int arg,const char *name,esp_pm_lock_handle_t *out) {
  assert(type==ESP_PM_NO_LIGHT_SLEEP && arg==0 && strcmp(name,"no_sleep")==0 && !s_live);
  ++s_creates;if(s_create_error!=ESP_OK)return s_create_error;
  s_live=true;*out=(void *)1;return ESP_OK;
}
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle) {
  assert(s_live && handle==(void *)1 && !atomic_exchange(&s_calling,true));
  esp_err_t error=s_acquire_error;if(error==ESP_OK)++s_pm_holds;s_calling=false;return error;
}
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle) {
  assert(s_live && handle==(void *)1 && s_pm_holds>0 && !atomic_exchange(&s_calling,true));
  esp_err_t error=s_release_error;if(error==ESP_OK)--s_pm_holds;s_calling=false;return error;
}
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t handle) {
  assert(s_live && handle==(void *)1 && s_pm_holds==0);
  if(s_delete_error==ESP_OK)s_live=false;return s_delete_error;
}
esp_err_t esp_pm_configure(const esp_pm_config_t *cfg) {
  assert(s_live && s_pm_holds==1 && cfg->light_sleep_enable);
  assert(cfg->min_freq_mhz==360 && cfg->max_freq_mhz==360);++s_configures;
  s_config_entered=true;while(!s_config_release) {}
  if(s_configure_error==ESP_OK)s_sleep=true;return s_configure_error;
}
#if CONFIG_PM_ENABLE
static void reset(void) {
  s_external_power=s_usb_suspended=s_usb_hold=s_is_initializing=false;
  s_no_sleep_lock=s_pending_lock=NULL;s_pending_held=false;s_client_holds=0;
  s_pm_holds=s_configures=s_creates=s_errors=s_warnings=0;s_live=s_sleep=false;
  s_config_entered=false;s_config_release=true;
  s_create_error=s_acquire_error=s_release_error=s_delete_error=s_configure_error=ESP_OK;
}
static void *initialize(void *unused) {(void)unused;power_manager_init();return NULL;}
static void *clients(void *unused) {
  (void)unused;for(unsigned i=0;i<2000;++i){assert(power_manager_no_sleep_acquire()==ESP_OK);assert(power_manager_no_sleep_release()==ESP_OK);}return NULL;
}
static void *usb(void *unused) {
  (void)unused;for(unsigned i=0;i<2000;++i){power_manager_set_external_power(i%2);power_manager_set_usb_suspended(i%3==0);(void)power_manager_external_power();}return NULL;
}
#endif
int main(void) {
#if CONFIG_PM_ENABLE
  reset();assert(power_manager_no_sleep_acquire()==ESP_ERR_INVALID_STATE);
  power_manager_set_external_power(true);power_manager_init();
  assert(s_sleep && s_pm_holds==1 && s_usb_hold);
  assert(power_manager_no_sleep_release()==ESP_ERR_INVALID_STATE && s_pm_holds==1);
  assert(power_manager_no_sleep_acquire()==ESP_OK && s_pm_holds==2);
  power_manager_set_usb_suspended(true);assert(s_pm_holds==1 && !s_usb_hold);
  assert(power_manager_no_sleep_release()==ESP_OK && s_pm_holds==0);
  s_acquire_error=ESP_FAIL;power_manager_set_usb_suspended(false);assert(!s_usb_hold && s_pm_holds==0);
  s_acquire_error=ESP_OK;power_manager_set_usb_suspended(false);assert(s_usb_hold && s_pm_holds==1);
  s_release_error=ESP_FAIL;power_manager_set_external_power(false);assert(s_usb_hold && s_pm_holds==1);
  s_release_error=ESP_OK;power_manager_set_external_power(false);assert(!s_usb_hold && s_pm_holds==0);
  unsigned creates=s_creates;power_manager_init();assert(s_creates==creates);
  pthread_t threads[4];
  for(unsigned i=0;i<4;++i)assert(pthread_create(&threads[i],NULL,i<2?clients:usb,NULL)==0);
  for(unsigned i=0;i<4;++i)assert(pthread_join(threads[i],NULL)==0);
  power_manager_set_external_power(false);assert(s_client_holds==0 && s_pm_holds==0);
  s_client_holds=UINT32_MAX;assert(power_manager_no_sleep_acquire()==ESP_ERR_INVALID_STATE);s_client_holds=0;
  reset();s_config_release=false;pthread_t starter;
  assert(pthread_create(&starter,NULL,initialize,NULL)==0);
  while(!s_config_entered) {}
  power_manager_init();power_manager_set_external_power(true);
  assert(s_creates==1 && s_configures==1 && power_manager_no_sleep_acquire()==ESP_ERR_INVALID_STATE);
  s_config_release=true;assert(pthread_join(starter,NULL)==0);
  assert(s_usb_hold && s_pm_holds==1);
  s_acquire_error=ESP_FAIL;assert(power_manager_no_sleep_acquire()==ESP_FAIL && s_client_holds==0 && s_pm_holds==1);
  s_acquire_error=ESP_OK;assert(power_manager_no_sleep_acquire()==ESP_OK);
  s_release_error=ESP_FAIL;assert(power_manager_no_sleep_release()==ESP_FAIL && s_client_holds==1 && s_pm_holds==2);
  s_release_error=ESP_OK;assert(power_manager_no_sleep_release()==ESP_OK && s_client_holds==0);
  reset();s_create_error=ESP_FAIL;power_manager_init();assert(s_configures==0 && !s_live && !s_sleep);
  reset();s_acquire_error=ESP_FAIL;power_manager_init();assert(s_configures==0 && !s_live);
  reset();s_configure_error=ESP_FAIL;power_manager_init();assert(!s_sleep && !s_live && s_no_sleep_lock==NULL);
  reset();s_configure_error=s_release_error=ESP_FAIL;power_manager_init();
  assert(s_pending_lock!=NULL && s_pending_held && s_pm_holds==1 && s_no_sleep_lock==NULL);
  s_configure_error=s_release_error=ESP_OK;power_manager_init();assert(s_sleep && s_pending_lock==NULL && s_pm_holds==0);
  reset();s_configure_error=s_delete_error=ESP_FAIL;power_manager_init();
  assert(s_pending_lock!=NULL && !s_pending_held && s_pm_holds==0);
  s_configure_error=s_delete_error=ESP_OK;power_manager_init();assert(s_sleep && s_pending_lock==NULL);
  reset();s_release_error=ESP_FAIL;power_manager_init();assert(s_sleep && s_usb_hold && s_pm_holds==1);
  s_release_error=ESP_OK;power_manager_set_usb_suspended(true);assert(!s_usb_hold && s_pm_holds==0);
#else
  power_manager_init();assert(s_configures==0 && !s_live);
  power_manager_set_external_power(true);assert(power_manager_external_power());
  power_manager_set_usb_suspended(true);power_manager_set_external_power(false);
  assert(!power_manager_external_power() && s_pm_holds==0);
  assert(power_manager_no_sleep_acquire()==ESP_OK && power_manager_no_sleep_release()==ESP_OK);
#endif
  puts("PASS: power manager, guarded initialization, truthful USB holds/retries, balanced clients, concurrent inputs and PM-disabled policy");
  return 0;
}
