/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <catch2/catch_test_macros.hpp>

#include "descriptors/cdc_descriptors.hpp"
#include "usb/cdc_acm_host.h"
#include "mock_add_usb_device.h"
#include "common_test_fixtures.hpp"
#include "usb_helpers.h"
#include "cdc_host_descriptor_parsing.h"
#include "usb/usb_types_ch9.h"

extern "C" {
#include "Mockusb_host.h"
}

/**
 * @brief Add mocked devices
 *
 * Mocked devices are defined by a device descriptor, a configuration descriptor and a device address
 */
static void _add_mocked_devices(void)
{
    // Init mocked devices list at the beginning of the test
    usb_host_mock_dev_list_init();

    // Fill mocked devices list

    // ASIX Electronics Corp. AX88772A Fast Ethernet (FS descriptor)
    REQUIRE(ESP_OK == usb_host_mock_add_device(0, (const usb_device_desc_t *)premium_cord_device_desc_fs,
                                               (const usb_config_desc_t *)premium_cord_config_desc_fs, USB_SPEED_FULL));

    // ASIX Electronics Corp. AX88772B (FS descriptor)
    REQUIRE(ESP_OK == usb_host_mock_add_device(1, (const usb_device_desc_t *)i_tec_device_desc_fs,
                                               (const usb_config_desc_t *)i_tec_config_desc_fs, USB_SPEED_FULL));

    // FTDI chip dual (FS descriptor)
    REQUIRE(ESP_OK == usb_host_mock_add_device(2, (const usb_device_desc_t *)ftdi_device_desc_fs_hs,
                                               (const usb_config_desc_t *)ftdi_config_desc_fs, USB_SPEED_FULL));

    // TTL232RG (FS descriptor)
    REQUIRE(ESP_OK == usb_host_mock_add_device(3, (const usb_device_desc_t *)ttl232_device_desc,
                                               (const usb_config_desc_t *)ttl232_config_desc, USB_SPEED_FULL));

    // CP210x (FS descriptor)
    REQUIRE(ESP_OK == usb_host_mock_add_device(4, (const usb_device_desc_t *)cp210x_device_desc,
                                               (const usb_config_desc_t *)cp210x_config_desc, USB_SPEED_FULL));

    // tusb_serial_device (HS descriptor)
    REQUIRE(ESP_OK == usb_host_mock_add_device(5, (const usb_device_desc_t *)tusb_serial_device_device_desc_fs_hs,
                                               (const usb_config_desc_t *)tusb_serial_device_config_desc_hs, USB_SPEED_HIGH));

    // tusb_serial_dual_device (HS descriptor)
    REQUIRE(ESP_OK == usb_host_mock_add_device(6, (const usb_device_desc_t *)tusb_serial_device_dual_device_desc_fs_hs,
                                               (const usb_config_desc_t *)tusb_serial_device_dual_config_desc_hs, USB_SPEED_HIGH));
}

/**
 * @brief Submit mock transfers to opened mocked device
 *
 * This function will submit couple of transfers to the mocked opened device, to test interaction of CDC-ACM host driver
 * with the mocked USB Host stack and mocked USB device
 *
 * @param[in] dev CDC handle obtained from cdc_acm_host_open()
 */
static void _submit_mock_transfer(cdc_acm_dev_hdl_t *dev)
{
    const uint8_t tx_buf[] = "HELLO";
    // Submit transfer successfully
    REQUIRE(ESP_OK == test_cdc_acm_host_data_tx_blocking(*dev, tx_buf, sizeof(tx_buf), 200, MOCK_USB_TRANSFER_SUCCESS));
    // Submit transfer which will fail to submit
    REQUIRE(ESP_ERR_INVALID_RESPONSE == test_cdc_acm_host_data_tx_blocking(*dev, tx_buf, sizeof(tx_buf), 200, MOCK_USB_TRANSFER_ERROR));
    // Submit transfer which times out
    REQUIRE(ESP_ERR_TIMEOUT == test_cdc_acm_host_data_tx_blocking(*dev, tx_buf, sizeof(tx_buf), 200, MOCK_USB_TRANSFER_TIMEOUT));
}

