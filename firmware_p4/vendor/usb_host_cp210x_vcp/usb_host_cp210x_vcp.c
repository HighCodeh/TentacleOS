/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_check.h"
#include "usb/usb_types_ch9.h"
#include "usb/cdc_acm_host.h"
#include "esp_private/cdc_host_common.h"
#include "usb/vcp_cp210x.h"

#define CP210X_READ_REQ  (USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_INTERFACE | USB_BM_REQUEST_TYPE_DIR_IN)
#define CP210X_WRITE_REQ (USB_BM_REQUEST_TYPE_TYPE_VENDOR | USB_BM_REQUEST_TYPE_RECIP_INTERFACE | USB_BM_REQUEST_TYPE_DIR_OUT)

// @see AN571: CP210x Virtual COM Port Interface, chapter 5
#define CP210X_CMD_IFC_ENABLE      (0x00) // Enable or disable the interface
#define CP210X_CMD_SET_BAUDDIV     (0x01) // Set the baud rate divisor
#define CP210X_CMD_GET_BAUDDIV     (0x02) // Get the baud rate divisor
#define CP210X_CMD_SET_LINE_CTL    (0x03) // Set the line control
#define CP210X_CMD_GET_LINE_CTL    (0x04) // Get the line control
#define CP210X_CMD_SET_BREAK       (0x05) // Set a BREAK
#define CP210X_CMD_IMM_CHAR        (0x06) // Send character out of order
#define CP210X_CMD_SET_MHS         (0x07) // Set modem handshaking
#define CP210X_CMD_GET_MDMSTS      (0x08) // Get modem status
#define CP210X_CMD_SET_XON         (0x09) // Emulate XON
#define CP210X_CMD_SET_XOFF        (0x0A) // Emulate XOFF
#define CP210X_CMD_SET_EVENTMASK   (0x0B) // Set the event mask
#define CP210X_CMD_GET_EVENTMASK   (0x0C) // Get the event mask
#define CP210X_CMD_GET_EVENTSTATE  (0x16) // Get the event state
#define CP210X_CMD_SET_RECEIVE     (0x17) // Set receiver max timeout
#define CP210X_CMD_GET_RECEIVE     (0x18) // Get receiver max timeout
#define CP210X_CMD_SET_CHAR        (0x0D) // Set special character individually
#define CP210X_CMD_GET_CHARS       (0x0E) // Get special characters
#define CP210X_CMD_GET_PROPS       (0x0F) // Get properties
#define CP210X_CMD_GET_COMM_STATUS (0x10) // Get the serial status
#define CP210X_CMD_RESET           (0x11) // Reset
#define CP210X_CMD_PURGE           (0x12) // Purge
#define CP210X_CMD_SET_FLOW        (0x13) // Set flow control
#define CP210X_CMD_GET_FLOW        (0x14) // Get flow control
#define CP210X_CMD_EMBED_EVENTS    (0x15) // Control embedding of events in the data stream
#define CP210X_CMD_GET_BAUDRATE    (0x1D) // Get the baud rate
#define CP210X_CMD_SET_BAUDRATE    (0x1E) // Set the baud rate
#define CP210X_CMD_SET_CHARS       (0x19) // Set special characters
#define CP210X_CMD_VENDOR_SPECIFIC (0xFF) // Read/write latch values

static const char *TAG = "CP210x";

// This is implementation of USB CDC-ACM compliant functions.
// It strictly follows interface defined in interface/usb/cdc_acm_host_inteface.h
static esp_err_t cp210x_line_coding_get(cdc_acm_dev_hdl_t cdc_hdl, cdc_acm_line_coding_t *line_coding)
{
    assert(line_coding);

    ESP_RETURN_ON_ERROR(
        cdc_acm_host_send_custom_request(
            cdc_hdl, CP210X_READ_REQ, CP210X_CMD_GET_BAUDRATE, 0, cdc_hdl->data.intf_desc->bInterfaceNumber, sizeof(line_coding->dwDTERate), (uint8_t *)&line_coding->dwDTERate), TAG,);

    uint8_t temp_data[2];
    ESP_RETURN_ON_ERROR(
        cdc_acm_host_send_custom_request(
            cdc_hdl, CP210X_READ_REQ, CP210X_CMD_GET_LINE_CTL, 0, cdc_hdl->data.intf_desc->bInterfaceNumber, 2, temp_data), TAG,);
    line_coding->bCharFormat = temp_data[0] & 0x0F;
    line_coding->bParityType = (temp_data[0] & 0xF0) >> 4;
    line_coding->bDataBits   = temp_data[1];

    return ESP_OK;
}

