/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_check.h"
#include "usb/cdc_acm_host.h"
#include "cdc_host_common.h"

static const char *TAG = "cdc_acm_compliant";

/**
 * @brief Send CDC specific request
 *
 * Helper function that will send CDC specific request to default endpoint.
 * Both IN and OUT requests are sent through this API, depending on the in_transfer parameter.
 *
 * @see  Chapter 6.2, USB CDC specification rev. 1.2
 * @note CDC specific requests are only supported by devices that have dedicated management element.
 *
 * @param[in] cdc_dev Pointer to CDC device
 * @param[in] in_transfer Direction of data phase. true: IN, false: OUT
 * @param[in] request CDC request code
 * @param[inout] data Pointer to data buffer. Input for OUT transfers, output for IN transfers.
 * @param[in] data_len Length of data buffer
 * @param[in] value Value to be set in bValue of Setup packet
 * @return
 *    - ESP_OK: Success
 *    - ESP_ERR_INVALID_ARG:      Invalid device or request
 *    - ESP_ERR_NOT_SUPPORTED:    Device does not have management element
 *    - ESP_ERR_INVALID_SIZE:     wLength does not fit CTRL data buffer
 *    - ESP_ERR_TIMED_OUT:        Request timeout (5 seconds)
 *    - ESP_ERR_INVALID_RESPONSE: Control transfer failed
 */
static esp_err_t send_cdc_request(cdc_dev_t *cdc_dev, bool in_transfer, cdc_request_code_t request, uint8_t *data, uint16_t data_len, uint16_t value)
{
    CDC_ACM_CHECK(cdc_dev->notif.intf_desc, ESP_ERR_NOT_SUPPORTED);

    uint8_t req_type = USB_BM_REQUEST_TYPE_TYPE_CLASS | USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    if (in_transfer) {
        req_type |= USB_BM_REQUEST_TYPE_DIR_IN;
    } else {
        req_type |= USB_BM_REQUEST_TYPE_DIR_OUT;
    }
    return cdc_acm_host_send_custom_request((cdc_acm_dev_hdl_t) cdc_dev, req_type, request, value, cdc_dev->notif.intf_desc->bInterfaceNumber, data_len, data);
}

esp_err_t acm_compliant_line_coding_get(cdc_acm_dev_hdl_t cdc_hdl, cdc_acm_line_coding_t *line_coding)
{
    CDC_ACM_CHECK(line_coding, ESP_ERR_INVALID_ARG);

    ESP_RETURN_ON_ERROR(
        send_cdc_request((cdc_dev_t *)cdc_hdl, true, USB_CDC_REQ_GET_LINE_CODING, (uint8_t *)line_coding, sizeof(cdc_acm_line_coding_t), 0),
        TAG,);
    ESP_LOGD(TAG, "Line Get: Rate: %"PRIu32", Stop bits: %d, Parity: %d, Databits: %d", line_coding->dwDTERate,
             line_coding->bCharFormat, line_coding->bParityType, line_coding->bDataBits);
    return ESP_OK;
}

esp_err_t acm_compliant_line_coding_set(cdc_acm_dev_hdl_t cdc_hdl, const cdc_acm_line_coding_t *line_coding)
{
    CDC_ACM_CHECK(line_coding, ESP_ERR_INVALID_ARG);

    ESP_RETURN_ON_ERROR(
        send_cdc_request((cdc_dev_t *)cdc_hdl, false, USB_CDC_REQ_SET_LINE_CODING, (uint8_t *)line_coding, sizeof(cdc_acm_line_coding_t), 0),
        TAG,);
    ESP_LOGD(TAG, "Line Set: Rate: %"PRIu32", Stop bits: %d, Parity: %d, Databits: %d", line_coding->dwDTERate,
             line_coding->bCharFormat, line_coding->bParityType, line_coding->bDataBits);
    return ESP_OK;
}

esp_err_t acm_compliant_set_control_line_state(cdc_acm_dev_hdl_t cdc_hdl, bool dtr, bool rts)
{
    const uint16_t ctrl_bitmap = (uint16_t)dtr | ((uint16_t)rts << 1);

    ESP_RETURN_ON_ERROR(
        send_cdc_request((cdc_dev_t *)cdc_hdl, false, USB_CDC_REQ_SET_CONTROL_LINE_STATE, NULL, 0, ctrl_bitmap),
        TAG,);
    ESP_LOGD(TAG, "Control Line Set: DTR: %d, RTS: %d", dtr, rts);
    return ESP_OK;
}

esp_err_t acm_compliant_send_break(cdc_acm_dev_hdl_t cdc_hdl, uint16_t duration_ms)
{
    ESP_RETURN_ON_ERROR(
        send_cdc_request((cdc_dev_t *)cdc_hdl, false, USB_CDC_REQ_SEND_BREAK, NULL, 0, duration_ms),
        TAG,);

    // Block until break is deasserted
    vTaskDelay(pdMS_TO_TICKS(duration_ms + 1));
    return ESP_OK;
}
