/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "usb/cdc_host_types.h"

#define NANJING_QINHENG_MICROE_VID (0x1A86)
#define CH340_PID                  (0x7522)
#define CH340_PID_1                (0x7523)
#define CH341_PID                  (0x5523)

// Auto detect supported PIDs
#define CH34X_PID_AUTO             (0)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open CH34x device
 *
 * @param[in]  pid           PID of the device
 * @param[in]  interface_idx Interface number
 * @param[in]  dev_config    CDC device configuration
 * @param[out] cdc_hdl_ret   Pointer to the CDC handle
 * @return
 *    - ESP_OK: Success
 *    - ESP_ERR_NOT_FOUND: Device not found
 *    - ESP_ERR_NO_MEM: No memory
 */
esp_err_t ch34x_vcp_open(uint16_t pid, uint8_t interface_idx, const cdc_acm_host_device_config_t *dev_config, cdc_acm_dev_hdl_t *cdc_hdl_ret);

#ifdef __cplusplus
}
#endif
