/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"

#pragma once

/**
 * @brief Intended transfer response
 *
 * It can be defined how the transfer is submitted
 * We can submit a transfer successfully, or we can induce some transfer errors
 */
typedef enum {
    MOCK_USB_TRANSFER_SUCCESS,  /*!< Transfer submitted successfully */
    MOCK_USB_TRANSFER_ERROR,    /*!< Transfer error */
    MOCK_USB_TRANSFER_TIMEOUT   /*!< Transfer timeout */
} mock_usb_transfer_response_t;


/**
 * @brief Host test fixture function, for installing CDC-ACM host driver
 *
 * - This function calls the real cdc_acm_host_install() function with USB Host stack CMock expectations
 *
 * @param[in] driver_config configuration for CDC-ACM Host
 *
 * @note This function returns the same parameters as the real the cdc_acm_host_install() function
 * @return
 *   - ESP_OK: Success
 *   - ESP_ERR_INVALID_STATE: The CDC driver is already installed or USB host library is not installed
 *   - ESP_ERR_NO_MEM: Not enough memory for installing the driver
 */
esp_err_t test_cdc_acm_host_install(const cdc_acm_host_driver_config_t *driver_config);

/**
 * @brief Host test fixture function, for uninstalling CDC-ACM host driver
 *
 * - This function calls the real cdc_acm_host_uninstall() function with USB Host stack CMock expectations
 *
 * @note This function returns the same parameters as the real the cdc_acm_host_uninstall() function
 * @return
 *   - ESP_OK: Success
 *   - ESP_ERR_INVALID_STATE: The CDC driver is not installed or not all CDC devices are closed
 *   - ESP_ERR_NOT_FINISHED: The CDC driver failed to uninstall completely
 */
esp_err_t test_cdc_acm_host_uninstall(void);

/**
 * @brief Host test fixture function, for opening CDC-ACM device
 *
 * - This function calls the real cdc_acm_host_open() functions with USB Host stack CMock expectations
 *
 * @param[in] device_addr     Device's Address
 * @param[in] vid             Device's Vendor ID
 * @param[in] pid             Device's Product ID
 * @param[in] interface_index Index of device's interface used for CDC-ACM communication
 * @param[in] dev_config      Configuration structure of the device
 * @param[out] cdc_hdl_ret    CDC device handle
 *
 * @note This function returns the same parameters as the real the cdc_acm_host_open() function
 * @return
 *   - ESP_OK: Success
 *   - ESP_ERR_INVALID_STATE: The CDC driver is not installed
 *   - ESP_ERR_INVALID_ARG: dev_config or cdc_hdl_ret is NULL
 *   - ESP_ERR_NO_MEM: Not enough memory for opening the device
 *   - ESP_ERR_NOT_FOUND: USB device with specified VID/PID is not connected or does not have specified interface
 */
esp_err_t test_cdc_acm_host_open(uint8_t device_addr, uint16_t vid, uint16_t pid, uint8_t interface_index, const cdc_acm_host_device_config_t *dev_config, cdc_acm_dev_hdl_t *cdc_hdl_ret);

/**
 * @brief Host test fixture function, for closing CDC-ACM device
 *
 * - This function calls the real cdc_acm_host_close() functions with USB Host stack CMock expectations
 *
 * @param[in] cdc_hdl CDC handle obtained from cdc_acm_host_open()
 * @param[in] interface_index Index of device's interface used for CDC-ACM communication
 *
 * @note This function returns the same parameters as the real the cdc_acm_host_open() function
 * @return
 *   - ESP_OK: Success
 *   - ESP_ERR_INVALID_STATE: The CDC driver is not installed
 *   - ESP_ERR_INVALID_ARG: dev_config or cdc_hdl_ret is NULL
 *   - ESP_ERR_NO_MEM: Not enough memory for opening the device
 *   - ESP_ERR_NOT_FOUND: USB device with specified VID/PID is not connected or does not have specified interface
 */
esp_err_t test_cdc_acm_host_close(cdc_acm_dev_hdl_t *cdc_hdl, uint8_t interface_index);

/**
 * @brief Host test fixture function, send transfer to mocked device
 *
 * - This function calls the real cdc_acm_host_data_tx_blocking() functions with USB Host stack CMock expectations
 *
 * @param[in] cdc_hdl CDC handle obtained from cdc_acm_host_open()
 * @param[in] data Data to be sent
 * @param[in] data_len Data length
 * @param[in] timeout_ms Timeout in [ms]
 * @param[in] transfer_response Intended transfer response (for testing multiple scenarios)
 *
 * @note This function returns the same parameters as the real the cdc_acm_host_data_tx_blocking() function
 * @return
 *   - ESP_OK: Success
 *   - ESP_ERR_NOT_SUPPORTED: The device was opened as read only
 *   - ESP_ERR_INVALID_ARG: Invalid input arguments
 *   - ESP_ERR_INVALID_SIZE: Invalid size of data to be sent
 *   - ESP_ERR_TIMEOUT: tx transfer has timed out
 *   - ESP_ERR_INVALID_RESPONSE: Invalid transfer response
 */
esp_err_t test_cdc_acm_host_data_tx_blocking(cdc_acm_dev_hdl_t cdc_hdl, const uint8_t *data, size_t data_len, uint32_t timeout_ms, mock_usb_transfer_response_t transfer_response);

/**
 * @brief Host test fixture function, reset endpoint halt -> flush -> clear
 *
 * Call a set of mocked functions to simplify endpoint reset
 *
 * @param ep_address[in] Endpoint address to be reset
 *
 * @return:
 *   - ESP_OK: endpoint reset successful
 */
esp_err_t test_cdc_acm_reset_transfer_endpoint(uint8_t ep_address);

/**
 * @brief Host test fixture function, claim interface
 *
 * Call a set of mocked functions to simplify interface claim
 *
 * @param interface_index[in] Interface index
 * @param has_separate_notification_interface[in] If true, notification interface is on a different interface than data (matches descriptor layout)
 *
 * @return:
 *   - ESP_OK: Interface claim successful
 */
esp_err_t test_usb_host_interface_claim(uint8_t interface_index, bool has_separate_notification_interface = false);
