#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_vfs_fat_sdmmc_mount(const char *base_path, const sdmmc_host_t *host, const void *slot,
                                   const void *mount, sdmmc_card_t **out_card);
esp_err_t esp_vfs_fat_sdmmc_unmount(void);
esp_err_t esp_vfs_fat_sdcard_unmount(const char *base_path, sdmmc_card_t *card);
esp_err_t esp_vfs_fat_spiflash_mount(const char *base, const char *part_label, const void *mount, void *wl_handle);
esp_err_t esp_vfs_fat_spiflash_unmount(const char *base, void *wl_handle);

#ifdef __cplusplus
}
#endif
