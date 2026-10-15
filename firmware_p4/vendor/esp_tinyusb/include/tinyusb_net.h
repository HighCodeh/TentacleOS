/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "sdkconfig.h"

#if (CONFIG_TINYUSB_NET_MODE_NONE != 1)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TinyUSB NET receive callback type.
 *
 * @param[in] buffer Pointer to the received packet payload.
 * @param[in] len Packet length in bytes.
 * @param[in] ctx User context from tinyusb_net_config_t.user_context.
 *
 * @return The return value is currently ignored by esp_tinyusb.
 */
typedef esp_err_t (*tusb_net_rx_cb_t)(void *buffer, uint16_t len, void *ctx);

/**
 * @brief TinyUSB NET TX buffer release callback type.
 *
 * @param[in] buffer User token passed as `buff_free_arg` to the send function.
 * @param[in] ctx User context from tinyusb_net_config_t.user_context.
 */
typedef void (*tusb_net_free_tx_cb_t)(void *buffer, void *ctx);

/**
 * @brief TinyUSB NET initialization callback type.
 *
 * @param[in] ctx User context from tinyusb_net_config_t.user_context.
 */
typedef void (*tusb_net_init_cb_t)(void *ctx);

/**
 * @brief TinyUSB NET driver configuration.
 */
typedef struct {
    uint8_t mac_addr[6];                      /*!< Device MAC address. */
    tusb_net_rx_cb_t on_recv_callback;        /*!< Optional receive callback. */
    tusb_net_free_tx_cb_t free_tx_buffer;     /*!< Optional callback used to release TX buffers or user tokens.
                                                   Required when the application needs asynchronous send cleanup. */
    tusb_net_init_cb_t on_init_callback;      /*!< Optional callback invoked from tud_network_init_cb(). */
    void *user_context;                       /*!< User context passed to every callback. */
} tinyusb_net_config_t;

/**
 * @brief Initialize the TinyUSB NET driver.
 *
 * @param[in] cfg Driver configuration. Must not be NULL.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if the TinyUSB NET driver is already initialized
 */
esp_err_t tinyusb_net_init(const tinyusb_net_config_t *cfg);

/**
 * @brief Deinitialize the TinyUSB NET driver.
 */
void tinyusb_net_deinit(void);

/**
 * @brief Send a packet synchronously through the TinyUSB NET interface.
 *
 * @note Synchronous and asynchronous sends can be mixed.
 * @note The first synchronous send allocates synchronization primitives, which
 *       increases heap usage.
 *
 * @param[in] buffer Packet payload buffer.
 * @param[in] len Packet length in bytes.
 * @param[in] buff_free_arg User token passed to `free_tx_buffer`, typically the
 *                          packet buffer pointer.
 * @param[in] timeout Timeout in RTOS ticks.
 *
 * @return
 *      - ESP_OK if the packet is accepted by TinyUSB for transmission
 *      - ESP_FAIL if the USB interface cannot accept the packet
 *      - ESP_ERR_TIMEOUT if the transmission does not complete before `timeout`
 *      - ESP_ERR_INVALID_STATE if the TinyUSB NET interface is not mounted
 *      - ESP_ERR_NO_MEM if internal synchronization objects cannot be allocated
 */
esp_err_t tinyusb_net_send_sync(void *buffer, uint16_t len, void *buff_free_arg, TickType_t  timeout);

/**
 * @brief Queue a packet for asynchronous transmission through the TinyUSB NET interface.
 *
 * @note When using asynchronous sends, free the packet through
 *       `free_tx_buffer` or another application-managed path.
 * @note Synchronous and asynchronous sends can be mixed.
 * @note `ESP_OK` means the packet was queued for processing in the TinyUSB task.
 *       It does not guarantee that the USB interface accepted the packet.
 *
 * @param[in] buffer Packet payload buffer.
 * @param[in] len Packet length in bytes.
 * @param[in] buff_free_arg User token passed to `free_tx_buffer`, typically the
 *                          packet buffer pointer.
 *
 * @return
 *      - ESP_OK if the packet is queued for deferred processing
 *      - ESP_ERR_INVALID_STATE if the TinyUSB NET interface is not mounted
 */
esp_err_t tinyusb_net_send_async(void *buffer, uint16_t len, void *buff_free_arg);

#endif // (CONFIG_TINYUSB_NET_MODE_NONE != 1)

#ifdef __cplusplus
}
#endif
