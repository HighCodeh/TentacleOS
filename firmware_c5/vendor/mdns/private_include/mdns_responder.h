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
* @brief Perform action from mdns responder
*
* @note Called from the _mdns_service_task() in mdns.c
*/
void mdns_priv_responder_action(mdns_action_t *action, mdns_action_subtype_t type);

/**
 * @brief Initializes responder storage
 */
esp_err_t mdns_priv_responder_init(void);

/**
 * @brief Frees responder storage
 */
void mdns_priv_responder_free(void);

/**
 * @brief  get global (mdns_server->hostname) hostname
 */
const char *mdns_priv_get_global_hostname(void);

/**
 * @brief  get service list
 */
mdns_srv_item_t *mdns_priv_get_services(void);

/**
 * @brief  get host list
 */
mdns_host_item_t *mdns_priv_get_hosts(void);

/**
 * @brief  get self host
 */
mdns_host_item_t *mdns_priv_get_self_host(void);

/**
 * @brief  set global hostname
 */
void mdns_priv_set_global_hostname(const char *hostname);

/**
 * @brief  get mdns_server->instance
 */
const char *mdns_priv_get_instance(void);

/**
 * @brief  set mdns_server->instance
 */
void mdns_priv_set_instance(const char *instance);

/**
 * @brief  Returns true if the mdns server is initialized
 */
bool mdns_priv_is_server_init(void);

/**
 * @brief  Restart the responder on all services without instance
 */
void mdns_priv_restart_all_pcbs_no_instance(void);

/**
 * @brief  Restart the responder on all active PCBs
 */
void mdns_priv_restart_all_pcbs(void);

/**
 * @brief Adds a delegated hostname to the linked list
 * @param hostname Host name pointer
 * @param address_list Address list
 * @return  true on success
 *          false if the host wasn't attached (this is our hostname, or alloc failure) so we have to free the structs
 */
bool mdns_priv_delegate_hostname_add(const char *hostname, mdns_ip_addr_t *address_list);

/**
 * @brief  Remaps hostname of self service
 */
void mdns_priv_remap_self_service_hostname(const char *old_hostname, const char *new_hostname);

#ifdef __cplusplus
}
#endif
