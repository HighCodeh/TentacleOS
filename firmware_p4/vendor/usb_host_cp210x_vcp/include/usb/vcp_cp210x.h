/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "usb/cdc_host_types.h"

#define SILICON_LABS_VID (0x10C4)
#define CP210X_PID       (0xEA60) // Single i.e. CP2101 - CP2104
#define CP2105_PID       (0xEA70) // Dual
#define CP2108_PID       (0xEA71) // Quad

// Auto detect supported PIDs
#define CP210X_PID_AUTO  (0)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open CP210x device
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
esp_err_t cp210x_vcp_open(uint16_t pid, uint8_t interface_idx, const cdc_acm_host_device_config_t *dev_config, cdc_acm_dev_hdl_t *cdc_hdl_ret);

#ifdef __cplusplus
}
#endif
