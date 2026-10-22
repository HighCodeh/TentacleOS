#ifndef TEST_PLATFORM_H
#define TEST_PLATFORM_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <pthread.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NO_MEM 0x101
#define OTA_WITH_SEQUENTIAL_WRITES 0xfffffffe
#define FIRMWARE_VERSION "fixture-version"
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
typedef int esp_err_t;
typedef uintptr_t esp_ota_handle_t;
typedef struct {uint32_t size,address;char label[16];} esp_partition_t;
typedef enum {ESP_OTA_IMG_NEW,ESP_OTA_IMG_PENDING_VERIFY,ESP_OTA_IMG_VALID,
  ESP_OTA_IMG_INVALID,ESP_OTA_IMG_ABORTED} esp_ota_img_states_t;
const char *esp_err_to_name(esp_err_t);
const esp_partition_t *esp_ota_get_next_update_partition(const void *);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_begin(const esp_partition_t *,size_t,esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t,const void *,size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *,esp_ota_img_states_t *);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
void esp_restart(void);
void vTaskDelay(uint32_t);
bool sd_is_mounted(void);
bool storage_assets_is_mounted(void);
uint8_t *storage_assets_load_file(const char *,size_t *);
esp_err_t storage_assets_write_file(const char *,const char *);
uint32_t ui_render_beat(void);
esp_err_t power_manager_no_sleep_acquire(void);
esp_err_t power_manager_no_sleep_release(void);
#endif
