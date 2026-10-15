/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "usb/usb_types_cdc.h"

// This is implementation of USB CDC-ACM compliant functions.
// It strictly follows interface defined in interface/usb/cdc_acm_host_inteface.h
// and is used by cdc_acm_host.c to provide CDC-ACM compliant interface.
esp_err_t acm_compliant_line_coding_get(cdc_acm_dev_hdl_t cdc_hdl, cdc_acm_line_coding_t *line_coding);
esp_err_t acm_compliant_line_coding_set(cdc_acm_dev_hdl_t cdc_hdl, const cdc_acm_line_coding_t *line_coding);
esp_err_t acm_compliant_set_control_line_state(cdc_acm_dev_hdl_t cdc_hdl, bool dtr, bool rts);
esp_err_t acm_compliant_send_break(cdc_acm_dev_hdl_t cdc_hdl, uint16_t duration_ms);
