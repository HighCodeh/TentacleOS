#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

esp_err_t esp_ota_get_boot_partition(void *partition);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_set_boot_partition(void *partition);

#ifdef __cplusplus
}
#endif