static esp_err_t cp210x_line_coding_set(cdc_acm_dev_hdl_t cdc_hdl, const cdc_acm_line_coding_t *line_coding)
{
    assert(line_coding);

    if (line_coding->dwDTERate != 0) {
        ESP_RETURN_ON_ERROR(
            cdc_acm_host_send_custom_request(
                cdc_hdl, CP210X_WRITE_REQ, CP210X_CMD_SET_BAUDRATE, 0, cdc_hdl->data.intf_desc->bInterfaceNumber, sizeof(line_coding->dwDTERate), (uint8_t *)&line_coding->dwDTERate), TAG,);
    }

    if (line_coding->bDataBits != 0) {
        const uint16_t wValue = line_coding->bCharFormat | (line_coding->bParityType << 4) | (line_coding->bDataBits << 8);
        return cdc_acm_host_send_custom_request(cdc_hdl, CP210X_WRITE_REQ, CP210X_CMD_SET_LINE_CTL, wValue, cdc_hdl->data.intf_desc->bInterfaceNumber, 0, NULL);
    }
    return ESP_OK;
}

static esp_err_t cp210x_set_control_line_state(cdc_acm_dev_hdl_t cdc_hdl, bool dtr, bool rts)
{
    const uint16_t wValue = (uint16_t)dtr | ((uint16_t)rts << 1) | 0x0300;
    return cdc_acm_host_send_custom_request(cdc_hdl, CP210X_WRITE_REQ, CP210X_CMD_SET_MHS, wValue, cdc_hdl->data.intf_desc->bInterfaceNumber, 0, NULL);
}

static esp_err_t cp210x_send_break(cdc_acm_dev_hdl_t cdc_hdl, uint16_t duration_ms)
{
    ESP_RETURN_ON_ERROR(
        cdc_acm_host_send_custom_request(
            cdc_hdl, CP210X_WRITE_REQ, CP210X_CMD_SET_BREAK, 1, cdc_hdl->data.intf_desc->bInterfaceNumber, 0, NULL), TAG,);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return cdc_acm_host_send_custom_request(cdc_hdl, CP210X_WRITE_REQ, CP210X_CMD_SET_BREAK, 0, cdc_hdl->data.intf_desc->bInterfaceNumber, 0, NULL);
}

esp_err_t cp210x_vcp_open(uint16_t pid, uint8_t interface_idx, const cdc_acm_host_device_config_t *dev_config, cdc_acm_dev_hdl_t *cdc_hdl_ret)
{
    esp_err_t ret;
    if (pid == CP210X_PID_AUTO) {
        static const uint16_t supported_pids[] = {CP210X_PID, CP2105_PID, CP2108_PID};
        static const size_t num_pids = sizeof(supported_pids) / sizeof(supported_pids[0]);

        ret = ESP_ERR_NOT_FOUND;
        for (size_t i = 0; i < num_pids; i++) {
            ret = cdc_acm_host_open(SILICON_LABS_VID, supported_pids[i], interface_idx, dev_config, cdc_hdl_ret);
            if (ret == ESP_OK) {
                break;
            }
        }
    } else {
        ret = cdc_acm_host_open(SILICON_LABS_VID, pid, interface_idx, dev_config, cdc_hdl_ret);
    }

    // Set custom function for this driver
    if (ret == ESP_OK) {
        cdc_acm_dev_hdl_t cdc_hdl = *cdc_hdl_ret;
        cdc_hdl->intf_func.line_coding_set = cp210x_line_coding_set;
        cdc_hdl->intf_func.line_coding_get = cp210x_line_coding_get;
        cdc_hdl->intf_func.set_control_line_state = cp210x_set_control_line_state;
        cdc_hdl->intf_func.send_break = cp210x_send_break;

        // CP210x interfaces must be explicitly enabled
        ret = cdc_acm_host_send_custom_request(cdc_hdl, CP210X_WRITE_REQ, CP210X_CMD_IFC_ENABLE, 1, cdc_hdl->data.intf_desc->bInterfaceNumber, 0, NULL);
        if (ret != ESP_OK) {
            cdc_acm_host_close(cdc_hdl);
            *cdc_hdl_ret = NULL;
        }
    }
    return ret;
};
