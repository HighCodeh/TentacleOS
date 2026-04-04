/**
 * @file sd_card_init.h
 * @brief SD card initialization and control functions (SDMMC)
 */

#ifndef SD_CARD_INIT_H
#define SD_CARD_INIT_H

#include "esp_err.h"
#include "vfs_config.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
#define SD_MAX_FILES            10
#define SD_ALLOCATION_UNIT      (16 * 1024)

/**
 * @brief Initialize SD card with default config (SDMMC 4-bit)
 * @return ESP_OK on success
 */
esp_err_t sd_init(void);

/**
 * @brief Initialize with custom config
 * @param max_files Maximum number of open files
 * @param format_if_failed Whether to format on mount failure
 * @return ESP_OK on success
 */
esp_err_t sd_init_custom(uint8_t max_files, bool format_if_failed);

/**
 * @brief Unmount the SD card
 * @return ESP_OK on success
 */
esp_err_t sd_deinit(void);

/**
 * @brief Check if mounted
 * @return true if mounted
 */
bool sd_is_mounted(void);

/**
 * @brief Remount the card
 * @return ESP_OK on success
 */
esp_err_t sd_remount(void);

/**
 * @brief Check connection health
 * @return ESP_OK if healthy
 */
esp_err_t sd_check_health(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_INIT_H */
