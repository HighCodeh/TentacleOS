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
 * @brief Free a transmit packet and its associated resources
 */
void mdns_priv_free_tx_packet(mdns_tx_packet_t *packet);

/**
 * @brief Create a probe packet for service discovery
 */
mdns_tx_packet_t *mdns_priv_create_probe_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t *services[], size_t len, bool first, bool include_ip);

/**
 * @brief Allocate and initialize an answer record for mDNS response
 */
bool mdns_priv_create_answer(mdns_out_answer_t **destination, uint16_t type, mdns_service_t *service,
                             mdns_host_item_t *host, bool flush, bool bye);

/**
 * @brief Create an announcement packet for service advertisement
 */
mdns_tx_packet_t *mdns_priv_create_announce_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t *services[], size_t len, bool include_ip);

/**
 * @brief Create an announcement packet from an existing probe packet
 */
mdns_tx_packet_t *mdns_priv_create_announce_from_probe(mdns_tx_packet_t *probe);

/**
 * @brief Dispatch a transmit packet for sending
 */
void mdns_priv_dispatch_tx_packet(mdns_tx_packet_t *p);

/**
 * @brief Send a goodbye message for a service subtype
 */
void mdns_priv_send_bye_subtype(mdns_srv_item_t *service, const char *instance_name, mdns_subtype_t *remove_subtypes);

/**
 * @brief Append host list information to service records
 */
bool mdns_priv_append_host_list_in_services(mdns_out_answer_t **destination, mdns_srv_item_t *services[], size_t services_len, bool flush, bool bye);

/**
 * @brief Clear the transmit queue head for a specific interface and protocol
 */
void mdns_priv_clear_tx_queue_if(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Clear the global transmit queue head
 */
void mdns_priv_clear_tx_queue(void);

/**
 * @brief Remove scheduled service packets for a given service
 */
void mdns_priv_remove_scheduled_service_packets(mdns_service_t *service);

/**
 * @brief Get the next packet from the PCB queue for a specific interface and protocol
 */
mdns_tx_packet_t *mdns_priv_get_next_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Deallocate an answer record
 */
void mdns_priv_dealloc_answer(mdns_out_answer_t **destination, uint16_t type, mdns_srv_item_t *service);

/**
 * @brief Remove a scheduled answer for a specific interface, protocol, and service
 */
void mdns_priv_remove_scheduled_answer(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, uint16_t type, mdns_srv_item_t *service);

/**
 * @brief Allocate a new packet with default settings
 */
mdns_tx_packet_t *mdns_priv_alloc_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief Send all pending mDNS packets
 */
void mdns_priv_send_packets(void);

/**
 * @brief Execute an mDNS action with specified type
 */
void mdns_priv_send_action(mdns_action_t *action, mdns_action_subtype_t type);

/**
 * @brief Schedule a transmit packet for sending after a specified delay
 */
void mdns_priv_send_after(mdns_tx_packet_t *packet, uint32_t ms_after);

/**
 * @brief  Send by for particular services on particular PCB
 */
void mdns_priv_send_bye(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t **services, size_t len, bool include_ip);

/**
 * @brief  Create answer packet to questions from parsed packet
 */
void mdns_priv_create_answer_from_parsed_packet(mdns_parsed_packet_t *parsed_packet);

/**
 * @brief  appends one TXT record ("key=value" or "key")
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  txt          one txt record
 *
 * @return length of added data: length of the added txt value + 1 on success
 *         0  if data won't fit the packet
 *         -1 if invalid TXT entry
 */
int mdns_priv_append_one_txt_record_entry(uint8_t *packet, uint16_t *index, mdns_txt_linked_item_t *txt);

#ifdef __cplusplus
}
#endif
