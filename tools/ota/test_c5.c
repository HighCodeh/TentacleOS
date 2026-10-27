#include "test_platform.h"
#define OTA_VALIDATE_TIMEOUT_MS 15000
#define OTA_VALIDATE_POLL_MS 100
static const char *TAG="C5_OTA_TEST";
static bool s_has_running=true,s_bridge=true;
static esp_ota_img_states_t s_ota_state=ESP_OTA_IMG_PENDING_VERIFY;
static esp_err_t s_state_error,s_confirm_error,s_rollback_error;
static unsigned s_confirms,s_rollbacks;
static int64_t s_time;
static const esp_partition_t PARTITION={.size=0x200000,.label="ota_0"};
const esp_partition_t *esp_ota_get_running_partition(void) {return s_has_running?&PARTITION:NULL;}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *out) {assert(p==&PARTITION);*out=s_ota_state;return s_state_error;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {++s_confirms;return s_confirm_error;}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) {++s_rollbacks;return s_rollback_error;}
const char *esp_err_to_name(esp_err_t error) {(void)error;return "fixture";}
static int64_t esp_timer_get_time(void) {return s_time;}
static unsigned spi_bridge_commands_processed(void) {return s_bridge?1:0;}
void vTaskDelay(uint32_t ticks) {assert(ticks==100);s_time+=(int64_t)ticks*1000;}
/* CONFIRMATION */
int main(void) {
  s_has_running=false;assert(ota_post_boot_check()==ESP_FAIL);s_has_running=true;
  s_state_error=ESP_FAIL;assert(ota_post_boot_check()==ESP_FAIL && s_confirms==0);
  s_state_error=ESP_ERR_NOT_FOUND;assert(ota_post_boot_check()==ESP_OK && s_confirms==0);
  s_state_error=ESP_OK;s_confirm_error=ESP_FAIL;
  assert(ota_post_boot_check()==ESP_FAIL && s_confirms==1);
  s_confirm_error=ESP_OK;assert(ota_post_boot_check()==ESP_OK && s_confirms==2);
  s_bridge=false;s_rollback_error=ESP_ERR_INVALID_STATE;
  assert(ota_post_boot_check()==ESP_ERR_INVALID_STATE && s_rollbacks==1 && s_time==15000000);
  s_time=0;s_rollback_error=ESP_OK;assert(ota_post_boot_check()==ESP_FAIL && s_rollbacks==2);
  puts("PASS: C5 OTA state/confirmation/rollback errors and actual bridge-health deadline");
  return 0;
}
