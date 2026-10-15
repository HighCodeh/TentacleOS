/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "usb/usb_types_cdc.h"
#include "usb/usb_host.h"               // For USB Host suspend/resume API

#ifdef USB_HOST_LIB_EVENT_FLAGS_AUTO_SUSPEND
/**
 * @brief Defined when the underlying ESP-IDF exposes USB Host suspend and
 *        resume support.
 */
#define CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
#endif

#ifdef REMOTE_WAKE_HAL_SUPPORTED
/**
 * @brief Defined when the underlying ESP-IDF exposes remote wakeup support for
 *        USB host devices.
 */
#define CDC_HOST_REMOTE_WAKE_SUPPORTED
#endif

/**
 * @brief Opaque handle to an opened CDC-ACM device.
 */
typedef struct cdc_dev_s *cdc_acm_dev_hdl_t;

/**
 * @brief CDC-ACM device event types reported to the application.
 */
typedef enum {
    CDC_ACM_HOST_ERROR,                             /*!< An error occurred on the CDC-ACM device. */
    CDC_ACM_HOST_SERIAL_STATE,                      /*!< The CDC-ACM serial state changed. */
    CDC_ACM_HOST_NETWORK_CONNECTION,                /*!< The CDC-ACM network connection state changed. */
    CDC_ACM_HOST_DEVICE_DISCONNECTED,               /*!< The CDC-ACM device was disconnected. */
#ifdef CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
    CDC_ACM_HOST_DEVICE_SUSPENDED,                  /*!< The CDC-ACM device was suspended. */
    CDC_ACM_HOST_DEVICE_RESUMED,                    /*!< The CDC-ACM device was resumed. */
#endif // CDC_HOST_SUSPEND_RESUME_API_SUPPORTED
} cdc_acm_host_dev_event_t;

/**
 * @brief CDC-ACM device event data.
 */
typedef struct {
    cdc_acm_host_dev_event_t type; /*!< Event type. */
    union {
        int error;                         /*!< USB Host error code. */
        cdc_acm_uart_state_t serial_state; /*!< Serial state bitmap. */
        bool network_connected;            /*!< Network connection state. */
        cdc_acm_dev_hdl_t cdc_hdl;         /*!< Device handle related to the event. */
    } data;                                /*!< Event-specific payload. */
} cdc_acm_host_dev_event_data_t;

/**
 * @brief Data receive callback type.
 *
 * @param[in] data Pointer to the received data.
 * @param[in] data_len Length of the received data in bytes.
 * @param[in] user_arg User argument passed to the open function.
 *
 * @return true if the data was processed and the RX buffer can be flushed
 * @return false if the data was not processed and new data should be appended
 *               to the RX buffer
 */
typedef bool (*cdc_acm_data_callback_t)(const uint8_t *data, size_t data_len, void *user_arg);

/**
 * @brief Device event callback type.
 *
 * @param[in] event Event data structure.
 * @param[in] user_ctx User context passed to the open function.
 */
typedef void (*cdc_acm_host_dev_callback_t)(const cdc_acm_host_dev_event_data_t *event, void *user_ctx);

/**
 * @brief CDC-ACM device configuration.
 */
typedef struct {
    uint32_t connection_timeout_ms;       /*!< Timeout for USB device connection in milliseconds. */
    size_t out_buffer_size;               /*!< Maximum USB bulk OUT transfer size. Set to 0 for read-only devices.
                                               Larger writes are split into multiple transfers. */
    size_t in_buffer_size;                /*!< Maximum USB bulk IN transfer size. If set to 0, the IN endpoint MPS
                                               is used. */
    cdc_acm_host_dev_callback_t event_cb; /*!< Device event callback. Can be NULL. */
    cdc_acm_data_callback_t data_cb;      /*!< Data RX callback. Can be NULL for write-only devices. */
    void *user_arg;                       /*!< User argument passed to both callbacks. */
} cdc_acm_host_device_config_t;

/**
 * @brief CDC-ACM host open configuration.
 */
typedef struct {
    uint16_t vid;                         /*!< Device vendor ID, or CDC_HOST_ANY_VID to match any vendor ID. */
    uint16_t pid;                         /*!< Device product ID, or CDC_HOST_ANY_PID to match any product ID. */
    uint8_t interface_idx;                /*!< Device interface index used for CDC-ACM communication. */
    uint8_t dev_addr;                     /*!< USB device address to select, or CDC_HOST_ANY_DEV_ADDR to match any. */

    uint32_t connection_timeout_ms;       /*!< Timeout for USB device connection in milliseconds. */
    size_t out_buffer_size;               /*!< Maximum USB bulk OUT transfer size. Set to 0 for read-only devices.
                                               Larger writes are split into multiple transfers. */
    size_t in_buffer_size;                /*!< Maximum USB bulk IN transfer size. If set to 0, the IN endpoint MPS
                                               is used. */
    cdc_acm_host_dev_callback_t event_cb; /*!< Device event callback. Can be NULL. */
    cdc_acm_data_callback_t data_cb;      /*!< Data RX callback. Can be NULL for write-only devices. */
    void *user_arg;                       /*!< User argument passed to both callbacks. */
} cdc_acm_host_open_config_t;
