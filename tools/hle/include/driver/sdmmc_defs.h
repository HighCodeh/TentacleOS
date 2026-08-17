#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1

typedef int sdmmc_csd_t;
typedef int sdmmc_cid_t;

esp_err_t sdmmc_card_init(const void *host, sdmmc_csd_t *csd, sdmmc_cid_t *cid, sdmmc_csd_t *scr, sdmmc_card_t *card);

#ifdef __cplusplus
}
#endif
