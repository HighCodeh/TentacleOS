#pragma once

#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *base_path;
    const char *partition_label;
    int partition;
    bool format_if_mount_failed;
    bool dont_mount;
    int dummy;
} esp_vfs_littlefs_conf_t;

esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *conf);
esp_err_t esp_vfs_littlefs_unregister(const char *label);
esp_err_t esp_littlefs_info(const char *label, size_t *total, size_t *used);

#ifdef __cplusplus
}
#endif
