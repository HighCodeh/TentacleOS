/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "soc/soc_caps.h"
#if SOC_USB_OTG_SUPPORTED

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_private/usb_phy.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "esp_intr_alloc.h"

#define APP_QUEUE_LENGTH 5

static uint8_t tx_buf[] = "HELLO";
static uint8_t tx_buf2[] = "WORLD";
static int nb_of_responses;
static int nb_of_responses2;
static bool new_dev_cb_called = false;
static bool rx_overflow = false;
static QueueHandle_t app_queue = NULL;
static StaticQueue_t s_queue_buf;
static char err_msg_buf[128];

// Event message queue
typedef enum {
    CDC_ACM_DRIVER_EVENT = 0,       // CDC-ACM Driver event group: New device connected
    CDC_ACM_DEVICE_EVENT,           // CDC-ACM Device event group: Dev disconnected, suspend/resume ..
} event_group_t;

// Test event struct used for expecting device and driver events
typedef struct {
    event_group_t event_group;                                  // Driver or Device event
    union {
        struct {
            usb_device_handle_t usb_dev_hdl;                    // New device handle
        } driver_evt;
        struct {
            cdc_acm_host_dev_event_data_t dev_event_data;       // Device event data
        } device_evt;
    };
} test_event_t;

// Static app queue storage
static uint8_t ucQueueStorage[APP_QUEUE_LENGTH * sizeof(test_event_t)];

// Default device config
static const cdc_acm_host_device_config_t default_dev_config = {
    .connection_timeout_ms = 500,
    .out_buffer_size = 64,
    .in_buffer_size = 0, // Use MPS of IN endpoint
    .event_cb = [](const cdc_acm_host_dev_event_data_t *event, void *user_ctx) -> void {
        switch (event->type)
        {
        case CDC_ACM_HOST_ERROR:
            printf("Error event %d\n", event->data.error);
            return; // Ignore error event
        case CDC_ACM_HOST_SERIAL_STATE:
            if (event->data.serial_state.bOverRun) {
                rx_overflow = true;
            }
            break;
        case CDC_ACM_HOST_NETWORK_CONNECTION:
            break;
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            printf("Disconnection event\n");
            TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(event->data.cdc_hdl));
            break;
#ifdef CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
        case CDC_ACM_HOST_DEVICE_SUSPENDED:
            printf("Device suspended event\n");
            break;
        case CDC_ACM_HOST_DEVICE_RESUMED:
            printf("Device resumed event\n");
            break;
#endif // CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
        default:
            assert(false);
        }

        const test_event_t test_event = {
            .event_group = CDC_ACM_DEVICE_EVENT,
            .device_evt = {
                .dev_event_data = *event,
            },
        };
        xQueueSend(app_queue, &test_event, 0);
    },
    .data_cb = [](const uint8_t *data, size_t data_len, void *arg) -> bool {
        printf("Data received\n");
        nb_of_responses++;
        TEST_ASSERT_EQUAL_STRING_LEN(data, arg, data_len);
        return true;
    },
    .user_arg = tx_buf,
};

// Default driver config
static const cdc_acm_host_driver_config_t driver_config_new_dev_cb = {
    .driver_task_stack_size = 4096,
    .driver_task_priority = 10,
    .xCoreID = 0,
    .new_dev_cb = [](usb_device_handle_t usb_dev) -> void {

        printf("New device connected\n");
        const test_event_t test_event = {
            .event_group = CDC_ACM_DRIVER_EVENT,
            .driver_evt = {
                .usb_dev_hdl = usb_dev,
            },
        };
        xQueueSend(app_queue, &test_event, 0);
    }
};

// Default structs for wait_for_app_event() function

static const test_event_t dev_gone_event = {
    .event_group = CDC_ACM_DEVICE_EVENT,
    .device_evt = {
        .dev_event_data = {
            .type = CDC_ACM_HOST_DEVICE_DISCONNECTED,
        },
    },
};

static const test_event_t new_dev_event = {
    .event_group = CDC_ACM_DRIVER_EVENT,
    .driver_evt = {
        .usb_dev_hdl = NULL,
    },
};

#ifdef CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
static const test_event_t suspend_event = {
    .event_group = CDC_ACM_DEVICE_EVENT,
    .device_evt = {
        .dev_event_data = {
            .type = CDC_ACM_HOST_DEVICE_SUSPENDED,
        },
    },
};

static const test_event_t resume_event = {
    .event_group = CDC_ACM_DEVICE_EVENT,
    .device_evt = {
        .dev_event_data = {
            .type = CDC_ACM_HOST_DEVICE_RESUMED,
        },
    },
};
#endif // CDC_HOST_SUSPEND_RESUME_API_SUPPORTED

// usb_host_lib_set_root_port_power is used to force toggle connection, primary developed for esp32p4
// esp32p4 is supported from IDF 5.3
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)
static usb_phy_handle_t phy_hdl = NULL;

// Force connection/disconnection using PHY
static void force_conn_state(bool connected, TickType_t delay_ticks)
{
    TEST_ASSERT_NOT_EQUAL(NULL, phy_hdl);
    if (delay_ticks > 0) {
        // Delay of 0 ticks causes a yield. So skip if delay_ticks is 0.
        vTaskDelay(delay_ticks);
    }
    ESP_ERROR_CHECK(usb_phy_action(phy_hdl, (connected) ? USB_PHY_ACTION_HOST_ALLOW_CONN : USB_PHY_ACTION_HOST_FORCE_DISCONN));
}

