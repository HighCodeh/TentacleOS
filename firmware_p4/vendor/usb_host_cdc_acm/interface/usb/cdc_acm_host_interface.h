/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#include "usb/usb_types_cdc.h"
#include "usb/cdc_host_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cdc_acm_intf_t cdc_acm_intf_t;

/**
 * @brief USB Host CDC-ACM interface
 *
 * CDC-ACM like devices (such as USB<->Serial converters) can be implemented in different ways.
 * Use this interface to implement CDC-ACM like devices that are not compliant with CDC-ACM specification.
 */
struct cdc_acm_intf_t {
    esp_err_t (*line_coding_set)(cdc_acm_dev_hdl_t cdc_hdl, const cdc_acm_line_coding_t *line_coding);
    esp_err_t (*line_coding_get)(cdc_acm_dev_hdl_t cdc_hdl, cdc_acm_line_coding_t *line_coding);
    esp_err_t (*set_control_line_state)(cdc_acm_dev_hdl_t cdc_hdl, bool dtr, bool rts);
    esp_err_t (*send_break)(cdc_acm_dev_hdl_t cdc_hdl, uint16_t duration_ms);
    void *user_data;                        // Can be used to store device-specific data
    void (*del)(cdc_acm_dev_hdl_t cdc_hdl); // Called when the device is closed, to free any resources allocated by the device
};

#ifdef __cplusplus
}
#endif
