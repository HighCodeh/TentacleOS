/*
 * SPDX-FileCopyrightText: 2020-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sdkconfig.h"
#include "esp_err.h"
#include "class/cdc/cdc.h"

#if (CONFIG_TINYUSB_CDC_ENABLED != 1)
#error "TinyUSB CDC driver must be enabled in menuconfig"
#endif


/**
 * @brief CDC ACM interface identifier.
 */
typedef enum {
    TINYUSB_CDC_ACM_0 = 0x0,              /*!< CDC ACM interface 0. */
    TINYUSB_CDC_ACM_1,                    /*!< CDC ACM interface 1. */
    TINYUSB_CDC_ACM_MAX                   /*!< Number of CDC ACM interfaces. */
} tinyusb_cdcacm_itf_t;

/*************************************************************************/
/*                      Callbacks and events                             */
/*************************************************************************/

/**
 * @brief Event data for CDC_EVENT_RX_WANTED_CHAR.
 */
typedef struct {
    char wanted_char; /*!< Requested character that triggered the callback. */
} cdcacm_event_rx_wanted_char_data_t;

/**
 * @brief Event data for CDC_EVENT_LINE_STATE_CHANGED.
 */
typedef struct {
    bool dtr; /*!< Data Terminal Ready line state. */
    bool rts; /*!< Request To Send line state. */
} cdcacm_event_line_state_changed_data_t;

/**
 * @brief Event data for CDC_EVENT_LINE_CODING_CHANGED.
 */
typedef struct {
    cdc_line_coding_t const *p_line_coding; /*!< Updated line coding value. */
} cdcacm_event_line_coding_changed_data_t;

/**
 * @brief CDC ACM event identifier.
 */
typedef enum {
    CDC_EVENT_RX,                           /*!< RX data is available. */
    CDC_EVENT_RX_WANTED_CHAR,               /*!< The requested character was received. */
    CDC_EVENT_LINE_STATE_CHANGED,           /*!< DTR or RTS changed. */
    CDC_EVENT_LINE_CODING_CHANGED           /*!< Line coding changed. */
} cdcacm_event_type_t;

/**
 * @brief CDC ACM event data passed to tusb_cdcacm_callback_t.
 */
typedef struct {
    cdcacm_event_type_t type; /*!< Event type. */
    union {
        cdcacm_event_rx_wanted_char_data_t rx_wanted_char_data; /*!< Data for CDC_EVENT_RX_WANTED_CHAR. */
        cdcacm_event_line_state_changed_data_t line_state_changed_data; /*!< Data for
                                                                             CDC_EVENT_LINE_STATE_CHANGED. */
        cdcacm_event_line_coding_changed_data_t line_coding_changed_data; /*!< Data for
                                                                               CDC_EVENT_LINE_CODING_CHANGED. */
    };
} cdcacm_event_t;

/**
 * @brief CDC ACM event callback type.
 *
 * @param[in] itf CDC ACM interface number.
 * @param[in] event Event data.
 */
typedef void(*tusb_cdcacm_callback_t)(int itf, cdcacm_event_t *event);

/**
 * @brief CDC ACM configuration.
 */
typedef struct {
    tinyusb_cdcacm_itf_t cdc_port;                      /*!< CDC ACM interface to initialize. */
    tusb_cdcacm_callback_t callback_rx;                /*!< Optional callback for CDC_EVENT_RX. */
    tusb_cdcacm_callback_t callback_rx_wanted_char;    /*!< Optional callback for CDC_EVENT_RX_WANTED_CHAR. */
    tusb_cdcacm_callback_t callback_line_state_changed; /*!< Optional callback for
                                                             CDC_EVENT_LINE_STATE_CHANGED. */
    tusb_cdcacm_callback_t callback_line_coding_changed; /*!< Optional callback for
                                                              CDC_EVENT_LINE_CODING_CHANGED. */
} tinyusb_config_cdcacm_t;

/************************************************************************/
/*                          Public functions                            */
/************************************************************************/

/**
 * @brief Initialize a CDC ACM interface.
 *
 * @param[in] cfg CDC ACM configuration. Must not be NULL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the selected CDC interface is already initialized
 *      - Other error codes from the underlying CDC initialization
 */
esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *cfg);

/**
 * @brief Deinitialize a CDC ACM interface.
 *
 * @param[in] itf CDC ACM interface number.
 *
 * @return
 *      - ESP_OK on success
 *      - Other error codes from the underlying CDC deinitialization
 */
esp_err_t tinyusb_cdcacm_deinit(int itf);

/**
 * @brief Register a CDC ACM event callback.
 *
 * If a callback is already registered for the selected event, it is replaced.
 *
 * @param[in] itf CDC ACM interface number.
 * @param[in] event_type Event to associate with the callback.
 * @param[in] callback Callback function, or NULL to disable the callback for
 *                     the selected event.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `event_type` is invalid
 *      - ESP_ERR_INVALID_STATE if the interface is not initialized
 */
esp_err_t tinyusb_cdcacm_register_callback(tinyusb_cdcacm_itf_t itf,
                                           cdcacm_event_type_t event_type,
                                           tusb_cdcacm_callback_t callback);

/**
 * @brief Unregister a CDC ACM event callback.
 *
 * @param[in] itf CDC ACM interface number.
 * @param[in] event_type Event whose callback should be removed.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if `event_type` is invalid
 *      - ESP_ERR_INVALID_STATE if the interface is not initialized
 */
esp_err_t tinyusb_cdcacm_unregister_callback(tinyusb_cdcacm_itf_t itf, cdcacm_event_type_t event_type);

/**
 * @brief Queue one character for transmission.
 *
 * @param[in] itf CDC ACM interface number.
 * @param[in] ch Character to queue.
 *
 * @return Number of bytes queued. Returns 0 if the interface is not
 *         initialized or the TX buffer cannot accept more data.
 */
size_t tinyusb_cdcacm_write_queue_char(tinyusb_cdcacm_itf_t itf, char ch);

/**
 * @brief Queue data for transmission.
 *
 * @param[in] itf CDC ACM interface number.
 * @param[in] in_buf Input buffer.
 * @param[in] in_size Input buffer size in bytes.
 *
 * @return Number of bytes queued. Returns 0 if the interface is not
 *         initialized.
 */
size_t tinyusb_cdcacm_write_queue(tinyusb_cdcacm_itf_t itf, const uint8_t *in_buf, size_t in_size);

/**
 * @brief Flush queued data from a CDC ACM TX buffer.
 *
 * Use tinyusb_cdcacm_write_queue() to add data to the buffer before calling
 * this function.
 *
 * @note Avoid calling this function with a timeout from CDC callbacks. TinyUSB
 *       may defer endpoint flushing until callback processing completes, which
 *       can cause the flush to block until the timeout expires.
 *
 * @param[in] itf CDC ACM interface number.
 * @param[in] timeout_ticks Flush timeout in RTOS ticks. Set to 0 for
 *                          non-blocking mode.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FINISHED if data is still pending in non-blocking mode
 *      - ESP_ERR_TIMEOUT if the timeout expires before the flush completes
 *      - ESP_FAIL if the interface is not initialized
 */
esp_err_t tinyusb_cdcacm_write_flush(tinyusb_cdcacm_itf_t itf, uint32_t timeout_ticks);

/**
 * @brief Read received data from a CDC ACM interface.
 *
 * @param[in] itf CDC ACM interface number.
 * @param[out] out_buf Output buffer. Must point to writable memory when
 *                     `out_buf_sz` is greater than 0.
 * @param[in] out_buf_sz Output buffer size in bytes.
 * @param[out] rx_data_size Number of bytes written to `out_buf`. Must not be
 *                          NULL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the interface is not initialized
 */
esp_err_t tinyusb_cdcacm_read(tinyusb_cdcacm_itf_t itf, uint8_t *out_buf, size_t out_buf_sz, size_t *rx_data_size);

/**
 * @brief Check whether a CDC ACM interface is initialized.
 *
 * @param[in] itf CDC ACM interface number.
 *
 * @return `true` if the interface is initialized, otherwise `false`.
 */
bool tinyusb_cdcacm_initialized(tinyusb_cdcacm_itf_t itf);

#ifdef __cplusplus
}
#endif