// Initialize the internal USB PHY to connect to the USB OTG peripheral. We manually install the USB PHY for testing
static bool install_phy(void)
{
    usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_HOST,
        .otg_speed = USB_PHY_SPEED_UNDEFINED,   // In Host mode, the speed is determined by the connected device
    };
    TEST_ASSERT_EQUAL(ESP_OK, usb_new_phy(&phy_config, &phy_hdl));
    // Return true, to skip_phy_setup during the usb_host_install()
    return true;
}

static void delete_phy(void)
{
    TEST_ASSERT_EQUAL(ESP_OK, usb_del_phy(phy_hdl)); // Tear down USB PHY
    phy_hdl = NULL;
}
#else // ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)

// Force connection/disconnection using root port power
static void force_conn_state(bool connected, TickType_t delay_ticks)
{
    if (delay_ticks > 0) {
        // Delay of 0 ticks causes a yield. So skip if delay_ticks is 0.
        vTaskDelay(delay_ticks);
    }
    ESP_ERROR_CHECK(usb_host_lib_set_root_port_power(connected));
}

static bool install_phy(void)
{
    // Return false, NOT to skip_phy_setup during the usb_host_install()
    return false;
}

static void delete_phy(void) {}
#endif // ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 3, 0)

void usb_lib_task(void *arg)
{
    const bool skip_phy_setup = install_phy();
    // Install USB Host driver. Should only be called once in entire application
    const usb_host_config_t host_config = {
        .skip_phy_setup = skip_phy_setup,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_install(&host_config));
    printf("USB Host installed\n");
    xTaskNotifyGive((TaskHandle_t)arg);

    bool has_clients = true;
    bool has_devices = false;
    while (has_clients) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            printf("Get FLAGS_NO_CLIENTS\n");
            if (ESP_OK == usb_host_device_free_all()) {
                printf("All devices marked as free, no need to wait FLAGS_ALL_FREE event\n");
                has_clients = false;
            } else {
                printf("Wait for the FLAGS_ALL_FREE\n");
                has_devices = true;
            }
        }
        if (has_devices && event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            printf("Get FLAGS_ALL_FREE\n");
            has_clients = false;
        }
#ifdef CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_AUTO_SUSPEND) {
            printf("Get AUTO_SUSPEND\n");
            TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
        }
#endif // CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
    }
    printf("No more clients and devices, uninstall USB Host library\n");

    // Clean up USB Host
    vTaskDelay(10); // Short delay to allow clients clean-up
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_uninstall());
    delete_phy();
    vTaskDelete(NULL);
}

void test_install_cdc_driver(const cdc_acm_host_driver_config_t *driver_config)
{
    // Create static app message queue
    app_queue = xQueueCreateStatic(APP_QUEUE_LENGTH, sizeof(test_event_t), &(ucQueueStorage[0]), &s_queue_buf);
    configASSERT(app_queue);

    // Create a task that will handle USB library events
    TEST_ASSERT_EQUAL(pdTRUE, xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4 * 4096, xTaskGetCurrentTaskHandle(), 10, NULL, 0));
    ulTaskNotifyTake(false, 1000);

    printf("Installing CDC-ACM driver\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_install(driver_config));
}

/**
 * @brief Wait for app event
 *
 * @param[in] expected_app_event event to expect
 * @param[in] ticks time to wait for an event
 * @param[in] file file from which the function was called
 * @param[in] line line from which the function was called
 */
static void wait_for_app_event_impl(const test_event_t *expected_app_event, TickType_t ticks, const char *file, int line);

/**
 * @brief Make sure no app event is delivered during the set amount of time
 *
 * @param[in] ticks time to check that no event is delivered
 * @param[in] file file from which the function was called
 * @param[in] line line from which the function was called
 */
static void wait_for_no_app_event_impl(TickType_t ticks, const char *file, int line);

#define wait_for_no_app_event(ticks) wait_for_no_app_event_impl((ticks), __FILE__, __LINE__)
static void wait_for_app_event_impl(const test_event_t *expected_app_event, TickType_t ticks, const char *file, int line)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(app_queue, "App queue has not been initialized");
    test_event_t app_event;
    if (pdTRUE == xQueueReceive(app_queue, &app_event, ticks)) {
        TEST_ASSERT_EQUAL_MESSAGE(expected_app_event->event_group, app_event.event_group, "Unexpected event group");
        if (app_event.event_group == CDC_ACM_DEVICE_EVENT) {
            if (expected_app_event->device_evt.dev_event_data.type != app_event.device_evt.dev_event_data.type) {
                snprintf(err_msg_buf, sizeof(err_msg_buf),
                         "Unexpected event at %s:%d\n %d expected, %d delivered\n",
                         file, line, expected_app_event->device_evt.dev_event_data.type, app_event.device_evt.dev_event_data.type);
                TEST_FAIL_MESSAGE(err_msg_buf);
            }
        } else {
            // Driver event (new device connected, nothing to check)
            return;
        }
    } else {
        snprintf(err_msg_buf, sizeof(err_msg_buf), "App event not generated on time at %s:%d\n", file, line);
        TEST_FAIL_MESSAGE(err_msg_buf);
    }
}

