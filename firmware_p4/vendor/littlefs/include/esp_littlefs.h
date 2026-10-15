#ifndef ESP_LITTLEFS_H__
#define ESP_LITTLEFS_H__

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include <stdbool.h>
#include "esp_partition.h"

/** LittleFS over ESP-IDF Block Device Layer (`esp_blockdev`) is only built on ESP-IDF 6+ (non-ESP8266). */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) && !defined(ESP8266)
#define ESP_LITTLEFS_HAS_BLOCKDEV 1
#include "esp_blockdev.h"
#else
#define ESP_LITTLEFS_HAS_BLOCKDEV 0
#endif

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
#include <sdmmc_cmd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LITTLEFS_VERSION_NUMBER "1.22.1"
#define ESP_LITTLEFS_VERSION_MAJOR 1
#define ESP_LITTLEFS_VERSION_MINOR 22
#define ESP_LITTLEFS_VERSION_PATCH 1

#ifdef ESP8266
// ESP8266 RTOS SDK default enables VFS DIR support
#define CONFIG_VFS_SUPPORT_DIR 1
#endif

#if CONFIG_VFS_SUPPORT_DIR
#define ESP_LITTLEFS_ENABLE_FTRUNCATE
#endif // CONFIG_VFS_SUPPORT_DIR

/**
 *Configuration structure for esp_vfs_littlefs_register.
 */
typedef struct {
    const char *base_path;            /**< Mounting point. */
    const char *partition_label;      /**< Label of partition to use. If no mount source is set (partition_label,
                                           partition, optional sdcard, optional blockdev on ESP-IDF 6+), the first data
                                           partition with subtype 'littlefs' is used. */
    const esp_partition_t* partition; /**< partition to use if partition_label is NULL */

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
    sdmmc_card_t *sdcard;       /**< SD card handle to use if both esp_partition handle & partition label is NULL */
#endif

#if ESP_LITTLEFS_HAS_BLOCKDEV
    /**
     * Block device for LittleFS when partition_label, partition, and sdcard (when enabled) are not used.
     *
     * `device_flags` on the handle are validated at mount:
     * - `encrypted` — mount fails (not supported).
     * - `default_val_after_erase` — must be 1 (LittleFS expects 0xFF after erase).
     * - `erase_before_write` and `and_type_write` are used only to select block sizing mode:
     *   - classic mode (either flag set): `lfs` `block_size` uses geometry erase size.
     *   - logical mode (both flags clear): `lfs` `block_size` uses lcm(read, prog).
     *
     * If the device provides `ops->release`, it is called when the filesystem is torn down
     * (e.g. `esp_vfs_littlefs_unregister_blockdev`).
     */
    esp_blockdev_handle_t blockdev;
#endif

    uint8_t format_if_mount_failed:1; /**< Format the file system if it fails to mount. */
    uint8_t read_only : 1;            /**< Mount the partition as read-only. */
    uint8_t dont_mount:1;             /**< Don't attempt to mount.*/
    uint8_t grow_on_mount:1;          /**< Grow filesystem to match partition size on mount.*/
} esp_vfs_littlefs_conf_t;

/**
 * Register and mount (if configured to) littlefs to VFS with given path prefix.
 *
 * @param   conf                      Pointer to esp_vfs_littlefs_conf_t configuration structure
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_NO_MEM          if objects could not be allocated
 *          - ESP_ERR_INVALID_STATE   if already mounted or partition is encrypted
 *          - ESP_ERR_NOT_FOUND       if partition for littlefs was not found
 *          - ESP_FAIL                if mount or format fails
 */
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t * conf);

/**
 * Unregister and unmount littlefs from VFS
 *
 * @param partition_label  Label of the partition to unregister.
 *
 * @return  
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_littlefs_unregister(const char* partition_label);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Unregister and unmount LittleFS from VFS for SD card
 *
 * @param sdcard  SD card to unregister.
 *
 * @return
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_littlefs_unregister_sdmmc(sdmmc_card_t *sdcard);
#endif

/**
 * Unregister and unmount littlefs from VFS
 *
 * @param partition  partition to unregister.
 *
 * @return  
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t esp_vfs_littlefs_unregister_partition(const esp_partition_t* partition);

#if ESP_LITTLEFS_HAS_BLOCKDEV
/**
 * Unregister and unmount littlefs from VFS.
 *
 * @param blockdev  blockdev to unregister.
 *
 * @return
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 *
 * @note Invokes `blockdev->ops->release` when present; the handle must not be used afterward.
 */
esp_err_t esp_vfs_littlefs_unregister_blockdev(esp_blockdev_handle_t blockdev);
#endif

/**
 * Check if littlefs is mounted
 *
 * @param partition_label  Label of the partition to check.
 *
 * @return  
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_littlefs_mounted(const char* partition_label);

/**
 * Check if littlefs is mounted
 *
 * @param partition  partition to check.
 *
 * @return  
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_littlefs_partition_mounted(const esp_partition_t* partition);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Check if littlefs is mounted
 *
 * @param sdcard  SD card to check.
 *
 * @return
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_littlefs_sdmmc_mounted(sdmmc_card_t *sdcard);
#endif

#if ESP_LITTLEFS_HAS_BLOCKDEV
/**
 * Check if littlefs is mounted
 *
 * @param blockdev  blockdev to check.
 *
 * @return  
 *          - true    if mounted
 *          - false   if not mounted
 */
bool esp_littlefs_blockdev_mounted(esp_blockdev_handle_t blockdev);
#endif

/**
 * Format the littlefs partition
 *
 * @param partition_label  Label of the partition to format.
 * @return  
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_format(const char* partition_label);

/**
 * Format the littlefs partition
 *
 * @param partition  partition to format.
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_format_partition(const esp_partition_t* partition);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Format the LittleFS on a SD card
 *
 * @param sdcard SD card to format
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_format_sdmmc(sdmmc_card_t *sdcard);
#endif

#if ESP_LITTLEFS_HAS_BLOCKDEV
/**
 * Format the littlefs blockdev
 *
 * @param blockdev  blockdev to format.
 * @note This call does not transfer ownership and does not invoke `blockdev->ops->release`.
 * @return
 *          - ESP_OK      if successful
 *          - ESP_FAIL    on error
 */
esp_err_t esp_littlefs_format_blockdev(esp_blockdev_handle_t blockdev);
#endif

/**
 * Get information for littlefs
 *
 * @param partition_label           Optional, label of the partition to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_littlefs_info(const char* partition_label, size_t* total_bytes, size_t* used_bytes);

/**
 * Get information for littlefs
 *
 * @param parition                  the partition to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_littlefs_partition_info(const esp_partition_t* partition, size_t *total_bytes, size_t *used_bytes);

#ifdef CONFIG_LITTLEFS_SDMMC_SUPPORT
/**
 * Get information for littlefs on SD card
 *
 * @param[in] sdcard                    the SD card to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_littlefs_sdmmc_info(sdmmc_card_t *sdcard, size_t *total_bytes, size_t *used_bytes);
#endif

#if ESP_LITTLEFS_HAS_BLOCKDEV
/**
 * Get information for littlefs
 *
 * @param blockdev                  the blockdev to get info for.
 * @param[out] total_bytes          Size of the file system
 * @param[out] used_bytes           Current used bytes in the file system
 *
 * @return  
 *          - ESP_OK                  if success
 *          - ESP_ERR_INVALID_STATE   if not mounted
 */
esp_err_t esp_littlefs_blockdev_info(esp_blockdev_handle_t blockdev, size_t *total_bytes, size_t *used_bytes);
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif
