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

typedef enum {
    NVS_TYPE_U8 = 0x01,
    NVS_TYPE_I8 = 0x11,
    NVS_TYPE_U16 = 0x02,
    NVS_TYPE_I16 = 0x12,
    NVS_TYPE_U32 = 0x04,
    NVS_TYPE_I32 = 0x14,
    NVS_TYPE_U64 = 0x08,
    NVS_TYPE_I64 = 0x18,
    NVS_TYPE_STR = 0x21,
    NVS_TYPE_BLOB = 0x42,
    NVS_TYPE_ANY = 0xff,
} nvs_type_t;

#define NVS_NS_NAME_MAX_SIZE  16
#define NVS_KEY_NAME_MAX_SIZE 16

typedef struct {
    char namespace_name[NVS_NS_NAME_MAX_SIZE];
    char key[NVS_KEY_NAME_MAX_SIZE];
    nvs_type_t type;
} nvs_entry_info_t;

typedef struct nvs_opaque_iterator_t *nvs_iterator_t;

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

esp_err_t nvs_entry_find(const char *part_name, const char *namespace_name, nvs_type_t type,
                         nvs_iterator_t *output_iterator);
esp_err_t nvs_entry_next(nvs_iterator_t *iterator);
esp_err_t nvs_entry_info(const nvs_iterator_t iterator, nvs_entry_info_t *out_info);
void nvs_release_iterator(nvs_iterator_t iterator);


#ifdef __cplusplus
}
#endif