#define wait_for_app_event(expected_app_event, ticks) wait_for_app_event_impl((expected_app_event), (ticks), __FILE__, __LINE__)
static void wait_for_no_app_event_impl(TickType_t ticks, const char *file, int line)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(app_queue, "App queue has not been initialized");
    test_event_t app_event;
    if (pdFALSE == xQueueReceive(app_queue, &app_event, ticks)) {
        return;
    } else {
        if (app_event.event_group == CDC_ACM_DEVICE_EVENT) {
            snprintf(err_msg_buf, sizeof(err_msg_buf),
                     "Expecting NO event, but a device event %d delivered at %s:%d\n",
                     app_event.device_evt.dev_event_data.type, file, line);
        } else {
            snprintf(err_msg_buf, sizeof(err_msg_buf), "Unexpected driver event\n");
        }
        TEST_FAIL_MESSAGE(err_msg_buf);
    }
}

static void new_dev_cb(usb_device_handle_t usb_dev)
{
    new_dev_cb_called = true;
    const usb_config_desc_t *config_desc;
    const usb_device_desc_t *device_desc;

    // Get descriptors
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_get_device_descriptor(usb_dev, &device_desc));
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_get_active_config_descriptor(usb_dev, &config_desc));

    printf("New device connected. VID = 0x%04X PID = %04X\n", device_desc->idVendor, device_desc->idProduct);
}

/* Basic test to check CDC communication:
 * open/read/write/close device
 * CDC-ACM specific commands: set/get_line_coding, set_control_line_state */
#define NUM_ITERATIONS 10
TEST_CASE("read_write", "[cdc_acm]")
{
    nb_of_responses = 0;
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    // Use default device config
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed

    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

TEST_CASE("cdc_specific_commands", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 500,
        .out_buffer_size = 64
    };

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    printf("Sending CDC specific commands\n");
    cdc_acm_line_coding_t line_coding_get;
    const cdc_acm_line_coding_t line_coding_set = {
        .dwDTERate = 9600,
        .bCharFormat = 1,
        .bParityType = 1,
        .bDataBits = 7,
    };
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_line_coding_set(cdc_dev, &line_coding_set));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_line_coding_get(cdc_dev, &line_coding_get));
    TEST_ASSERT_EQUAL_MEMORY(&line_coding_set, &line_coding_get, sizeof(cdc_acm_line_coding_t));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_set_control_line_state(cdc_dev, true, false));

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/* Test communication with multiple CDC-ACM devices from one thread */
TEST_CASE("multiple_devices", "[cdc_acm]")
{
    nb_of_responses = 0;
    nb_of_responses2 = 0;

    test_install_cdc_driver(NULL);

    printf("Opening 2 CDC-ACM devices\n");
    cdc_acm_dev_hdl_t cdc_dev1, cdc_dev2;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev1)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    dev_config.data_cb = [](const uint8_t *data, size_t data_len, void *arg) -> bool {
        printf("Data received 2\n");
        nb_of_responses2++;
        TEST_ASSERT_EQUAL_STRING_LEN(data, arg, data_len);
        return true;
    };
    dev_config.user_arg = tx_buf2;
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 2, &dev_config, &cdc_dev2)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev1);
    TEST_ASSERT_NOT_NULL(cdc_dev2);

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev1, tx_buf, sizeof(tx_buf), 1000));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev2, tx_buf2, sizeof(tx_buf2), 1000));

    vTaskDelay(10); // Wait for RX callbacks

    // We sent two messages, should get two responses
    TEST_ASSERT_EQUAL(1, nb_of_responses);
    TEST_ASSERT_EQUAL(1, nb_of_responses2);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev1));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev2));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

#define MULTIPLE_THREADS_TRANSFERS_NUM 5
#define MULTIPLE_THREADS_TASKS_NUM 4
static void tx_task(void *arg)
{
    cdc_acm_dev_hdl_t cdc_dev = (cdc_acm_dev_hdl_t) arg;
    // Send multiple transfers to make sure that some of them will run at the same time
    for (int i = 0; i < MULTIPLE_THREADS_TRANSFERS_NUM; i++) {
        // BULK endpoints
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));

        // CTRL endpoints
        cdc_acm_line_coding_t line_coding_get;
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_line_coding_get(cdc_dev, &line_coding_get));
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_set_control_line_state(cdc_dev, true, false));
    }
    vTaskDelete(NULL);
}

/**
 * @brief Multiple threads test
 *
 * In this test, one CDC device is accessed from multiple threads.
 * It has to be opened/closed just once, though.
 */
TEST_CASE("multiple_threads", "[cdc_acm]")
{
    nb_of_responses = 0;
    cdc_acm_dev_hdl_t cdc_dev;
    test_install_cdc_driver(NULL);

    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    // Create multiple tasks that will try to access cdc_dev
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL(pdTRUE, xTaskCreate(tx_task, "CDC TX", 4096, cdc_dev, i + 3, NULL));
    }

    // Wait until all tasks finish
    vTaskDelay(pdMS_TO_TICKS(500));
    TEST_ASSERT_EQUAL(MULTIPLE_THREADS_TASKS_NUM * MULTIPLE_THREADS_TRANSFERS_NUM, nb_of_responses);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/* Test CDC driver reaction to USB device sudden disconnection */
TEST_CASE("sudden_disconnection", "[cdc_acm]")
{
    test_install_cdc_driver(NULL);

    cdc_acm_dev_hdl_t cdc_dev;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    dev_config.user_arg = xTaskGetCurrentTaskHandle();
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev));
    TEST_ASSERT_NOT_NULL(cdc_dev);

    force_conn_state(false, pdMS_TO_TICKS(10));                        // Simulate device disconnection
    wait_for_app_event(&dev_gone_event, 100);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief CDC-ACM error handling test
 *
 * There are multiple erroneous scenarios checked in this test:
 *
 * -# Install CDC-ACM driver without USB Host
 * -# Open device without installed driver
 * -# Uninstall driver before installing it
 * -# Open non-existent device
 * -# Open the same device twice
 * -# Uninstall driver with open devices
 * -# Send unsupported CDC request
 * -# Write to read-only device
 */
