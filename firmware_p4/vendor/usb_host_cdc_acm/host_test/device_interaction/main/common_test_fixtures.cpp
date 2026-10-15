/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <catch2/catch_test_macros.hpp>
#include "usb/cdc_acm_host.h"
#include "mock_add_usb_device.h"
#include "common_test_fixtures.hpp"
#include "cdc_host_descriptor_parsing.h"

extern "C" {
#include "Mockusb_host.h"
}

/**
 * @brief CMock expectations for current CDC device
 *
 * Only used for mocked usb host expectations
 *
 * For example, based on the descriptors, we record that a certain device has CTRL EP, IN EP, OUT EP and NO notif EP
 * Then, during the device opening we expect usb_host_transfer_alloc() exactly 3 times (CTRL, IN, OUT)
 * Also, during the device closing we expect usb_host_transfer_free() exactly 3 times (CTRL, IN, OUT)
 * Different device could have CTRL, IN, OUT and NOTIF, thus we must expect those mocked functions exactly 4 times
 */
typedef struct {
    struct {
        usb_transfer_t *out_xfer;
        usb_transfer_t *in_xfer;
        uint8_t in_bEndpointAddress;
        uint8_t out_bEndpointAddress;
    } data;
    struct {
        usb_transfer_t *xfer;
        uint8_t bEndpointAddress;
        bool has_separate_interface;
    } notif;
} cdc_dev_expects_t;

/** Per-device CMock state keyed by CDC handle (supports multiple open devices). */
static std::unordered_map<cdc_acm_dev_hdl_t, cdc_dev_expects_t *> s_cdc_dev_expects_by_hdl;

/**
 * @brief Lookup table with CMock expectations for current device
 *
 * The function goes through the lookup table to find the CMock expectations for the current device
 * based on the device handle (CDC handle) and returns a pointer to the expectations of that device
 *
 * @param dev_hdl Device handle
 * @return pointer to cmock expectations for this device, or nullptr if not found
 */
static cdc_dev_expects_t *_test_cdc_expects_lookup(cdc_acm_dev_hdl_t dev_hdl)
{
    auto it = s_cdc_dev_expects_by_hdl.find(dev_hdl);
    return (it != s_cdc_dev_expects_by_hdl.end()) ? it->second : nullptr;
}

/**
 * @brief Free all CMock expectations and clear the lookup table
 */
static void _test_cdc_expects_free_all(void)
{
    for (auto &kv : s_cdc_dev_expects_by_hdl) {
        free(kv.second);
    }
    s_cdc_dev_expects_by_hdl.clear();
}

/**
 * @brief Free CMock expectations for a specific device and remove it from the lookup table
 *
 * The function goes through the lookup table to find the CMock expectations for the current device based on the device handle (CDC handle).
 *
 * @param dev_hdl Device handle to be freed
 */
static void _test_cdc_expects_free(cdc_acm_dev_hdl_t dev_hdl)
{
    auto dev_hdl_found = s_cdc_dev_expects_by_hdl.find(dev_hdl);
    if (dev_hdl_found != s_cdc_dev_expects_by_hdl.end()) {
        free(dev_hdl_found->second);
        s_cdc_dev_expects_by_hdl.erase(dev_hdl_found);
    }
}

/**
 * @brief Create CMock expectations for current device
 *
 * This function creates CMock expectations for a USB device,
 *
 * @param[in] dev_address Device address
 * @param[in] interface_index Interface index to be used
 * @param[in] dev_config CDC-ACM Host device config struct
 * @param[out] out_expects Allocated expectations for this open (caller registers on success)
 *
 * @return
 *   - ESP_OK: Mock expectations created successfully
 *   - ESP_ERR_NO_MEM: Not enough memory
 */
