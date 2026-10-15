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
 * @brief Disable the PCB for the selected interface and protocol
 * @note Called from the main module (deinit and event handler)
 */
void mdns_priv_pcb_disable(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Enable the PCB for the selected interface and protocol
 * @note Called from the main module (deinit and event handler)
 */
void mdns_priv_pcb_enable(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Set the interface as duplicate
 * @note Called from the packet parser when checking for collisions
 */
void mdns_priv_pcb_set_duplicate(mdns_if_t tcpip_if);

/**
 * @brief Send announcement on particular PCB
 * @note Called mainly from mdns.c to send announcements on all interfaces
 */
void mdns_priv_pcb_announce(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t **services, size_t len, bool include_ip);

/**
 * @brief Checks if the PCB is OFF
 * @note Called from mdns_send.c
 */
bool mdns_priv_pcb_is_off(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Schedules TX packets for various PCB states of probing and announcing
 * @note Called from mdns_send.c
 */
void mdns_priv_pcb_schedule_tx_packet(mdns_tx_packet_t *p);

/**
 * @brief Update probing services on certain service removal
 * @note Called from mdns_send.c upon removing scheduled packets for a service
 */
void mdns_priv_pcb_check_probing_services(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_service_t *service, bool removed_answers, bool *should_remove_questions);

/**
 * @brief Deinit pcbs
 * @note Called from mdns_free()
 */
void mdns_priv_pcb_deinit(void);

/**
 * @brief Checks if the netif and PCB is initialized
 * @note Called from mdns_send.c
 */
bool mdsn_priv_pcb_is_inited(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Checks if PCB is duplicated
 * @note Called from mdns_send.c
 */
bool mdns_priv_pcb_is_duplicate(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Checks if PCB is probing
 * @note Called from mdns_receive.c
 */
bool mdns_priv_pcb_is_probing(mdns_rx_packet_t *packet);

/**
 * @brief Set probe failed to PCB
 * @note Called from mdns_receive.c
 */
void mdns_priv_pcb_set_probe_failed(mdns_rx_packet_t *packet);

/**
 * @brief Checks if PCB completed probing
 * @note Called from mdns_receive.c
 */
bool mdns_priv_pcb_is_after_probing(mdns_rx_packet_t *packet);

/**
 * @brief Checks if the selected interface has a duplicate
 * @note Called from mdns_send.c
 */
bool mdns_priv_pcb_check_for_duplicates(mdns_if_t tcpip_if);

/**
 * @brief Sends bye for particular services on particular PCB
 * @note Called from mdns.c
 */
void mdns_priv_pcb_send_bye_service(mdns_srv_item_t **services, size_t len, bool include_ip);

/**
 * @brief  Send probe on all active PCBs
 */
void mdns_priv_probe_all_pcbs(mdns_srv_item_t **services, size_t len, bool probe_ip, bool clear_old_probe);

/**
 * @brief  Send probe for particular services on particular PCB
 *
 * Tests possible duplication on probing service structure and probes only for new entries.
 * - If pcb probing then add only non-probing services and restarts probing
 * - If pcb not probing, run probing for all specified services
 */
void mdns_priv_init_pcb_probe(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t **services, size_t len, bool probe_ip);

#ifdef __cplusplus
}
#endif