TEST_CASE("error_handling", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    // Install CDC-ACM driver without USB Host
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, cdc_acm_host_install(NULL));

    // Open device without installed driver
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev));

    // Uninstall driver before installing it
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, cdc_acm_host_uninstall());

    // Properly install USB and CDC drivers
    test_install_cdc_driver(NULL);

    // Open non-existent device
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, cdc_acm_host_open(0x303A, 0x1234, 0, &dev_config, &cdc_dev)); // 0x303A:0x1234 this device is not connected to USB Host
    TEST_ASSERT_NULL(cdc_dev);

    // Open regular device
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev));
    TEST_ASSERT_NOT_NULL(cdc_dev);

    // Open one CDC-ACM device twice
    cdc_acm_dev_hdl_t cdc_dev_test;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev_test));
    TEST_ASSERT_NULL(cdc_dev_test);

    // Uninstall driver with open devices
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, cdc_acm_host_uninstall());

    // Send NULL data
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, cdc_acm_host_data_tx_blocking(cdc_dev, NULL, 10, 1000));

    // Change mode to read-only and try to write to it
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    dev_config.out_buffer_size = 0; // Read-only device
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev));
    TEST_ASSERT_NOT_NULL(cdc_dev);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));

    // Send unsupported CDC request (TinyUSB accepts SendBreak command, even though it doesn't support it)
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_send_break(cdc_dev, 100));

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

TEST_CASE("custom_command", "[cdc_acm]")
{
    test_install_cdc_driver(NULL);

    // Open device with only CTRL endpoint (endpoint no 0)
    cdc_acm_dev_hdl_t cdc_dev;
    cdc_acm_host_device_config_t dev_config = default_dev_config;
    dev_config.out_buffer_size = 0;
    dev_config.in_buffer_size = 0;
    dev_config.data_cb = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev));
    TEST_ASSERT_NOT_NULL(cdc_dev);

    // Corresponds to command: Set Control Line State, DTR on, RTS off
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_send_custom_request(cdc_dev, 0x21, 34, 1, 0, 0, NULL));

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

TEST_CASE("new_device_connection_1", "[cdc_acm]")
{
    // Create a task that will handle USB library events
    TEST_ASSERT_EQUAL(pdTRUE, xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4 * 4096, xTaskGetCurrentTaskHandle(), 10, NULL, 0));
    ulTaskNotifyTake(false, 1000);

    // Option 1: Register callback during driver install
    printf("Installing CDC-ACM driver\n");
    cdc_acm_host_driver_config_t driver_config = {
        .driver_task_stack_size = 2048,
        .driver_task_priority = 11,
        .xCoreID = 0,
        .new_dev_cb = new_dev_cb,
    };
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_install(&driver_config));

    vTaskDelay(50);
    TEST_ASSERT_TRUE_MESSAGE(new_dev_cb_called, "New device callback was not called\n");
    new_dev_cb_called = false;

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

TEST_CASE("new_device_connection_2", "[cdc_acm]")
{
    test_install_cdc_driver(NULL);

    // Option 2: Register callback after driver install
    force_conn_state(false, 50);
    vTaskDelay(50);

    // Make sure no disconnection event is delivered, since the device has not been opened by any client
    wait_for_no_app_event(50);

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_register_new_dev_callback(new_dev_cb));
    force_conn_state(true, 0);
    vTaskDelay(50);
    TEST_ASSERT_TRUE_MESSAGE(new_dev_cb_called, "New device callback was not called\n");
    new_dev_cb_called = false;

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief CDC-ACM RX Buffer Test
 *
 * This test case verifies the behavior of the CDC-ACM driver when handling RX buffers.
 * The RX callback can return 'false' to signal that the received data was not processed, which means that next RX data should be appended to the current buffer.
 *
 * In case the RX is full, the driver should notify the application.
 *
 * -# Install the CDC-ACM driver
 * -# Open a device with in_buffer_size=512
 * -# Receive >512 bytes of data in normal operation mode -> no error
 * -# Receive >512 bytes of data in 'not processed' mode -> error
 */
TEST_CASE("rx_buffer", "[cdc_acm]")
{
    test_install_cdc_driver(NULL);
    bool process_data = true; // This variable will determine return value of data_cb

    cdc_acm_dev_hdl_t cdc_dev;
    cdc_acm_host_device_config_t dev_config = default_dev_config;
    dev_config.in_buffer_size = 512;
    dev_config.data_cb = [](const uint8_t *data, size_t data_len, void *arg) -> bool {
        bool *process_data = (bool *)arg;
        return *process_data;
    };
    dev_config.user_arg = &process_data;

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev));
    TEST_ASSERT_NOT_NULL(cdc_dev);

    // 1. Send > in_buffer_size bytes of data in normal operation: Expect no error
    // Send 63 bytes of data. 63 because we do not want to send MPS sized packet for Full Speed devices
    uint8_t tx_data[63] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
    for (int i = 0; i < 10; i++) { // 630 (bytes sent) > 512 (in_buffer_size)
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_data, sizeof(tx_data), 1000));
        vTaskDelay(5);
    }
    TEST_ASSERT_FALSE_MESSAGE(rx_overflow, "RX overflowed");
    rx_overflow = false;

    // 2. Send < (in_buffer_size - IN_MPS) bytes of data in 'not processed' mode: Expect no error
    process_data = false;
    for (int i = 0; i < 7; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_data, sizeof(tx_data), 1000));
        vTaskDelay(5);
    }
    TEST_ASSERT_FALSE_MESSAGE(rx_overflow, "RX overflowed");
    rx_overflow = false;

    // 3. Send >= (in_buffer_size - IN_MPS) bytes of data in 'not processed' mode: Expect error
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_data, sizeof(tx_data), 1000));
    vTaskDelay(5);

