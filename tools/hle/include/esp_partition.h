#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t address;
    size_t size;
    char label[16];
} esp_partition_t;

typedef int esp_partition_subtype_t;
typedef int esp_partition_type_t;

esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
const esp_partition_t *esp_ota_get_running_partition(void);
void esp_partition_get_sha256(const esp_partition_t *p, uint8_t *sha);

#define ESP_PARTITION_SUBTYPE_ANY (-1)
#define ESP_PARTITION_TYPE_APP 0

#ifdef __cplusplus
}
#endif
