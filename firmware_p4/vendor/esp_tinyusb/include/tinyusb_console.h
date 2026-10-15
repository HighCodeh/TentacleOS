/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Redirect the standard console streams to a TinyUSB CDC interface.
 *
 * @param[in] cdc_intf TinyUSB CDC interface number to register as the console.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the selected CDC interface is not initialized
 *      - ESP_FAIL if stdio redirection fails
 *      - Other error codes from VFS registration
 */
esp_err_t tinyusb_console_init(int cdc_intf);

/**
 * @brief Restore the standard console streams.
 *
 * Call this function only after a successful call to tinyusb_console_init().
 *
 * @param[in] cdc_intf CDC interface number kept for API symmetry with
 *                     tinyusb_console_init().
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL if restoring the standard streams fails
 */
esp_err_t tinyusb_console_deinit(int cdc_intf);

#ifdef __cplusplus
}
#endif
