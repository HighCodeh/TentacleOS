/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include "mdns_private.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the mDNS network interfaces
 *
 * @note Called from mdns_init() in mdns.c
 */
esp_err_t mdns_priv_netif_init(void);

/**
 * @brief Deinitialize the mDNS network interfaces
 *
 * @note Called from mdns_init() in mdns.c
 */
esp_err_t mdns_priv_netif_deinit(void);

/**
 * @brief Unregister predefined (default) network interfaces
 *
 * @note Called from mdns_free() in mdns.c
 *
 */
void mdns_priv_netif_unregister_predefined_handlers(void);

/**
 * @brief Clean the internal netif for the particular interface
 *
 * @note Called from mdns_responder on disabling pcbs
 */
void mdns_priv_netif_disable(mdns_if_t tcpip_if);

/**
 * @brief Returns potentially duplicated interface
 *
 * @note Called from multiple places where Rx and Tx packets are processed
 */
mdns_if_t mdns_priv_netif_get_other_interface(mdns_if_t tcpip_if);

/**
 * @brief Gets the actual esp_netif pointer from the internal network interface list
 *
 * The supplied ordinal number could
 * - point to a predef netif -> "STA", "AP", "ETH"
 *      - if no entry in the list (NULL) -> check if the system added this netif
 * - point to a custom netif -> just return the entry in the list
 *      - users is responsible for the lifetime of this netif (to be valid between mdns-init -> deinit)
 *
 * @param tcpip_if Ordinal number of the interface
 * @return Pointer ot the esp_netif object if the interface is available, NULL otherwise
 */
esp_netif_t *mdns_priv_get_esp_netif(mdns_if_t tcpip_if);

#ifdef __cplusplus
}
#endif