#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE
    TEST_ASSERT_FALSE_MESSAGE(rx_overflow, "RX overflow");
#else
    TEST_ASSERT_TRUE_MESSAGE(rx_overflow, "RX did not overflow");
#endif
    rx_overflow = false;

    // 4. Send more data to the EP: Expect no error
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_data, sizeof(tx_data), 1000));
    vTaskDelay(5);
    TEST_ASSERT_FALSE_MESSAGE(rx_overflow, "RX overflowed");
    rx_overflow = false;

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Closing procedure test
 *
 * -# Close already closed device
 */
TEST_CASE("closing", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    printf("Closing already closed device \n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0) // This fix is only available in usb managed component now
/**
 * @brief Test: Closing from high priority task
 *
 * -# Install CDC-ACM driver with low priority (0)
 * -# Open and close device from higher priority task (app_main has priority 1)
 */
TEST_CASE("closing_high_priority", "[cdc_acm]")
{
    // Create a task that will handle USB library events
    TEST_ASSERT_EQUAL(pdTRUE, xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4 * 4096, xTaskGetCurrentTaskHandle(), 10, NULL, 0));
    ulTaskNotifyTake(false, 1000);

    printf("Installing CDC-ACM driver with minimum priority\n");
    static const cdc_acm_host_driver_config_t cdc_acm_driver_config = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 0, // Minimum priority
        .xCoreID = 0,
        .new_dev_cb = NULL,
    };
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_install(&cdc_acm_driver_config));

    cdc_acm_dev_hdl_t cdc_dev = NULL;
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}
#endif // ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)

/* Basic test to check CDC driver reaction to TX timeout */
/* Temporary disabling the test, as it keeps failing in the CI IDF-14863 */
TEST_CASE("tx_timeout", "[cdc_acm][ignore]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    // Use default device config
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    // TX some data with timeout_ms=0. This will cause a timeout
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 0));
    vTaskDelay(10); // Wait before trying new TX

    // TX some data again with greater timeout. This will check normal operation
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    vTaskDelay(10);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

#ifdef CDC_HOST_SUSPEND_RESUME_API_SUPPORTED

/**
 * @brief Test: Basic suspend/resume cycle with multiple pseudo-devices
 *
 * #. open/read/write the device
 * #. suspend/resume the device - check that the client events are delivered
 * #. read/write/close the device
 */
TEST_CASE("suspend_resume_multiple_devs", "[cdc_acm]")
{
    nb_of_responses = 0;
    nb_of_responses2 = 0;

    test_install_cdc_driver(NULL);

    cdc_acm_dev_hdl_t cdc_dev1 = NULL, cdc_dev2 = NULL;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev1)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    dev_config.data_cb = [](const uint8_t *data, size_t data_len, void *arg) -> bool {
        printf("Data received 2\n");
        nb_of_responses2++;
        TEST_ASSERT_EQUAL_STRING_LEN(data, arg, data_len);
        return true;
    };
    dev_config.user_arg = tx_buf2;
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 2, &dev_config, &cdc_dev2)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev1);
    TEST_ASSERT_NOT_NULL(cdc_dev2);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev1, tx_buf, sizeof(tx_buf), 1000));
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev2, tx_buf2, sizeof(tx_buf2), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed

    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses);
    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses2);
    nb_of_responses = 0;
    nb_of_responses2 = 0;

    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);
    wait_for_app_event(&suspend_event, 100);

    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
    wait_for_app_event(&resume_event, 100);
    wait_for_app_event(&resume_event, 100);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev1, tx_buf, sizeof(tx_buf), 1000));
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev2, tx_buf2, sizeof(tx_buf2), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed

    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses);
    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses2);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev1));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev2));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

#define TEST_CDC_ACM_SUSPEND_TIMER_INTERVAL_MS   1000
#define TEST_CDC_ACM_SUSPEND_TIMER_MARGIN_MS     50
/**
 * @brief Test: Automatic suspend timer
 *
 * #. open the device, set One-Shot auto suspend timer, expect one USB_HOST_CLIENT_EVENT_DEV_SUSPENDED event
 * #. Set Periodic auto suspend timer, expect multiple USB_HOST_CLIENT_EVENT_DEV_SUSPENDED events
 * #. Disable Periodic auto suspend timer, expect no event
 * #. disconnect the device
 * #. cleanup
 */
