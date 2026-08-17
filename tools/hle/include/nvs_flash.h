#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
void nvs_flash_init_partition(const char *name);
void nvs_flash_init_partition_ptr(const char *name);

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_erase_all(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);

esp_err_t nvs_get_i8(nvs_handle_t h, const char *key, int8_t *val);
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *val);
esp_err_t nvs_get_i16(nvs_handle_t h, const char *key, int16_t *val);
esp_err_t nvs_get_u16(nvs_handle_t h, const char *key, uint16_t *val);
esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *val);
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *val);
esp_err_t nvs_get_i64(nvs_handle_t h, const char *key, int64_t *val);
esp_err_t nvs_get_u64(nvs_handle_t h, const char *key, uint64_t *val);
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len);

esp_err_t nvs_set_i8(nvs_handle_t h, const char *key, int8_t v);
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t v);
esp_err_t nvs_set_i16(nvs_handle_t h, const char *key, int16_t v);
esp_err_t nvs_set_u16(nvs_handle_t h, const char *key, uint16_t v);
esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t v);
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t v);
esp_err_t nvs_set_i64(nvs_handle_t h, const char *key, int64_t v);
esp_err_t nvs_set_u64(nvs_handle_t h, const char *key, uint64_t v);
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *v);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *v, size_t len);


#ifdef __cplusplus
}
#endif
