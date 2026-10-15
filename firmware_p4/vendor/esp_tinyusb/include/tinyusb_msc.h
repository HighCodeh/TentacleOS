/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "wear_levelling.h"
#include "esp_vfs_fat.h"
#if (SOC_SDMMC_HOST_SUPPORTED)
#include "driver/sdmmc_host.h"
#endif // SOC_SDMMC_HOST_SUPPORTED

/**
 * @brief Opaque handle for a TinyUSB MSC storage instance.
 */
typedef struct tinyusb_msc_storage_s *tinyusb_msc_storage_handle_t;

/**
 * @brief TinyUSB MSC storage mount point.
 */
typedef enum {
    TINYUSB_MSC_STORAGE_MOUNT_USB = 0,          /*!< Storage is exposed to the USB host. */
    TINYUSB_MSC_STORAGE_MOUNT_APP,              /*!< Storage is mounted for local application use. */
} tinyusb_msc_mount_point_t;

/**
 * @brief TinyUSB MSC event identifier.
 */
typedef enum {
    TINYUSB_MSC_EVENT_MOUNT_START,            /*!< Mount or unmount operation is starting. */
    TINYUSB_MSC_EVENT_MOUNT_COMPLETE,         /*!< Mount or unmount operation completed successfully. */
    TINYUSB_MSC_EVENT_MOUNT_FAILED,           /*!< Mount or unmount operation failed. */
    TINYUSB_MSC_EVENT_FORMAT_REQUIRED,        /*!< Formatting is required before the filesystem can be mounted. */
    TINYUSB_MSC_EVENT_FORMAT_FAILED,          /*!< Formatting failed. */
} tinyusb_msc_event_id_t;

/**
 * @brief TinyUSB MSC event data passed to tusb_msc_callback_t.
 */
typedef struct {
    tinyusb_msc_event_id_t id;                   /*!< Event identifier. */
    tinyusb_msc_mount_point_t mount_point;       /*!< Mount point associated with the event. */
    union {
        struct {

        } event_data;                                /*!< Reserved for future event-specific data. */
        // Deprecated in v2.0.0, could be removed in future releases
        struct {
            bool is_mounted;                        /*!< `true` when the storage is mounted. */
        } mount_changed_data __attribute__((deprecated)); /*!< Deprecated compatibility field. */
    };
} tinyusb_msc_event_t;

/**
 * @brief FAT filesystem configuration for a TinyUSB MSC storage instance.
 */
typedef struct {
    char *base_path;                        /*!< Filesystem mount path.
                                                 Set to NULL to use the default component path. */
    esp_vfs_fat_mount_config_t config;      /*!< FAT mount configuration. */
    bool do_not_format;                     /*!< If true, fail instead of formatting media with no filesystem. */
    BYTE format_flags;                      /*!< FatFs format flags. Set to 0 to use FM_ANY. */
} tinyusb_msc_fatfs_config_t;

/**
 * @brief TinyUSB MSC event callback type.
 *
 * @param[in] handle Storage handle associated with the event.
 * @param[in] event Event data.
 * @param[in] arg User argument configured for the callback.
 */
typedef void(*tusb_msc_callback_t)(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg);

/**
 * @brief TinyUSB MSC storage configuration.
 */
typedef struct {
    union {
        wl_handle_t wl_handle;              /*!< Wear levelling handle for SPI flash storage. */
#if (SOC_SDMMC_HOST_SUPPORTED)
        sdmmc_card_t *card;                 /*!< SD/MMC card descriptor. */
#endif // SOC_SDMMC_HOST_SUPPORTED
    } medium;                               /*!< Storage medium selector. */
    tinyusb_msc_fatfs_config_t fat_fs;      /*!< FAT filesystem configuration. */
    tinyusb_msc_mount_point_t mount_point;  /*!< Requested initial storage owner after creation. */
} tinyusb_msc_storage_config_t;

/**
 * @brief TinyUSB MSC driver configuration.
 */
typedef struct {
    union {
        struct {
            uint16_t auto_mount_off: 1;     /*!< Disable automatic remounting on USB connect and disconnect. */
            uint16_t reserved15: 15;        /*!< Reserved, set to 0. */
        };
        uint16_t val;                       /*!< Raw flag value. */
    } user_flags;                           /*!< Driver flags. */
    tusb_msc_callback_t callback;           /*!< Optional storage event callback. */
    void *callback_arg;                     /*!< User argument passed to `callback`. */
} tinyusb_msc_driver_config_t;

// ----------------------------- Driver API ---------------------------------

/**
 * @brief Install the TinyUSB MSC driver.
 *
 * @param[in] config Driver configuration. Must not be NULL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `config` is NULL
 *      - ESP_ERR_INVALID_STATE if the driver is already installed
 *      - ESP_ERR_NO_MEM if memory allocation fails
 */
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config);

/**
 * @brief Uninstall the TinyUSB MSC driver.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_SUPPORTED if the driver is not installed
 *      - ESP_ERR_INVALID_STATE if one or more storage instances are still present
 */
esp_err_t tinyusb_msc_uninstall_driver(void);

