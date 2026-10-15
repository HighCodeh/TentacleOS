/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "sdkconfig.h"
#include "unity.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define DEV_EVT_QUEUE_LENGTH                5

static QueueHandle_t dev_evt_queue = NULL;
static StaticQueue_t s_queue_buf;
static uint8_t ucQueueStorage[DEV_EVT_QUEUE_LENGTH * sizeof(tinyusb_event_t)];

const char *CDC_DEV_TAG = "CDC device";
static uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
static void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    size_t rx_size = 0;
    /* read and write back */
    ESP_ERROR_CHECK(tinyusb_cdcacm_read(itf, buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size));
    tinyusb_cdcacm_write_queue(itf, buf, rx_size);
    tinyusb_cdcacm_write_flush(itf, 0);
}

static const tusb_desc_device_t cdc_device_descriptor = {
    .bLength = sizeof(cdc_device_descriptor),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = TINYUSB_ESPRESSIF_VID,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static const uint16_t cdc_desc_config_len = TUD_CONFIG_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN;
static const uint8_t cdc_fs_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, cdc_desc_config_len, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_CDC_DESCRIPTOR(2, 4, 0x83, 8, 0x04, 0x84, 64),
};

#if (TUD_OPT_HIGH_SPEED)
static const uint8_t cdc_hs_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 4, 0, cdc_desc_config_len, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 512),
    TUD_CDC_DESCRIPTOR(2, 4, 0x83, 8, 0x04, 0x84, 512),
};

static const tusb_desc_device_qualifier_t device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0
};

#endif // TUD_OPT_HIGH_SPEED

/**
 * @brief TinyUSB device events handler
 *
 * @param[in] event Device event
 * @param[in] arg User arguments
 */
static void device_event_handler(tinyusb_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        ESP_LOGI(CDC_DEV_TAG, "TinyUSB event: Device attached");
        break;
    case TINYUSB_EVENT_DETACHED:
        ESP_LOGI(CDC_DEV_TAG, "TinyUSB event: Device detached");
        break;
    case TINYUSB_EVENT_SUSPENDED:
        ESP_LOGI(CDC_DEV_TAG, "TinyUSB event: Device suspended with remote wakeup %s",
                 ((event->suspended.remote_wakeup) ? ("enabled") : ("disabled")));
        break;
    case TINYUSB_EVENT_RESUMED:
        ESP_LOGI(CDC_DEV_TAG, "TinyUSB event: Device resumed");
        break;
    default:
        break;
    }

    xQueueSend(dev_evt_queue, event, 0);
}

static void cdc_acm_mock_device_run(void)
{
    // Create static app message queue
    dev_evt_queue = xQueueCreateStatic(DEV_EVT_QUEUE_LENGTH, sizeof(tinyusb_event_t), &(ucQueueStorage[0]), &s_queue_buf);
    configASSERT(dev_evt_queue);

    ESP_LOGI(CDC_DEV_TAG, "Initialization");
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(device_event_handler);
    tusb_cfg.descriptor.device = &cdc_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = cdc_fs_desc_configuration;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.qualifier = &device_qualifier;
    tusb_cfg.descriptor.high_speed_config = cdc_hs_desc_configuration;
#endif // TUD_OPT_HIGH_SPEED

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t amc_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &tinyusb_cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };

    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&amc_cfg));
#if (CONFIG_TINYUSB_CDC_COUNT > 1)
    amc_cfg.cdc_port = TINYUSB_CDC_ACM_1;
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&amc_cfg));
#endif

    printf("USB initialization DONE\n");
}

/* Following test cases implement dual CDC-ACM USB device that can be used as mock device for CDC-ACM Host tests */

/**
 * @brief Run CDC-ACM Device with 2 interfaces in a loopback mode
 */
TEST_CASE("mock_dev_app_dual_iface", "[cdc_acm_mock_dev][dual_iface][ignore]")
{
    cdc_acm_mock_device_run();

    while (1) {
        vTaskDelay(10);
    }
}

/**
 * @brief Run CDC-ACM Device with 2 interfaces in a loopback mode
 *
 * Device performs sudden disconnect followed by a connect upon receiving Suspend callback
 */
TEST_CASE("mock_dev_app_suspend_dconn", "[cdc_acm_mock_dev][suspend_dconn][ignore]")
{
    cdc_acm_mock_device_run();
    tinyusb_event_id_t mask_event = UINT32_MAX;

    while (1) {
        tinyusb_event_t dev_event;
        if (xQueueReceive(dev_evt_queue, &dev_event, portMAX_DELAY)) {

            if (mask_event == dev_event.id) {
                ESP_LOGI(CDC_DEV_TAG, "Event %d masked", dev_event.id);
                mask_event = UINT32_MAX;    // Refresh masked event, allowing the device test app to handle further events without hard reset
                continue;
            }

            switch (dev_event.id) {
            case TINYUSB_EVENT_SUSPENDED:
                TEST_ASSERT(tud_disconnect());
                ESP_LOGI(CDC_DEV_TAG, "Triggering disconnect");

                // Stay disconnected for a while and trigger connect
                vTaskDelay(pdMS_TO_TICKS(1000));

                ESP_LOGI(CDC_DEV_TAG, "Triggering connect");
                TEST_ASSERT(tud_connect());

                // End of the test case: Mask next suspend event (auto suspend) from the device after uninstalling the cdc-acm host
                mask_event = TINYUSB_EVENT_SUSPENDED;
                break;
            default:
                break;
            }
        }
    }
}