/**
 * @brief Send cdc-acm specific requests to the device
 *
 * This function checks whether the device is cdc-acm compliant, sends class specific requests to the device and
 * expects device's response according to the compliance
 *
 * @param[in] dev CDC handle
 * @param[in] address device address
 */
static void _set_cdc_acm_specific_requests(cdc_acm_dev_hdl_t *dev, uint8_t address)
{
    // Get mocked device's device descriptor and check whether the device is cdc-acm compliant
    const usb_device_desc_t *device_desc;
    REQUIRE(ESP_OK == usb_host_mock_get_device_descriptor_by_address(address, &device_desc));
    REQUIRE(device_desc != nullptr);
    const uint8_t device_subclass = (uint8_t)(device_desc->bDeviceSubClass);

    if (device_subclass == 0x02) {
        // This device is CDC compliant. By default, all class specific requests are supported (implemented).
        // Expect one ctrl transfer for each class specific request
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);

        cdc_acm_line_coding_t line_coding = {};
        REQUIRE(ESP_OK == cdc_acm_host_line_coding_get(*dev, &line_coding));
        REQUIRE(ESP_OK == cdc_acm_host_line_coding_set(*dev, &line_coding));
        REQUIRE(ESP_OK == cdc_acm_host_set_control_line_state(*dev, false, false));
        REQUIRE(ESP_OK == cdc_acm_host_send_break(*dev, 10));
    } else {
        // This device is not CDC compliant. By default, all class specific requests are not supported (not implemented).

        cdc_acm_line_coding_t line_coding = {};
        REQUIRE(ESP_ERR_NOT_SUPPORTED == cdc_acm_host_line_coding_get(*dev, &line_coding));
        REQUIRE(ESP_ERR_NOT_SUPPORTED == cdc_acm_host_line_coding_set(*dev, &line_coding));
        REQUIRE(ESP_ERR_NOT_SUPPORTED == cdc_acm_host_set_control_line_state(*dev, false, false));
        REQUIRE(ESP_ERR_NOT_SUPPORTED == cdc_acm_host_send_break(*dev, 10));
    }
}

/**
 * @brief Set remote wakeup on the device
 *
 * This function checks whether the device supports remote wakeup feature
 * Sends mocked transfers to the device to test all the error states and expected behaviour of the public function
 *
 * @param[in] dev CDC handle
 * @param[in] address device address
 */
