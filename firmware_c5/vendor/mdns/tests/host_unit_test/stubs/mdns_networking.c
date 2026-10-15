/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mdns_networking.h"
#include "mdns_mem_caps.h"
#include "mdns_service.h"

struct pbuf  {
    void *payload;
    size_t len;
};

bool mdns_priv_if_ready(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    return true; // Default to ready for testing
}

esp_err_t mdns_priv_if_init(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    return ESP_OK;
}

esp_err_t mdns_priv_if_deinit(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    return ESP_OK;
}

size_t mdns_priv_if_write(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, const esp_ip_addr_t *ip, uint16_t port, uint8_t *data, size_t len)
{
    return len; // Return the input length as if all data was sent successfully
}

void *mdns_priv_get_packet_data(mdns_rx_packet_t *packet)
{
    return packet->pb->payload;
}

size_t mdns_priv_get_packet_len(mdns_rx_packet_t *packet)
{
    return packet->pb->len;
}

void mdns_priv_packet_free(mdns_rx_packet_t *packet)
{
    mdns_mem_free(packet->pb->payload);
    mdns_mem_free(packet->pb);
    mdns_mem_free(packet);
}

esp_err_t mdns_packet_push(esp_ip_addr_t *addr, int port, mdns_if_t tcpip_if, uint8_t*data, size_t len)
{
    // Allocate the packet structure and pass it to the mdns main engine
    mdns_rx_packet_t *packet = (mdns_rx_packet_t *) mdns_mem_calloc(1, sizeof(mdns_rx_packet_t));
    struct pbuf *packet_pbuf = mdns_mem_calloc(1, sizeof(struct pbuf));
    uint8_t *buf = mdns_mem_malloc(len);
    if (packet == NULL || packet_pbuf == NULL || buf == NULL) {
        mdns_mem_free(buf);
        mdns_mem_free(packet_pbuf);
        mdns_mem_free(packet);
        return ESP_ERR_NO_MEM;
    }
    memcpy(buf, data, len);
    packet_pbuf->payload = buf;
    packet_pbuf->len = len;
    packet->tcpip_if = tcpip_if;
    packet->pb = packet_pbuf;
    packet->src_port = port;
    memcpy(&packet->src, addr, sizeof(esp_ip_addr_t));
    memset(&packet->dest, 0, sizeof(esp_ip_addr_t));
    packet->multicast = 1;
    packet->dest.type = packet->src.type;
    packet->ip_protocol = packet->src.type == ESP_IPADDR_TYPE_V4 ? MDNS_IP_PROTOCOL_V4 : MDNS_IP_PROTOCOL_V6;
    mdns_action_t *action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
    if (!action) {
        return ESP_ERR_NO_MEM;
    }

    action->type = ACTION_RX_HANDLE;
    action->data.rx_handle.packet = packet;
    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(action);
        mdns_mem_free(packet->pb->payload);
        mdns_mem_free(packet->pb);
        mdns_mem_free(packet);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