static esp_err_t _test_create_cmock_expectations(uint8_t dev_address, uint8_t interface_index, const cdc_acm_host_device_config_t *dev_config,
                                                 cdc_dev_expects_t **out_expects)
{
    cdc_dev_expects_t *cdc_dev_expects = (cdc_dev_expects_t *)calloc(1, sizeof(cdc_dev_expects_t));
    if (cdc_dev_expects == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    *out_expects = cdc_dev_expects;

    int notif_xfer, data_in_xfer, data_out_xfer;
    const usb_config_desc_t *config_desc;
    const usb_device_desc_t *device_desc;
    cdc_parsed_info_t cdc_info;
    usb_host_mock_get_config_descriptor_by_address(dev_address, &config_desc);
    usb_host_mock_get_device_descriptor_by_address(dev_address, &device_desc);
    cdc_parse_interface_descriptor(device_desc, config_desc, interface_index, &cdc_info);

    // Get IN and OUT endpoints addresses
    cdc_dev_expects->data.in_bEndpointAddress = cdc_info.in_ep->bEndpointAddress;
    cdc_dev_expects->data.out_bEndpointAddress = cdc_info.out_ep->bEndpointAddress;

    // Get notification endpoint address and check if notification transfer is allocated (if notif. EP exists)
    if (cdc_info.notif_ep) {
        cdc_dev_expects->notif.bEndpointAddress = cdc_info.notif_ep->bEndpointAddress;
        cdc_dev_expects->notif.xfer = reinterpret_cast<usb_transfer_t *>(&notif_xfer);
        if (cdc_info.notif_intf != cdc_info.data_intf) {
            cdc_dev_expects->notif.has_separate_interface = true;
        }
    } else {
        cdc_dev_expects->notif.bEndpointAddress = 0;
        cdc_dev_expects->notif.xfer = nullptr;
    }

    // Check, if IN data transfer is allocated
    if (dev_config->in_buffer_size) {
        cdc_dev_expects->data.in_xfer = reinterpret_cast<usb_transfer_t *>(&data_in_xfer);
    } else {
        cdc_dev_expects->data.in_xfer = nullptr;
    }

    // Check if OUT data transfer is allocated
    if (dev_config->out_buffer_size) {
        cdc_dev_expects->data.out_xfer = reinterpret_cast<usb_transfer_t *>(&data_out_xfer);
    } else {
        cdc_dev_expects->data.out_xfer = nullptr;
    }

    return ESP_OK;
}

esp_err_t test_cdc_acm_host_install(const cdc_acm_host_driver_config_t *driver_config)
{
    usb_host_client_register_ExpectAnyArgsAndReturn(ESP_OK);
    usb_host_client_register_AddCallback(usb_host_client_register_mock_callback);

    usb_host_client_handle_events_ExpectAnyArgsAndReturn(ESP_OK);
    usb_host_client_handle_events_AddCallback(usb_host_client_handle_events_mock_callback);

    // Call the real function cdc_acm_host_install()
    return cdc_acm_host_install(driver_config);
}

esp_err_t test_cdc_acm_host_uninstall(void)
{
    usb_host_client_unblock_ExpectAnyArgsAndReturn(ESP_OK);
    usb_host_client_unblock_AddCallback(usb_host_client_unblock_mock_callback);

    usb_host_client_deregister_ExpectAnyArgsAndReturn(ESP_OK);
    usb_host_client_deregister_AddCallback(usb_host_client_deregister_mock_callback);

    _test_cdc_expects_free_all();

    // Call the real function, cdc_acm_host_uninstall()
    return cdc_acm_host_uninstall();
}

esp_err_t test_cdc_acm_host_open(uint8_t dev_address, uint16_t vid, uint16_t pid, uint8_t interface_index, const cdc_acm_host_device_config_t *dev_config, cdc_acm_dev_hdl_t *cdc_hdl_ret)
{
    cdc_dev_expects_t *cdc_dev_expects = nullptr;
    esp_err_t ret = _test_create_cmock_expectations(dev_address, interface_index, dev_config, &cdc_dev_expects);
    if (ret != ESP_OK) {
        return ret;      // ESP_ERR_NO_MEM
    }

    // Basic USB Host stack operations are stubbed
    usb_host_device_addr_list_fill_Stub(usb_host_device_addr_list_fill_mock_callback);
    usb_host_device_info_Stub(usb_host_device_info_mock_callback);
    usb_host_get_device_descriptor_Stub(usb_host_get_device_descriptor_mock_callback);
    usb_host_get_active_config_descriptor_Stub(usb_host_get_active_config_descriptor_mock_callback);

    // We are expecting usb_host_device_open, usb_host_get_device_descriptor usb_host_device_close
    // to be called at least mocked_devs_count times
    const int mocked_devs_count = usb_host_mock_get_devs_count();   // Get number of mocked USB devices in mocked device list
    for (int i = 0; i < mocked_devs_count; i++) {
        usb_host_device_open_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_device_close_ExpectAnyArgsAndReturn(ESP_OK);
    }

    usb_host_device_open_AddCallback(usb_host_device_open_mock_callback);
    usb_host_device_close_AddCallback(usb_host_device_close_mock_callback);

    // Setup control transfer
    usb_host_transfer_alloc_ExpectAnyArgsAndReturn(ESP_OK);

    // Setup Notif transfer
    if (cdc_dev_expects->notif.xfer) {
        usb_host_transfer_alloc_ExpectAnyArgsAndReturn(ESP_OK);
    }

    //  Setup IN data transfer
    if (dev_config->in_buffer_size) {
        usb_host_transfer_alloc_ExpectAnyArgsAndReturn(ESP_OK);
    }

    // Setup OUT bulk transfer
    if (dev_config->out_buffer_size) {
        usb_host_transfer_alloc_ExpectAnyArgsAndReturn(ESP_OK);
    }

    // Register callback
    usb_host_transfer_alloc_AddCallback(usb_host_transfer_alloc_mock_callback);

    // Call cdc_acm_start

    // Claim 1st interface
    // Make sure that the interface_index has been claimed
    const bool sep_notif = cdc_dev_expects->notif.has_separate_interface;
    test_usb_host_interface_claim(interface_index, sep_notif);

    // Claim 2nd interface (if supported)
    if (cdc_dev_expects->notif.has_separate_interface) {
        test_usb_host_interface_claim(interface_index + 1, sep_notif);
    } else if (cdc_dev_expects->notif.xfer) {
        // If the device has no separate interface, but has notification endpoint
        usb_host_transfer_submit_ExpectAnyArgsAndReturn(ESP_OK);
    }

    // Call the real function cdc_acm_host_open
    // Expect ESP_OK and dev_hdl non nullptr
    ret = cdc_acm_host_open(vid, pid, interface_index, dev_config, cdc_hdl_ret);

    // If the cdc_acm_host_open() fails, delete the created cdc_device
    if (ret != ESP_OK) {
        free(cdc_dev_expects);
    } else {
        s_cdc_dev_expects_by_hdl[*cdc_hdl_ret] = cdc_dev_expects;
    }
    return ret;
}

esp_err_t test_cdc_acm_host_close(cdc_acm_dev_hdl_t *cdc_hdl, uint8_t interface_index)
{
    cdc_acm_dev_hdl_t hdl = *cdc_hdl;
    cdc_dev_expects_t *exp = _test_cdc_expects_lookup(hdl);
    if (exp == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Cancel pooling of IN endpoint -> halt, flush, clear
    test_cdc_acm_reset_transfer_endpoint(exp->data.in_bEndpointAddress);

    // Cancel pooling of Notification endpoint -> halt, flush, clear
    if (exp->notif.xfer) {
        test_cdc_acm_reset_transfer_endpoint(exp->notif.bEndpointAddress);
    }

    // Release data interface
    if (exp->notif.has_separate_interface) {
        usb_host_interface_release_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_interface_release_ExpectAnyArgsAndReturn(ESP_OK);
    } else {
        usb_host_interface_release_ExpectAndReturn(nullptr, nullptr, interface_index, ESP_OK);
    }
    usb_host_interface_release_IgnoreArg_client_hdl();  // Ignore all function parameters, except interface_index
    usb_host_interface_release_IgnoreArg_dev_hdl();


    // Free notif transfer
    if (exp->notif.xfer) {
        usb_host_transfer_free_ExpectAnyArgsAndReturn(ESP_OK);
    }

    // Free in transfer
    if (exp->data.in_xfer) {
        usb_host_transfer_free_ExpectAnyArgsAndReturn(ESP_OK);
    }

    // Free out transfer
    if (exp->data.out_xfer) {
        usb_host_transfer_free_ExpectAnyArgsAndReturn(ESP_OK);
    }

    // Call cdc_acm_device_remove
    // Free ctrl transfer
    usb_host_transfer_free_ExpectAnyArgsAndReturn(ESP_OK);
    usb_host_transfer_free_AddCallback(usb_host_transfer_free_mock_callback);

    usb_host_device_close_ExpectAnyArgsAndReturn(ESP_OK);

    // Call the real function cdc_acm_host_close
    esp_err_t ret = cdc_acm_host_close(*cdc_hdl);
    if (ret == ESP_OK) {
        _test_cdc_expects_free(hdl);
    }
    return ret;
}

esp_err_t test_cdc_acm_host_data_tx_blocking(cdc_acm_dev_hdl_t cdc_hdl, const uint8_t *data, size_t data_len, uint32_t timeout_ms, mock_usb_transfer_response_t transfer_response)
{
    cdc_dev_expects_t *exp = _test_cdc_expects_lookup(cdc_hdl);
    if (exp == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    usb_host_transfer_submit_ExpectAnyArgsAndReturn(ESP_OK);

    switch (transfer_response) {
    case MOCK_USB_TRANSFER_SUCCESS: {
        // Make the submitted transfer to pass
        usb_host_transfer_submit_AddCallback(usb_host_transfer_submit_success_mock_callback);
        break;
    }
    case MOCK_USB_TRANSFER_ERROR: {
        // Make the submitted transfer to fail
        usb_host_transfer_submit_AddCallback(usb_host_transfer_submit_invalid_response_mock_callback);
        break;
    }
    case MOCK_USB_TRANSFER_TIMEOUT: {
        // Make the submitted transfer to be timed out
        usb_host_transfer_submit_AddCallback(usb_host_transfer_submit_timeout_mock_callback);
        // Reset out endpoint
        test_cdc_acm_reset_transfer_endpoint(exp->data.out_bEndpointAddress);
        break;
    }
    default:
        break;
    }

    // Call the real function cdc_acm_host_data_tx_blocking()
    return cdc_acm_host_data_tx_blocking(cdc_hdl, data, data_len, timeout_ms);
}

esp_err_t test_cdc_acm_reset_transfer_endpoint(uint8_t ep_address)
{
    // Expect correct ep_address for all (halt, flush, clear)
    usb_host_endpoint_halt_ExpectAndReturn(nullptr, ep_address, ESP_OK);
    usb_host_endpoint_halt_IgnoreArg_dev_hdl();

    usb_host_endpoint_flush_ExpectAndReturn(nullptr, ep_address, ESP_OK);
    usb_host_endpoint_flush_IgnoreArg_dev_hdl();

    usb_host_endpoint_clear_ExpectAndReturn(nullptr, ep_address, ESP_OK);
    usb_host_endpoint_clear_IgnoreArg_dev_hdl();

    return ESP_OK;
}

esp_err_t test_usb_host_interface_claim(uint8_t interface_index, bool has_separate_notification_interface)
{
    usb_host_interface_claim_ExpectAndReturn(nullptr, nullptr, interface_index, 0, ESP_OK);
    if (has_separate_notification_interface) {
        usb_host_interface_claim_IgnoreArg_bInterfaceNumber();
    }
    usb_host_interface_claim_IgnoreArg_client_hdl();        // Ignore all function parameters, except interface_index
    usb_host_interface_claim_IgnoreArg_dev_hdl();
    usb_host_interface_claim_IgnoreArg_bAlternateSetting();
    usb_host_transfer_submit_ExpectAnyArgsAndReturn(ESP_OK);

    return ESP_OK;
}