static void _set_remote_wakeup(cdc_acm_dev_hdl_t *dev, uint8_t address)
{
    // Get mocked device's config descriptor and check whether it supports remote wakeup
    const usb_config_desc_t *config_desc;
    REQUIRE(ESP_OK == usb_host_mock_get_config_descriptor_by_address(address, &config_desc));
    REQUIRE(config_desc != nullptr);
    const bool supports_remote_wakeup = bool(config_desc->bmAttributes & USB_BM_ATTRIBUTES_WAKEUP);

    if (supports_remote_wakeup) {
        // Register ctrl transfer function to timeout callback
        usb_host_get_active_config_descriptor_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_transfer_submit_control_AddCallback(usb_host_transfer_submit_control_timeout_mock_callback);
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);
        REQUIRE(ESP_ERR_TIMEOUT == cdc_acm_host_enable_remote_wakeup(*dev, true));

        // Register ctrl transfer function to invalid response callback
        usb_host_get_active_config_descriptor_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_transfer_submit_control_AddCallback(usb_host_transfer_submit_control_invalid_response_mock_callback);
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);
        REQUIRE(ESP_ERR_INVALID_RESPONSE == cdc_acm_host_enable_remote_wakeup(*dev, true));

        // Pass invalid invalid argument and expect ESP_ERR_INVALID_ARG
        REQUIRE(ESP_ERR_INVALID_ARG == cdc_acm_host_enable_remote_wakeup(nullptr, true));

        // Register ctrl transfer function to success callback
        usb_host_transfer_submit_control_AddCallback(usb_host_transfer_submit_control_success_mock_callback);

        // Successfully enable remote wakeup
        usb_host_get_active_config_descriptor_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);
        REQUIRE(ESP_OK == cdc_acm_host_enable_remote_wakeup(*dev, true));

        // Enable remote wakeup again (when the remote wake is already enabled)
        // Don't expect any ctrl transfer to be sent to the device
        usb_host_get_active_config_descriptor_ExpectAnyArgsAndReturn(ESP_OK);
        REQUIRE(ESP_OK == cdc_acm_host_enable_remote_wakeup(*dev, true));

        // Disable remote wakeup
        usb_host_get_active_config_descriptor_ExpectAnyArgsAndReturn(ESP_OK);
        usb_host_transfer_submit_control_ExpectAnyArgsAndReturn(ESP_OK);
        REQUIRE(ESP_OK == cdc_acm_host_enable_remote_wakeup(*dev, false));

        // Disable remote wakeup again (when the remote wake is already disabled)
        usb_host_get_active_config_descriptor_ExpectAnyArgsAndReturn(ESP_OK);
        REQUIRE(ESP_OK == cdc_acm_host_enable_remote_wakeup(*dev, false));
    } else {
        // Device does not support remote wakeup
        // Try to enable it, expect ESP_ERR_NOT_SUPPORTED
        usb_host_get_active_config_descriptor_ExpectAnyArgsAndReturn(ESP_OK);
        REQUIRE(ESP_ERR_NOT_SUPPORTED == cdc_acm_host_enable_remote_wakeup(*dev, true));
    }
}

SCENARIO("Invalid custom command")
{
    Mockusb_host_Init();
    _add_mocked_devices();

    GIVEN("Mocked device is opened") {
        REQUIRE(ESP_OK == test_cdc_acm_host_install(nullptr));

        cdc_acm_dev_hdl_t dev = nullptr;
        const cdc_acm_host_device_config_t dev_config = {
            .connection_timeout_ms = 1000,
            .out_buffer_size = 100,
            .in_buffer_size = 100,
            .event_cb = nullptr,
            .data_cb = nullptr,
            .user_arg = nullptr,
        };

        // Use any device, does not matter for this test
        const uint16_t vid = 0xB95, pid = 0x772A;
        const uint8_t device_address = 0, interface_index = 0;

        // Open a device
        REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index, &dev_config, &dev));
        REQUIRE(dev != nullptr);

        THEN("Command that does not fit into EP0 buffer is rejected with ESP_ERR_INVALID_SIZE") {
            uint8_t data[64 - USB_SETUP_PACKET_SIZE + 1]; // +1 to make sure that the buffer overflows
            REQUIRE(ESP_ERR_INVALID_SIZE == cdc_acm_host_send_custom_request(dev, 0x21, 34, 1, 0, sizeof(data), data));
        }

        THEN("Command length > 0 but data is NULL is rejected with ESP_ERR_INVALID_ARG") {
            REQUIRE(ESP_ERR_INVALID_ARG == cdc_acm_host_send_custom_request(dev, 0x21, 34, 1, 0, 10, nullptr));
        }

        // Close the device
        REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev, interface_index));
        REQUIRE(ESP_OK == test_cdc_acm_host_uninstall());
    }
}

