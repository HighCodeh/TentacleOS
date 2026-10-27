#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_PM_NO_LIGHT_SLEEP 2
typedef int esp_err_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) assert(pthread_mutex_lock(lock)==0)
#define portEXIT_CRITICAL(lock) assert(pthread_mutex_unlock(lock)==0)
typedef void *esp_pm_lock_handle_t;
typedef struct {int max_freq_mhz,min_freq_mhz;bool light_sleep_enable;} esp_pm_config_t;
static unsigned s_errors,s_warnings;
#define ESP_LOGE(tag, ...) ((void)(tag),++s_errors)
#define ESP_LOGW(tag, ...) ((void)(tag),++s_warnings)
#define ESP_LOGI(tag, ...) ((void)(tag))
const char *esp_err_to_name(esp_err_t);
esp_err_t esp_pm_lock_create(int,int,const char *,esp_pm_lock_handle_t *);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t);
esp_err_t esp_pm_lock_delete(esp_pm_lock_handle_t);
esp_err_t esp_pm_configure(const esp_pm_config_t *);
#endif