TEST_CASE("automatic_suspend_timer", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    // Use default device config
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    // Set One-Shot auto suspend timer
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_set_auto_suspend(USB_HOST_LIB_AUTO_SUSPEND_ONE_SHOT, TEST_CDC_ACM_SUSPEND_TIMER_INTERVAL_MS));
    wait_for_app_event(&suspend_event, pdMS_TO_TICKS(TEST_CDC_ACM_SUSPEND_TIMER_INTERVAL_MS + TEST_CDC_ACM_SUSPEND_TIMER_MARGIN_MS));

    // Manually resume the root port and expect the resumed event
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
    wait_for_app_event(&resume_event, 20);

    // Make sure no event is delivered, since the timer is a One-Shot timer
    wait_for_no_app_event(pdMS_TO_TICKS(TEST_CDC_ACM_SUSPEND_TIMER_INTERVAL_MS * 2));

    // Set Periodic auto suspend timer
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_set_auto_suspend(USB_HOST_LIB_AUTO_SUSPEND_PERIODIC, TEST_CDC_ACM_SUSPEND_TIMER_INTERVAL_MS));

    for (int i = 0; i < 3; i++) {
        // Expect suspended event from auto suspend timer
        wait_for_app_event(&suspend_event, pdMS_TO_TICKS(TEST_CDC_ACM_SUSPEND_TIMER_INTERVAL_MS + TEST_CDC_ACM_SUSPEND_TIMER_MARGIN_MS));

        // Resume the root port manually and expect the resume event
        TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
        wait_for_app_event(&resume_event, 20);

        // Verify data transmit on resumed device
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }

    // Let the transfer to finish before disabling the timer, to make sure that the timer won't interfere with the ongoing transfer
    vTaskDelay(10);

    // Disable the Periodic auto suspend timer
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_set_auto_suspend(USB_HOST_LIB_AUTO_SUSPEND_PERIODIC, 0));
    // Make sure no event is delivered
    wait_for_no_app_event(pdMS_TO_TICKS(TEST_CDC_ACM_SUSPEND_TIMER_INTERVAL_MS * 2));

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: Sudden disconnect, while in suspended state
 *
 * #. open/suspend the device
 * #. disconnect the device
 * #. cleanup
 */
TEST_CASE("suspend_resume_sudden_disconnect", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    // Use default device config
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);

    force_conn_state(false, pdMS_TO_TICKS(10));                        // Simulate device disconnection
    wait_for_app_event(&dev_gone_event, 100);

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

static void suspend_tx_task(void *arg)
{
    cdc_acm_dev_hdl_t cdc_dev = (cdc_acm_dev_hdl_t) arg;
    // Send multiple transfers to make sure that some of them will run at the same time
    for (int i = 0; i < MULTIPLE_THREADS_TRANSFERS_NUM; i++) {
        // We are expecting either
        //  - ESP_OK: Transfer was submitted or deferred
        //  - ESP_ERR_INVALID_STATE: Transfer can't be submitted or deferred, when the root port is in suspending state
        //  - ESP_ERR_INVALID_RESPONSE: Transfer was submitted, but not finished,
        //                              EP halt and flush (part of suspend procedure) caused the ongoing transfer to fail with this error code.

        // BULK endpoints
        esp_err_t ret;
        ret = cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 2000);
        TEST_ASSERT_MESSAGE(
            ret == ESP_OK || ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_INVALID_RESPONSE,
            "Unexpected return value from transfer submit");

        // CTRL endpoints
        cdc_acm_line_coding_t line_coding_get;
        ret = cdc_acm_host_line_coding_get(cdc_dev, &line_coding_get);
        TEST_ASSERT_MESSAGE(
            ret == ESP_OK || ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_INVALID_RESPONSE,
            "Unexpected return value from transfer submit");

        ret = cdc_acm_host_set_control_line_state(cdc_dev, true, true);
        TEST_ASSERT_MESSAGE(
            ret == ESP_OK || ret == ESP_ERR_INVALID_STATE || ret == ESP_ERR_INVALID_RESPONSE,
            "Unexpected return value from transfer submit");
    }
    vTaskDelete(NULL);
}

/**
 * @brief Initiate auto suspend from multiple threads
 *
 * open device and send transfer from multiple tasks
 * immediately suspend the root port
 * expect both SUSPEND and RESUME events (resume event, because of auto-resume by transfer submit)
 * Expect transfer either to pass, or to fail, due to root being int suspending state
 * close the device, cleanup
 */
TEST_CASE("auto_suspend_multiple_threads", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev;
    test_install_cdc_driver(NULL);

    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    // Create multiple tasks that will try to access cdc_dev
    for (int i = 0; i < MULTIPLE_THREADS_TASKS_NUM; i++) {
        TEST_ASSERT_EQUAL(pdTRUE, xTaskCreate(suspend_tx_task, "CDC TX", 4096, cdc_dev, i + 3, NULL));
    }
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    // Expect suspend event
    wait_for_app_event(&suspend_event, 100);
    // As there are transfer being sent from other tasks, the root port will be automatically resumed.
    // Expect resume event
    wait_for_app_event(&resume_event, 1000);

    // Wait for all transfer to finish
    vTaskDelay(100);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: Device close, while in suspended state
 *
 * #. open/suspend the device
 * #. close the device
 * #. fail to open the still suspended device
 * #. cleanup
 */
TEST_CASE("device_close_while_suspended", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    // Use default device config
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);

    // Close the suspended device
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    // Try to open the still suspended device
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)

    // Cleanup
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: Device open, while in suspended state
 *
 * #. suspend the device
 * #. fail to opend the suspended device
 * #. resume/opend the device
 * #. cleanup
 */