SCENARIO("Interact with mocked USB devices")
{
    Mockusb_host_Init();
    // We put the device adding to the SECTION, to run it just once, not repeatedly for all the following SECTIONs
    SECTION("Add mocked devices") {

        _add_mocked_devices();

        // Optionally, print all the devices
        // usb_host_mock_print_mocked_devices(0xFF);
    }

    GIVEN("Mocked devices are added to the device list") {
        // Install CDC-ACM driver

        REQUIRE(ESP_OK == test_cdc_acm_host_install(nullptr));

        cdc_acm_dev_hdl_t dev = nullptr;
        const cdc_acm_host_device_config_t dev_config = {
            .connection_timeout_ms = 1000,
            .out_buffer_size = 100,
            .in_buffer_size = 100,
            .event_cb = nullptr,
            .data_cb = nullptr,
            .user_arg = nullptr,
        };

        SECTION("Interact with device: ASIX Electronics Corp. AX88772A Fast Ethernet") {

            // Define details of a device which will be opened
            const uint16_t vid = 0xB95, pid = 0x772A;
            const uint8_t device_address = 0, interface_index = 0;

            // Open a device
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index, &dev_config, &dev));
            REQUIRE(dev != nullptr);

            // Interact with the device - set remote wakeup
            _set_remote_wakeup(&dev, device_address);

            // Interact with the device - submit mocked transfers
            _submit_mock_transfer(&dev);

            // Interact with the device - set cdc-acm specific requests
            _set_cdc_acm_specific_requests(&dev, device_address);

            // Close the device
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev, interface_index));
        }

        SECTION("Interact with device: ASIX Electronics Corp. AX88772B") {

            // Define details of a device which will be opened
            const uint16_t vid = 0xB95, pid = 0x772B;
            const uint8_t device_address = 1, interface_index = 0;

            // Open a device
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index, &dev_config, &dev));
            REQUIRE(dev != nullptr);

            // Interact with the device - set remote wakeup
            _set_remote_wakeup(&dev, device_address);

            // Interact with the device - submit mocked transfers
            _submit_mock_transfer(&dev);

            // Interact with device - set cdc-acm specific requests
            _set_cdc_acm_specific_requests(&dev, device_address);

            // Close the device
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev, interface_index));
        }

        SECTION("Interact with device: FTDI chip dual (FS descriptor)") {

            // Define details of a device which will be opened
            const uint16_t vid = 0x403, pid = 0x6010;
            const uint8_t device_address = 2, interface_index = 0;

            // Open a device
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index, &dev_config, &dev));
            REQUIRE(dev != nullptr);

            // Interact with the device - set remote wakeup
            _set_remote_wakeup(&dev, device_address);

            // Interact with the device - submit mocked transfers
            _submit_mock_transfer(&dev);

            // Interact with device - set cdc-acm specific requests
            _set_cdc_acm_specific_requests(&dev, device_address);

            // Close the device
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev, interface_index));
        }

        SECTION("Interact with device: TTL232RG (FS descriptor)") {

            // Define details of a device which will be opened
            const uint16_t vid = 0x403, pid = 0x6001;
            const uint8_t device_address = 3, interface_index = 0;

            // Open a device
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index, &dev_config, &dev));
            REQUIRE(dev != nullptr);

            // Interact with the device - set remote wakeup
            _set_remote_wakeup(&dev, device_address);

            // Interact with the device - submit mocked transfers
            _submit_mock_transfer(&dev);

            // Interact with device - set cdc-acm specific requests
            _set_cdc_acm_specific_requests(&dev, device_address);

            // Close the device
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev, interface_index));
        }

        SECTION("Interact with device: CP210x") {

            // Define details of a device which will be opened
            const uint16_t vid = 0x10C4, pid = 0xEA60;
            const uint8_t device_address = 4, interface_index = 0;

            // Open a device
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index, &dev_config, &dev));
            REQUIRE(dev != nullptr);

            // Interact with the device - set remote wakeup
            _set_remote_wakeup(&dev, device_address);

            // Interact with the device - submit mocked transfers
            _submit_mock_transfer(&dev);

            // Interact with device - set cdc-acm specific requests
            _set_cdc_acm_specific_requests(&dev, device_address);

            // Close the device
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev, interface_index));
        }

        SECTION("Interact with device: TinyUSB serial") {

            // Define details of a device which will be opened
            const uint16_t vid = 0x303A, pid = 0x4001;
            const uint8_t device_address = 5, interface_index = 0;

            // Open a device
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index, &dev_config, &dev));
            REQUIRE(dev != nullptr);

            // Interact with the device - set remote wakeup
            _set_remote_wakeup(&dev, device_address);

            // Interact with the device - submit mocked transfers
            _submit_mock_transfer(&dev);

            // Interact with device - set cdc-acm specific requests
            _set_cdc_acm_specific_requests(&dev, device_address);

            // Close the device
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev, interface_index));
        }

        SECTION("Interact with device: TinyUSB serial dual device") {

            // Open both interfaces of the device
            const uint16_t vid = 0x303A, pid = 0x4002;
            const uint8_t device_address = 6, interface_index1 = 0, interface_index2 = 2;
            cdc_acm_dev_hdl_t dev1 = nullptr, dev2 = nullptr;

            // Open two devices
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index1, &dev_config, &dev1));
            REQUIRE(dev1 != nullptr);
            REQUIRE(ESP_OK == test_cdc_acm_host_open(device_address, vid, pid, interface_index2, &dev_config, &dev2));
            REQUIRE(dev2 != nullptr);

            // Interact with the devices - set remote wakeup
            _set_remote_wakeup(&dev1, device_address);
            _set_remote_wakeup(&dev2, device_address);

            // Interact with the device - submit mocked transfers
            _submit_mock_transfer(&dev1);
            _submit_mock_transfer(&dev2);

            // Interact with device - set cdc-acm specific requests
            _set_cdc_acm_specific_requests(&dev1, device_address);
            _set_cdc_acm_specific_requests(&dev2, device_address);

            // Close the devices
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev1, interface_index1));
            REQUIRE(ESP_OK == test_cdc_acm_host_close(&dev2, interface_index2));
        }

        // Uninstall CDC-ACM driver
        REQUIRE(ESP_OK == test_cdc_acm_host_uninstall());
    }
}

