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
 * @brief Start or stop requested queries
 * @note Called periodically from timer task in mdns.c
 */
void mdns_priv_query_start_stop(void);

/**
 *  @brief Free search item queue
 *  @note Called from mdns_free()
 */
void mdns_priv_query_free(void);

/**
 * @brief Free search results
 * @note Called from multiple modules (browser, querier, core)
 */
void mdns_priv_query_results_free(mdns_result_t *results);

/**
 * @brief Complete the query if max results reached
 * @note Called from the packet parser
 */
void mdns_priv_query_done(void);

/**
*  @brief Looks for the name/type in active search items
*
*  @note Called from the packet parser (mdns_receive.c)
*
*  @return search results
*/
mdns_search_once_t *mdns_priv_query_find(mdns_name_t *name, uint16_t type, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Looks for the name/type in the search items
 *
 * @note Called from the packet parser (mdns_receive.c)
 */
mdns_search_once_t *mdns_priv_query_find_from(mdns_search_once_t *s, mdns_name_t *name, uint16_t type, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Add TXT item to the search results
 * @note Called from the packet parser (mdns_receive.c)
 */
void mdns_priv_query_result_add_txt(mdns_search_once_t *search, mdns_txt_item_t *txt, uint8_t *txt_value_len,
                                    size_t txt_count, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol,
                                    uint32_t ttl);
/**
 * @brief Add IP (A/AAAA records) to the search results
 * @note Called from the packet parser (mdns_receive.c)
 */
void mdns_priv_query_result_add_ip(mdns_search_once_t *search, const char *hostname, esp_ip_addr_t *ip,
                                   mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, uint32_t ttl);

/**
 * @brief Add SRV record to the search results
 * @note Called from the packet parser (mdns_receive.c)
 */
void mdns_priv_query_result_add_srv(mdns_search_once_t *search, const char *hostname, uint16_t port,
                                    mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, uint32_t ttl);

/**
 * @brief Add PTR record to the search results
 * @note Called from the packet parser (mdns_receive.c)
 */
mdns_result_t *mdns_priv_query_result_add_ptr(mdns_search_once_t *search, const char *instance,
                                              const char *service_type, const char *proto, mdns_if_t tcpip_if,
                                              mdns_ip_protocol_t ip_protocol, uint32_t ttl);

/**
 * @brief Perform action from mdns service queue
 *
 * @note Called from the _mdns_service_task() in mdns.c
 */
void mdns_priv_query_action(mdns_action_t *action, mdns_action_subtype_t type);

/**
 * @brief  Create linked IP (copy) from parsed one
 */
mdns_ip_addr_t *mdns_priv_result_addr_create_ip(esp_ip_addr_t *ip);

/**
 * @brief Update TTL results to whichever is smaller
 */
static inline void mdns_priv_query_update_result_ttl(mdns_result_t *r, uint32_t ttl)
{
    r->ttl = r->ttl < ttl ? r->ttl : ttl;
}

/**
 * @brief Send a search query packet on the specified interface and protocol
 */
void mdns_priv_query_send(mdns_search_once_t *search, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

#ifdef __cplusplus
}
#endif
