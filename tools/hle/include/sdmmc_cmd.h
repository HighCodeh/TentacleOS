#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t sdmmc_slot_t;
#define SDMMC_SLOT_0 0
#define SDMMC_SLOT_1 1

typedef struct sdmmc_host_t { int flags; int slot; int max_freq_khz; bool io_voltage; } sdmmc_host_t;
typedef struct sdmmc_card_t { int dummy; } sdmmc_card_t;
typedef struct { sdmmc_host_t host; sdmmc_slot_t slot; int cd; int wp; } sdmmc_slot_config_t;

typedef struct { int host; int slot; } sdmmc_init_slot_config_t;

#define SDMMC_HOST_DEFAULT() (sdmmc_host_t){0}
#define SDMMC_SLOT_CONFIG_DEFAULT() (sdmmc_slot_config_t){0}
#define SDMMC_HOST_FLAG_4BIT    1
#define SDMMC_FREQ_DEFAULT      20000

esp_err_t sdmmc_host_init(void);
esp_err_t sdmmc_host_init_slot(int slot, const void *config);
void sdmmc_card_print_info(FILE *f, const sdmmc_card_t *card);
void sdmmc_host_deinit(void);

#ifdef __cplusplus
}
#endif