SCENARIO("TinyUSB serial")
{
    Mockusb_host_Init();
    GIVEN("CDC driver installed") {
        _add_mocked_devices();
        REQUIRE(ESP_OK == test_cdc_acm_host_install(nullptr));
        AND_GIVEN("device opened") {
            const cdc_acm_host_device_config_t dev_config = {
                .connection_timeout_ms = 1000,
                .out_buffer_size = 100,
                .in_buffer_size = 100,
                .event_cb = nullptr,
                .data_cb = nullptr,
                .user_arg = nullptr,
            };

            usb_host_device_open_Stub(usb_host_device_open_mock_callback);
            usb_host_get_device_descriptor_Stub(usb_host_get_device_descriptor_mock_callback);
            usb_host_device_close_Stub(usb_host_device_close_mock_callback);
            usb_host_get_active_config_descriptor_Stub(usb_host_get_active_config_descriptor_mock_callback);
            usb_host_device_addr_list_fill_Stub(usb_host_device_addr_list_fill_mock_callback);
            usb_host_device_info_Stub(usb_host_device_info_mock_callback);
            usb_host_transfer_alloc_Stub(usb_host_transfer_alloc_mock_callback);
            usb_host_transfer_submit_control_Stub(usb_host_transfer_submit_control_success_mock_callback);

            usb_host_interface_claim_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_transfer_submit_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_interface_claim_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_transfer_submit_ExpectAnyArgsAndReturn(ESP_OK);

            // Open a device
            cdc_acm_dev_hdl_t dev = nullptr;
            esp_err_t ret = cdc_acm_host_open(0x303A, 0x4001, 0, &dev_config, &dev);
            REQUIRE(ret == ESP_OK);

            THEN("CDC-ACM specific commands are supported and work correctly") {
                // This device is CDC compliant. The CDC-ACM subclass functions must be supported
                cdc_acm_line_coding_t line_coding = {};
                REQUIRE(ESP_OK == cdc_acm_host_line_coding_get(dev, &line_coding));
                REQUIRE(ESP_OK == cdc_acm_host_line_coding_set(dev, &line_coding));
                REQUIRE(ESP_OK == cdc_acm_host_set_control_line_state(dev, false, false));
                REQUIRE(ESP_OK == cdc_acm_host_send_break(dev, 10));
            }

            THEN("Printing device descriptors works correctly") {
                cdc_acm_host_desc_print(dev);
            }

            THEN("Functional descriptors can be obtained") {
                // Request various CDC functional descriptors
                // Following are present in the TinyUSB CDC device: Header, Call management, ACM, Union
                const cdc_header_desc_t *header_desc;
                const cdc_acm_call_desc_t *call_desc;
                const cdc_acm_acm_desc_t *acm_desc;
                const cdc_union_desc_t *union_desc;
                REQUIRE(ESP_OK == cdc_acm_host_cdc_desc_get(dev, USB_CDC_DESC_SUBTYPE_HEADER, (const usb_standard_desc_t **)&header_desc));
                REQUIRE(ESP_OK == cdc_acm_host_cdc_desc_get(dev, USB_CDC_DESC_SUBTYPE_CALL, (const usb_standard_desc_t **)&call_desc));
                REQUIRE(ESP_OK == cdc_acm_host_cdc_desc_get(dev, USB_CDC_DESC_SUBTYPE_ACM, (const usb_standard_desc_t **)&acm_desc));
                REQUIRE(ESP_OK == cdc_acm_host_cdc_desc_get(dev, USB_CDC_DESC_SUBTYPE_UNION, (const usb_standard_desc_t **)&union_desc));
                REQUIRE(nullptr != header_desc);
                REQUIRE(nullptr != call_desc);
                REQUIRE(nullptr != acm_desc);
                REQUIRE(nullptr != union_desc);
                REQUIRE(USB_CDC_DESC_SUBTYPE_HEADER == header_desc->bDescriptorSubtype);
                REQUIRE(USB_CDC_DESC_SUBTYPE_CALL == call_desc->bDescriptorSubtype);
                REQUIRE(USB_CDC_DESC_SUBTYPE_ACM == acm_desc->bDescriptorSubtype);
                REQUIRE(USB_CDC_DESC_SUBTYPE_UNION == union_desc->bDescriptorSubtype);

                // Check few errors
                REQUIRE(ESP_ERR_NOT_FOUND == cdc_acm_host_cdc_desc_get(dev, USB_CDC_DESC_SUBTYPE_OBEX, (const usb_standard_desc_t **)&header_desc));
                REQUIRE(ESP_ERR_INVALID_ARG == cdc_acm_host_cdc_desc_get(dev, USB_CDC_DESC_SUBTYPE_MAX, (const usb_standard_desc_t **)&header_desc));
                REQUIRE(ESP_ERR_INVALID_ARG == cdc_acm_host_cdc_desc_get(NULL, USB_CDC_DESC_SUBTYPE_HEADER, (const usb_standard_desc_t **)&header_desc));
            }

            usb_host_endpoint_halt_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_endpoint_flush_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_endpoint_clear_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_endpoint_halt_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_endpoint_flush_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_endpoint_clear_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_interface_release_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_interface_release_ExpectAnyArgsAndReturn(ESP_OK);
            usb_host_transfer_free_Stub(usb_host_transfer_free_mock_callback); // Free all transfers
            usb_host_device_close_ExpectAnyArgsAndReturn(ESP_OK);      // Close the device
            cdc_acm_host_close(dev);
        }

        // Uninstall CDC-ACM driver
        REQUIRE(ESP_OK == test_cdc_acm_host_uninstall());
    }
}
