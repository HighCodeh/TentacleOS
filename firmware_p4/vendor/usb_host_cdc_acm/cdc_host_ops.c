/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "usb/cdc_acm_host_ops.h"
#include "cdc_host_common.h"

static const char *TAG = "cdc_acm_ops";

esp_err_t cdc_acm_host_line_coding_set(cdc_acm_dev_hdl_t cdc_hdl, const cdc_acm_line_coding_t *line_coding)
{
    ESP_RETURN_ON_FALSE(cdc_hdl, ESP_ERR_INVALID_ARG, TAG, "invalid CDC handle");
    ESP_RETURN_ON_FALSE(line_coding, ESP_ERR_INVALID_ARG, TAG, "line_coding can't be NULL");
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    if (cdc_hdl->intf_func.line_coding_set) {
        ret = cdc_hdl->intf_func.line_coding_set(cdc_hdl, line_coding);
    }
    return ret;
}

esp_err_t cdc_acm_host_line_coding_get(cdc_acm_dev_hdl_t cdc_hdl, cdc_acm_line_coding_t *line_coding)
{
    ESP_RETURN_ON_FALSE(cdc_hdl, ESP_ERR_INVALID_ARG, TAG, "invalid CDC handle");
    ESP_RETURN_ON_FALSE(line_coding, ESP_ERR_INVALID_ARG, TAG, "line_coding can't be NULL");
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    if (cdc_hdl->intf_func.line_coding_get) {
        ret = cdc_hdl->intf_func.line_coding_get(cdc_hdl, line_coding);
    }
    return ret;
}

esp_err_t cdc_acm_host_set_control_line_state(cdc_acm_dev_hdl_t cdc_hdl, bool dtr, bool rts)
{
    ESP_RETURN_ON_FALSE(cdc_hdl, ESP_ERR_INVALID_ARG, TAG, "invalid CDC handle");
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    if (cdc_hdl->intf_func.set_control_line_state) {
        ret = cdc_hdl->intf_func.set_control_line_state(cdc_hdl, dtr, rts);
    }
    return ret;
}

esp_err_t cdc_acm_host_send_break(cdc_acm_dev_hdl_t cdc_hdl, uint16_t duration_ms)
{
    ESP_RETURN_ON_FALSE(cdc_hdl, ESP_ERR_INVALID_ARG, TAG, "invalid CDC handle");
    esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
    if (cdc_hdl->intf_func.send_break) {
        ret = cdc_hdl->intf_func.send_break(cdc_hdl, duration_ms);
    }
    return ret;
}
