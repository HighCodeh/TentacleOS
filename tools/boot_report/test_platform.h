#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#define RTC_NOINIT_ATTR
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define NVS_READWRITE 1
typedef int esp_err_t;
typedef enum { ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_SW,ESP_RST_PANIC,
  ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,
  ESP_RST_SDIO,ESP_RST_USB,ESP_RST_JTAG,ESP_RST_EFUSE,ESP_RST_PWR_GLITCH,ESP_RST_CPU_LOCKUP
} esp_reset_reason_t;
typedef uintptr_t nvs_handle_t;
typedef void *esp_timer_handle_t;
typedef struct {void (*callback)(void *);void *arg;const char *name;} esp_timer_create_args_t;
typedef struct {char exc_task[32];uint32_t exc_pc;struct {uint32_t mcause,mtval,ra,sp;} ex_info;} esp_core_dump_summary_t;
static unsigned s_errors,s_warnings;
#define ESP_LOGE(tag, ...) ((void)(tag),++s_errors)
#define ESP_LOGW(tag, ...) ((void)(tag),++s_warnings)
#define ESP_LOGI(tag, ...) ((void)(tag))
const char *esp_err_to_name(esp_err_t);
esp_reset_reason_t esp_reset_reason(void);
esp_err_t esp_core_dump_image_check(void);
esp_err_t esp_core_dump_get_summary(esp_core_dump_summary_t *);
esp_err_t esp_core_dump_image_erase(void);
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_u32(nvs_handle_t,const char *,uint32_t *);
esp_err_t nvs_set_u32(nvs_handle_t,const char *,uint32_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
esp_err_t esp_timer_create(const esp_timer_create_args_t *,esp_timer_handle_t *);
esp_err_t esp_timer_start_once(esp_timer_handle_t,uint64_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
#endif
