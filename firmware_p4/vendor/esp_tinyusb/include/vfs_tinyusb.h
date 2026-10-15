/*
 * SPDX-FileCopyrightText: 2020-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_vfs_common.h" // For esp_line_endings_t definitions

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum VFS path length, including the terminating null byte.
 */
#define VFS_TUSB_MAX_PATH 16

/**
 * @brief Default VFS path used for TinyUSB CDC registration.
 */
#define VFS_TUSB_PATH_DEFAULT "/dev/tusb_cdc"

/**
 * @brief Register a TinyUSB CDC interface in VFS.
 *
 * Only one TinyUSB CDC interface can be registered in VFS at a time.
 *
 * @param[in] cdc_intf TinyUSB CDC interface number.
 * @param[in] path VFS path to register. Set to NULL to use
 *                 VFS_TUSB_PATH_DEFAULT.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the selected CDC interface is not initialized
 *      - ESP_ERR_INVALID_ARG if `path` is too long
 *      - Other error codes from VFS registration
 */
esp_err_t esp_vfs_tusb_cdc_register(int cdc_intf, char const *path);

/**
 * @brief Unregister a TinyUSB CDC interface from VFS.
 *
 * @param[in] path VFS path to unregister. Set to NULL to use
 *                 VFS_TUSB_PATH_DEFAULT.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `path` does not match the registered TinyUSB VFS path
 *      - Other error codes from VFS unregistration
 */
esp_err_t esp_vfs_tusb_cdc_unregister(char const *path);

/**
 * @brief Set the transmitted line ending conversion mode.
 *
 * This controls how newline characters (`\n`) written to stdout are converted
 * before they are sent over the TinyUSB CDC VFS stream:
 *
 * - ESP_LINE_ENDINGS_CRLF: convert LF to CRLF
 * - ESP_LINE_ENDINGS_CR: convert LF to CR
 * - ESP_LINE_ENDINGS_LF: no modification
 *
 * @param[in] mode Line ending conversion mode.
 */
void esp_vfs_tusb_cdc_set_tx_line_endings(esp_line_endings_t mode);

/**
 * @brief Set the received line ending conversion mode.
 *
 * This controls how line endings received from the TinyUSB CDC VFS stream are
 * converted before they are passed to stdin as newline characters (`\n`):
 *
 * - ESP_LINE_ENDINGS_CRLF: convert CRLF to LF
 * - ESP_LINE_ENDINGS_CR: convert CR to LF
 * - ESP_LINE_ENDINGS_LF: no modification
 *
 * @param[in] mode Line ending conversion mode.
 */
void esp_vfs_tusb_cdc_set_rx_line_endings(esp_line_endings_t mode);

#ifdef __cplusplus
}
#endif
