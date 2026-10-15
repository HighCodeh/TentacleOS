/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/*
 * MDNS Server Networking -- private include
 *
 */
#include "mdns.h"
#include "mdns_private.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Check if the netif on the selected interfacce and protocol is ready
 */
bool mdns_priv_if_ready(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief  Start PCB
 */
esp_err_t mdns_priv_if_init(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief  Stop PCB
 */
esp_err_t mdns_priv_if_deinit(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol);

/**
 * @brief  send packet over UDP
 *
 * @param  server       The server
 * @param  data         byte array containing the packet data
 * @param  len          length of the packet data
 *
 * @return length of sent packet or 0 on error
 */
size_t mdns_priv_if_write(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, const esp_ip_addr_t *ip, uint16_t port, uint8_t *data, size_t len);

/**
 * @brief  Gets data pointer to the mDNS packet
 */
void *mdns_priv_get_packet_data(mdns_rx_packet_t *packet);

/**
 * @brief  Gets data length of c
 */
size_t mdns_priv_get_packet_len(mdns_rx_packet_t *packet);

/**
 * @brief  Free the  mDNS packet
 */
void mdns_priv_packet_free(mdns_rx_packet_t *packet);

#ifdef __cplusplus
}
#endif
