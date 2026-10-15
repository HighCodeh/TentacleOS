/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "usb/cdc_host_types.h"
#include "usb/usb_types_cdc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the line coding reported to the device.
 *
 * @see Chapter 6.3.10, USB CDC-PSTN specification rev. 1.2
 *
 * @param[in] cdc_hdl CDC handle obtained from cdc_acm_host_open().
 * @param[in] line_coding Line coding to apply.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `cdc_hdl` or `line_coding` is NULL
 *      - ESP_ERR_NOT_SUPPORTED if the device does not implement this request
 *      - Other error codes from the device-specific control request
 */
esp_err_t cdc_acm_host_line_coding_set(cdc_acm_dev_hdl_t cdc_hdl, const cdc_acm_line_coding_t *line_coding);

/**
 * @brief Get the current device line coding.
 *
 * @see Chapter 6.3.11, USB CDC-PSTN specification rev. 1.2
 *
 * @param[in] cdc_hdl CDC handle obtained from cdc_acm_host_open().
 * @param[out] line_coding Returned line coding.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `cdc_hdl` or `line_coding` is NULL
 *      - ESP_ERR_NOT_SUPPORTED if the device does not implement this request
 *      - Other error codes from the device-specific control request
 */
esp_err_t cdc_acm_host_line_coding_get(cdc_acm_dev_hdl_t cdc_hdl, cdc_acm_line_coding_t *line_coding);

/**
 * @brief Set the DTR and RTS control line state.
 *
 * @see Chapter 6.3.12, USB CDC-PSTN specification rev. 1.2
 *
 * @param[in] cdc_hdl CDC handle obtained from cdc_acm_host_open().
 * @param[in] dtr Data Terminal Ready state.
 * @param[in] rts Request To Send state.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `cdc_hdl` is NULL
 *      - ESP_ERR_NOT_SUPPORTED if the device does not implement this request
 *      - Other error codes from the device-specific control request
 */
esp_err_t cdc_acm_host_set_control_line_state(cdc_acm_dev_hdl_t cdc_hdl, bool dtr, bool rts);

/**
 * @brief Send a BREAK condition to the device.
 *
 * This call blocks until `duration_ms` has elapsed.
 *
 * @see Chapter 6.3.13, USB CDC-PSTN specification rev. 1.2
 *
 * @param[in] cdc_hdl CDC handle obtained from cdc_acm_host_open().
 * @param[in] duration_ms Duration of the BREAK signal in milliseconds.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `cdc_hdl` is NULL
 *      - ESP_ERR_NOT_SUPPORTED if the device does not implement this request
 *      - Other error codes from the device-specific control request
 */
esp_err_t cdc_acm_host_send_break(cdc_acm_dev_hdl_t cdc_hdl, uint16_t duration_ms);

#ifdef __cplusplus
}
#endif