/**
 * @brief Create a TinyUSB MSC storage instance backed by SPI flash.
 *
 * @param[in] config Storage configuration. Must not be NULL.
 * @param[out] handle Optional output for the created storage handle.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `config` is NULL or `config->medium.wl_handle` is invalid
 *      - ESP_ERR_NOT_SUPPORTED if the TinyUSB MSC buffer is smaller than the wear levelling sector size
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - ESP_FAIL if the storage cannot be mapped to a LUN
 *      - Other error codes from driver installation, storage medium setup, or filesystem mounting
 */
esp_err_t tinyusb_msc_new_storage_spiflash(const tinyusb_msc_storage_config_t *config,
                                           tinyusb_msc_storage_handle_t *handle);

#if (SOC_SDMMC_HOST_SUPPORTED)
/**
 * @brief Create a TinyUSB MSC storage instance backed by an SD/MMC card.
 *
 * @param[in] config Storage configuration. Must not be NULL.
 * @param[out] handle Optional output for the created storage handle.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `config` is NULL or `config->medium.card` is NULL
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - ESP_FAIL if the storage cannot be mapped to a LUN
 *      - Other error codes from driver installation, storage medium setup, or filesystem mounting
 */
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config,
                                        tinyusb_msc_storage_handle_t *handle);
#endif // SOC_SDMMC_HOST_SUPPORTED

/**
 * @brief Delete a TinyUSB MSC storage instance.
 *
 * @param[in] handle Storage handle returned by a storage creation function.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `handle` is NULL
 *      - ESP_ERR_INVALID_STATE if the driver is not installed, no storage exists, or deferred writes are pending
 *      - ESP_ERR_NOT_FOUND if the storage is not mapped to any LUN
 */
esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle);

/**
 * @brief Set the TinyUSB MSC storage event callback.
 *
 * @param[in] callback Callback function.
 * @param[in] arg User argument passed to the callback.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the driver is not installed
 *      - ESP_ERR_INVALID_ARG if `callback` is NULL
 */
esp_err_t tinyusb_msc_set_storage_callback(tusb_msc_callback_t callback, void *arg);

/**
 * @brief Format a storage instance with a FAT filesystem.
 *
 * @note This function erases all data on the storage media.
 * @note The storage must be mounted to TINYUSB_MSC_STORAGE_MOUNT_APP.
 *
 * @param[in] handle Storage handle returned by a storage creation function.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the MSC driver is not initialized
 *      - ESP_ERR_INVALID_ARG if `handle` is NULL or the storage is not mounted to the application
 *      - ESP_ERR_NOT_FOUND if the media is not in the expected unformatted state
 *      - Other error codes from disk I/O, VFS, or FAT filesystem operations
 */
esp_err_t tinyusb_msc_format_storage(tinyusb_msc_storage_handle_t handle);

/**
 * @brief Update FAT filesystem parameters for a storage instance.
 *
 * @param[in] handle Storage handle returned by a storage creation function.
 * @param[in] fatfs_config FAT filesystem configuration.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `fatfs_config` is NULL
 *      - ESP_ERR_INVALID_STATE if the MSC driver or storage is not initialized
 */
esp_err_t tinyusb_msc_config_storage_fat_fs(tinyusb_msc_storage_handle_t handle,
                                            tinyusb_msc_fatfs_config_t *fatfs_config);

/**
 * @brief Request the active mount point for a storage instance.
 *
 * This function requests switching storage ownership between the application
 * and the USB host.
 *
 * @note This function does not propagate failures from the internal
 *       mount/unmount helpers to the caller.
 *
 * @param[in] handle Storage handle returned by a storage creation function.
 * @param[in] mount_point Requested mount point.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the MSC driver is not installed
 */
esp_err_t tinyusb_msc_set_storage_mount_point(tinyusb_msc_storage_handle_t handle,
                                              tinyusb_msc_mount_point_t mount_point);

// ------------------------------------ Getters ------------------------------------

/**
 * @brief Get the storage capacity in sectors.
 *
 * @param[in] handle Storage handle returned by a storage creation function.
 * @param[out] sector_count Number of sectors in the storage medium.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `sector_count` is NULL
 *      - ESP_ERR_INVALID_STATE if the MSC driver or storage is not initialized
 */
esp_err_t tinyusb_msc_get_storage_capacity(tinyusb_msc_storage_handle_t handle, uint32_t *sector_count);

/**
 * @brief Get the storage sector size in bytes.
 *
 * @param[in] handle Storage handle returned by a storage creation function.
 * @param[out] sector_size Sector size in bytes.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `sector_size` is NULL
 *      - ESP_ERR_INVALID_STATE if the MSC driver or storage is not initialized
 */
esp_err_t tinyusb_msc_get_storage_sector_size(tinyusb_msc_storage_handle_t handle, uint32_t *sector_size);

/**
 * @brief Get the current mount point of a storage instance.
 *
 * @param[in] handle Storage handle returned by a storage creation function.
 * @param[out] mount_point Current mount point.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `mount_point` is NULL
 *      - ESP_ERR_INVALID_STATE if the MSC driver or storage is not initialized
 */
esp_err_t tinyusb_msc_get_storage_mount_point(tinyusb_msc_storage_handle_t handle,
                                              tinyusb_msc_mount_point_t *mount_point);

#ifdef __cplusplus
}
#endif