TEST_CASE("device_open_while_suspended", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;
    test_install_cdc_driver(NULL);
    vTaskDelay(100);                        // Some time to enumerate the device

    // Suspend the root port, but do not expect any event, since the device was never opened
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_no_app_event(100);

    // Use default device config
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NULL(cdc_dev);

    // Resume the device
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
    vTaskDelay(100);

    // Open the resumed device
    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: resuming the device by submitting a transfer
 *
 * #. open/suspend the device
 * #. submit a non-ctrl transfer, expect the device to resume
 * #. submit a ctrl transfer, expect the device to resume
 * #. cleanup
 */
TEST_CASE("resume_by_transfer_submit", "[cdc_acm]")
{
    nb_of_responses = 0;
    cdc_acm_dev_hdl_t cdc_dev = NULL;

    test_install_cdc_driver(NULL);

    // Use default device config
    const cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);

    // BULK endpoints
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    wait_for_app_event(&resume_event, 1000);
    TEST_ASSERT_EQUAL(nb_of_responses, 1);

    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);

    // CTRL endpoints
    cdc_acm_line_coding_t line_coding_get;
    const cdc_acm_line_coding_t line_coding_set = {
        .dwDTERate = 9600,
        .bCharFormat = 1,
        .bParityType = 1,
        .bDataBits = 7,
    };
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_line_coding_set(cdc_dev, &line_coding_set));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_line_coding_get(cdc_dev, &line_coding_get));
    TEST_ASSERT_EQUAL_MEMORY(&line_coding_set, &line_coding_get, sizeof(cdc_acm_line_coding_t));
    wait_for_app_event(&resume_event, 1000);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: Sudden disconnect (by device) during suspend sequence
 *
 * #. open the device and suspend the root port
 * #. device registers the suspend suspend event and disconnects the port before the host finishes the suspend procedure
 * #. host expects device disconnection event
 * #. device connects the port back again
 * #. host registers the new device event, enumerates, opens the device and tests it functionality
 * #. cleanup
 */
TEST_CASE("device_suspend_sudden_disconnect", "[host_suspend_dconn]")
{
    nb_of_responses = 0;

    test_install_cdc_driver(&driver_config_new_dev_cb);

    cdc_acm_dev_hdl_t cdc_dev = NULL;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    wait_for_app_event(&new_dev_event, 100);
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    printf("Suspending the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());

    // The device has started registered suspend event and suddenly disconnected the port

    // Expect device disconnect event and new device event
    wait_for_app_event(&dev_gone_event, pdMS_TO_TICKS(5000));
    wait_for_app_event(&new_dev_event, pdMS_TO_TICKS(5000));

    // Open the device again and verify correct functionality
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: Sudden disconnect (by device) during resume sequence
 *
 * #. open the device, suspend the root port, expect suspend event, resume the root port
 * #. device registers the resume suspend event and disconnects the port before the host finishes the resume procedure
 * #. host expects device disconnection event
 * #. device connects the port back again
 * #. host registers the new device event, enumerates, opens the device and tests it functionality
 * #. cleanup
 */
TEST_CASE("device_resume_sudden_disconnect", "[host_resume_dconn]")
{
    nb_of_responses = 0;

    test_install_cdc_driver(&driver_config_new_dev_cb);

    cdc_acm_dev_hdl_t cdc_dev = NULL;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    wait_for_app_event(&new_dev_event, 100);
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    printf("Suspending the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);

    // Stay suspended for a while
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("Resuming the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());

    // The device has registered resume event and suddenly disconnected the port

    // Expect device disconnect event and new device event
    wait_for_app_event(&dev_gone_event, pdMS_TO_TICKS(5000));
    wait_for_app_event(&new_dev_event, pdMS_TO_TICKS(5000));

    // Open the device again and verify correct functionality
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

#endif // CDC_HOST_SUSPEND_RESUME_API_SUPPORTED

#ifdef CDC_HOST_REMOTE_WAKE_SUPPORTED
/**
 * @brief Test: device remote wakeup
 *
 * #. open the device and test it's functionality
 * #. suspend the root port, expect the suspend event
 * #. device registers the suspend event and starts remote wakeup sequence
 * #. host registers the remote wakeup sequence and expect resume event
 * #. cleanup
 */
TEST_CASE("device_remote_wakeup", "[host_remote_wake]")
{
    nb_of_responses = 0;
    test_install_cdc_driver(NULL);

    cdc_acm_dev_hdl_t cdc_dev = NULL;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed
    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses);

    // Enable remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_enable_remote_wakeup(cdc_dev, true));

    printf("Suspending the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);

    // Device has generated remote wakeup

    // Expect resume event
    wait_for_app_event(&resume_event, pdMS_TO_TICKS(5000));

    nb_of_responses = 0;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed
    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses);

    // Clean-up
    vTaskDelay(100);
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: device remote wakeup whit multiple interfaces
 *
 * #. open 2 pseudo devices, enable remote wakeup by dev1, disable remote wakeup by dev2
 * #. suspend the root port, expect no remote wakeup event
 * #. resume the root port, disable remote wakeup by dev1 (already disabled), enable remote wakeup by dev2
 * #. suspend the root port, expect remote wakeup from from device
 * #. cleanup
 */
TEST_CASE("device_remote_wakeup_multiple_interfaces", "[host_remote_wake]")
{
    test_install_cdc_driver(NULL);

    cdc_acm_dev_hdl_t cdc_dev1 = NULL, cdc_dev2 = NULL;
    cdc_acm_host_device_config_t dev1_config = default_dev_config;
    cdc_acm_host_device_config_t dev2_config = default_dev_config;

    printf("Opening CDC-ACM devices\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev1_config, &cdc_dev1)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 2, &dev2_config, &cdc_dev2)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev1);
    TEST_ASSERT_NOT_NULL(cdc_dev2);

    // Enable remote wakeup by cdc_dev1 and disable it by cdc_dev2
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_enable_remote_wakeup(cdc_dev1, true));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_enable_remote_wakeup(cdc_dev2, false));

    // Suspend the device and expect 2 suspend events
    printf("Suspending the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);
    wait_for_app_event(&suspend_event, 100);

    // Remote wakeup was first enabled by cdc_dev1, then disabled by cdc_dev2
    // No remote wakeup event shall be generated from the device

    // Expect NO event
    wait_for_no_app_event(pdMS_TO_TICKS(5000));

    printf("Resume the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_resume());
    wait_for_app_event(&resume_event, 100);
    wait_for_app_event(&resume_event, 100);

    // Disable remote wakeup by cdc_dev1 (should be already disabled) and enable it by cdc_dev2
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_enable_remote_wakeup(cdc_dev1, false));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_enable_remote_wakeup(cdc_dev2, true));
    // Try to enable remote wakeup again
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_enable_remote_wakeup(cdc_dev2, true));

    // Suspend the device and expect 2 suspend events
    printf("Suspending the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);
    wait_for_app_event(&suspend_event, 100);

    // Remote wakeup was first disabled (not set) by cdc_dev1, then enabled by cdc_dev2
    // A remote wakeup event shall be generated from the device

    // Expect 2 resume events (Remote wakeup)
    wait_for_app_event(&resume_event, 500);
    wait_for_app_event(&resume_event, 500);

    // Clean-up
    vTaskDelay(100);
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev1));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev2));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

/**
 * @brief Test: device remote wakeup with sudden disconnect
 *
 * #. open the device and test it's functionality
 * #. suspend the root port, expect the suspend event
 * #. device registers the suspend event and starts remote wakeup sequence followed by port disconnection
 * #. host expects only disconnection event
 * #. device connect the root port back
 * #. host expects new device event, opens the device and test it's functionality
 * #. cleanup
 */
TEST_CASE("device_remote_wakeup_sudden_disconnect", "[host_remote_wake_dconn]")
{
    nb_of_responses = 0;
    test_install_cdc_driver(&driver_config_new_dev_cb);

    cdc_acm_dev_hdl_t cdc_dev = NULL;
    cdc_acm_host_device_config_t dev_config = default_dev_config;

    printf("Opening CDC-ACM device\n");
    wait_for_app_event(&new_dev_event, 100);
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed
    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses);

    // Enable remote wakeup
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_enable_remote_wakeup(cdc_dev, true));

    printf("Suspending the root port\n");
    TEST_ASSERT_EQUAL(ESP_OK, usb_host_lib_root_port_suspend());
    wait_for_app_event(&suspend_event, 100);

    // The device has started signalizing remote wakeup and suddenly disconnected the port

    // Expect device disconnect event and new device event
    wait_for_app_event(&dev_gone_event, pdMS_TO_TICKS(5000));
    wait_for_app_event(&new_dev_event, pdMS_TO_TICKS(5000));

    // Open the device again and verify correct functionality
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);

    nb_of_responses = 0;
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, tx_buf, sizeof(tx_buf), 1000));
    }
    vTaskDelay(10); // Wait until responses are processed
    TEST_ASSERT_EQUAL(NUM_ITERATIONS, nb_of_responses);

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}
#endif // CDC_HOST_REMOTE_WAKE_SUPPORTED

