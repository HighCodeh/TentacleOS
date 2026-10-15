/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include "sdkconfig.h"
#include "mdns_private.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_MDNS_ENABLE_DEBUG_PRINTS
#include "esp_log.h"

/*  Define the debug macros for the mDNS module
 */
#define DBG_BROWSE_RESULTS(result, browse) mdns_debug_printf_browse_result(result, browse)

#define DBG_BROWSE_RESULTS_WITH_MSG(result, ...) do { \
                                                     ESP_LOGD("mdns", __VA_ARGS__);               \
                                                     mdns_debug_printf_browse_result_all(result); \
                                                 } while(0)

#define DBG_TX_PACKET(packet, data, len) mdns_debug_tx_packet(packet, data, len)

#define DBG_RX_PACKET(packet, data, len) mdns_debug_rx_packet(packet, data, len)

/**
 * @brief Print the browse results
 */
void mdns_debug_printf_browse_result(mdns_result_t *r_t, mdns_browse_t *b_t);

/**
 * @brief Print all the browse results
 */
void mdns_debug_printf_browse_result_all(mdns_result_t *r_t);

/**
 * @brief Print the tx packet
 */
void mdns_debug_tx_packet(mdns_tx_packet_t *p, uint8_t packet[MDNS_MAX_PACKET_SIZE], uint16_t index);

/**
 * @brief Print the rx packet
 */
void mdns_debug_rx_packet(mdns_rx_packet_t *packet, const uint8_t* data, uint16_t len);

#else

/*  Define the dummy debug macros if debugging is OFF
 */
#define DBG_BROWSE_RESULTS(result, browse)
#define DBG_BROWSE_RESULTS_WITH_MSG(result, ...)
#define DBG_TX_PACKET(packet, data, len)
#define DBG_RX_PACKET(packet, data, len)

#endif  // CONFIG_MDNS_ENABLE_DEBUG_PRINTS

#ifdef __cplusplus
}
#endif