/**
 * @brief Run CDC-ACM Device with 2 interfaces in a loopback mode
 *
 * Device performs sudden disconnect followed by a connect upon receiving Resume callback
 */
TEST_CASE("mock_dev_app_resume_dconn", "[cdc_acm_mock_dev][resume_dconn][ignore]")
{
    cdc_acm_mock_device_run();

    while (1) {
        tinyusb_event_t dev_event;
        if (xQueueReceive(dev_evt_queue, &dev_event, portMAX_DELAY)) {

            switch (dev_event.id) {
            case TINYUSB_EVENT_RESUMED:
                TEST_ASSERT(tud_disconnect());
                ESP_LOGI(CDC_DEV_TAG, "Triggering disconnect");

                // Stay disconnected for a while and trigger connect
                vTaskDelay(pdMS_TO_TICKS(1000));

                ESP_LOGI(CDC_DEV_TAG, "Triggering connect");
                TEST_ASSERT(tud_connect());
                break;
            default:
                break;
            }
        }
    }
}

/**
 * @brief Run CDC-ACM Device with 2 interfaces in a loopback mode
 *
 * Device performs remote wakeup upon receiving Suspend callback
 */
TEST_CASE("mock_dev_app_remote_wake", "[cdc_acm_mock_dev][remote_wake][ignore]")
{
    cdc_acm_mock_device_run();
    tinyusb_event_id_t mask_event = UINT32_MAX;

    while (1) {
        tinyusb_event_t dev_event;
        if (xQueueReceive(dev_evt_queue, &dev_event, portMAX_DELAY)) {

            if (mask_event == dev_event.id) {
                ESP_LOGI(CDC_DEV_TAG, "Event %d masked", dev_event.id);
                mask_event = UINT32_MAX;    // Refresh masked event, allowing the device test app to handle further events without hard reset
                continue;
            }

            switch (dev_event.id) {
            case TINYUSB_EVENT_SUSPENDED:

                if (!dev_event.suspended.remote_wakeup) {
                    // Suspend event delivered, but remote wakeup is not enabled by the host
                    // Try to call remote wakeup on the device
                    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_remote_wakeup());
                    break;
                }

                // Device is now in suspended state with remote wakeup enabled, start remote wakeup signaling
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGI(CDC_DEV_TAG, "Triggering remote wakeup");
                TEST_ASSERT_EQUAL(ESP_OK, tinyusb_remote_wakeup());

                // End of the test case: Ignore the suspend event from the device after uninstalling the cdc-acm host
                mask_event = TINYUSB_EVENT_SUSPENDED;
                break;
            default:
                break;
            }
        }
    }
}

/**
 * @brief Run CDC-ACM Device with 2 interfaces in a loopback mode
 *
 * Device performs remote wakeup followed by a sudden disconnect and connect upon receiving Suspend callback
 */
TEST_CASE("mock_dev_app_remote_wake_dconn", "[cdc_acm_mock_dev][remote_wake_dconn][ignore]")
{
    cdc_acm_mock_device_run();
    tinyusb_event_id_t mask_event = UINT32_MAX;

    while (1) {
        tinyusb_event_t dev_event;
        if (xQueueReceive(dev_evt_queue, &dev_event, portMAX_DELAY)) {

            if (mask_event == dev_event.id) {
                ESP_LOGI(CDC_DEV_TAG, "Event %d masked", dev_event.id);
                mask_event = UINT32_MAX;    // Refresh masked event, allowing the device test app to handle further events without hard reset
                continue;
            }

            switch (dev_event.id) {
            case TINYUSB_EVENT_SUSPENDED:

                if (!dev_event.suspended.remote_wakeup) {
                    // Suspend event delivered, but remote wakeup is not enabled by the host
                    // Try to call remote wakeup on the device
                    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, tinyusb_remote_wakeup());
                    break;
                }

                // Device is now in suspended state with remote wakeup enabled
                // Start remote wakeup signaling followed by sudden disconnect
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGI(CDC_DEV_TAG, "Triggering remote wakeup and sudden disconnect");
                TEST_ASSERT_EQUAL(ESP_OK, tinyusb_remote_wakeup());
                TEST_ASSERT(tud_disconnect());

                // Stay disconnected for a while and trigger connect
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP_LOGI(CDC_DEV_TAG, "Triggering connect");
                TEST_ASSERT(tud_connect());

                // End of the test case: Ignore the suspend event from the device after uninstalling the cdc-acm host
                mask_event = TINYUSB_EVENT_SUSPENDED;
                break;
            default:
                break;
            }
        }
    }
}