/**
 * @brief Test sending a large data buffer over CDC-ACM
 *
 */
size_t bytes_received = 0;
TEST_CASE("large_tx", "[cdc_acm]")
{
    cdc_acm_dev_hdl_t cdc_dev = NULL;
    test_install_cdc_driver(NULL);

    // Create a large data buffer
    bytes_received = 0;
    size_t large_tx_size = 10 * 1024; // 10 KB
    uint8_t *large_tx_buf = (uint8_t *)malloc(large_tx_size);
    TEST_ASSERT_NOT_NULL(large_tx_buf);
    for (size_t i = 0; i < large_tx_size; i++) {
        large_tx_buf[i] = i % 256;
    }

    // Use default device config
    cdc_acm_host_device_config_t dev_config = default_dev_config;
    dev_config.data_cb = [](const uint8_t *data, size_t data_len, void *arg) -> bool {
        if (data_len > 0)   // Do not process Zero-length packets
        {
            TEST_ASSERT_NOT_NULL(data);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(data, (uint8_t *)arg + bytes_received, data_len);
            bytes_received += data_len;
        }
        return true;
    };
    dev_config.user_arg = large_tx_buf;

    printf("Opening CDC-ACM device\n");
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_open(0x303A, 0x4002, 0, &dev_config, &cdc_dev)); // 0x303A:0x4002 (TinyUSB Dual CDC device)
    TEST_ASSERT_NOT_NULL(cdc_dev);
    vTaskDelay(10);

    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_data_tx_blocking(cdc_dev, large_tx_buf, large_tx_size, 5000));
    vTaskDelay(100); // Wait until responses are processed
    TEST_ASSERT_EQUAL(large_tx_size, bytes_received);

    free(large_tx_buf);

    // Clean-up
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_close(cdc_dev));
    TEST_ASSERT_EQUAL(ESP_OK, cdc_acm_host_uninstall());
    vTaskDelay(20); // Short delay to allow task to be cleaned up
}

#endif // SOC_USB_OTG_SUPPORTED
