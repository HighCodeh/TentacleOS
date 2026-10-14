#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t mcause;
    uint32_t mtval;
    uint32_t ra;
    uint32_t sp;
} esp_core_dump_summary_extra_info_t;

typedef struct {
    char exc_task[16];
    uint32_t exc_pc;
    uint32_t core_dump_version;
    esp_core_dump_summary_extra_info_t ex_info;
} esp_core_dump_summary_t;

// A host run never leaves a core dump behind.
static inline esp_err_t esp_core_dump_image_check(void) { return ESP_ERR_NOT_FOUND; }
static inline esp_err_t esp_core_dump_get_summary(esp_core_dump_summary_t *summary) {
    (void)summary;
    return ESP_ERR_NOT_FOUND;
}
static inline esp_err_t esp_core_dump_image_erase(void) { return ESP_OK; }
