/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "mdns_private.h"

#define MDNS_UTILS_DEFAULT_DOMAIN "local"
#define MDNS_UTILS_SIZEOF_IP6_ADDR (MDNS_ANSWER_AAAA_SIZE)

/**
 * @brief  read uint16_t from a packet
 * @param  packet       the packet
 * @param  index        index in the packet where the value starts
 *
 * @return the value
 */
static inline uint16_t mdns_utils_read_u16(const uint8_t *packet, uint16_t index)
{
    return (uint16_t)(packet[index]) << 8 | packet[index + 1];
}

/**
 * @brief  read uint32_t from a packet
 * @param  packet       the packet
 * @param  index        index in the packet where the value starts
 *
 * @return the value
 */
static inline uint32_t mdns_utils_read_u32(const uint8_t *packet, uint16_t index)
{
    return (uint32_t)(packet[index]) << 24 | (uint32_t)(packet[index + 1]) << 16 | (uint32_t)(packet[index + 2]) << 8 | packet[index + 3];
}

/**
 * @brief  Check for null or empty string
 */
static inline bool mdns_utils_str_null_or_empty(const char *str)
{
    return (str == NULL || *str == 0);
}

/**
 * @brief  reads and formats MDNS FQDN into mdns_name_t structure
 *
 * @param  packet       MDNS packet
 * @param  start        Starting point of FQDN
 * @param  name         mdns_name_t structure to populate
 *
 * @return the address after the parsed FQDN in the packet or NULL on error
 */
const uint8_t *mdns_utils_parse_fqdn(const uint8_t *packet, const uint8_t *start, mdns_name_t *name, size_t packet_len);

/**
 * @brief  reads MDNS FQDN into mdns_name_t structure
 *         FQDN is in format: [hostname.|[instance.]_service._proto.]local.
 *
 * @param  packet       MDNS packet
 * @param  start        Starting point of FQDN
 * @param  name         mdns_name_t structure to populate
 * @param  buf          temporary char buffer
 *
 * @return the address after the parsed FQDN in the packet or NULL on error
 */
const uint8_t *mdns_utils_read_fqdn(const uint8_t *packet, const uint8_t *start, mdns_name_t *name, char *buf, size_t packet_len);

/**
 * @brief  Check if IPv6 address is NULL
 */
bool mdns_utils_ipv6_address_is_zero(esp_ip6_addr_t ip6);


/**
 * @brief Checks if the hostname is ours
 */
bool mdns_utils_hostname_is_ours(const char *hostname);

/**
 * @brief Checks if given service matches with arguments
 */
bool mdns_utils_service_match(const mdns_service_t *srv, const char *service, const char *proto,
                              const char *hostname);

/**
 * @brief  finds service from given service type
 * @param  server       the server
 * @param  service      service type to match
 * @param  proto        proto to match
 * @param  hostname     hostname of the service (if non-null)
 *
 * @return the service item if found or NULL on error
 */
mdns_srv_item_t *mdns_utils_get_service_item(const char *service, const char *proto, const char *hostname);

/**
 * @brief  finds service from given instance, etc
 */
mdns_srv_item_t *mdns_utils_get_service_item_instance(const char *instance, const char *service, const char *proto,
                                                      const char *hostname);

/**
 * @brief  returns true if the service matches with args
 */
bool mdns_utils_service_match_instance(const mdns_service_t *srv, const char *instance, const char *service,
                                       const char *proto, const char *hostname);

/**
 * @brief  returns true if the two instances match
 */
bool mdns_utils_instance_name_match(const char *lhs, const char *rhs);

/**
 * @brief  Get service instance name
 */
const char *mdns_utils_get_service_instance_name(const mdns_service_t *service);

/**
 * @brief  Copy address list
 */
mdns_ip_addr_t *mdns_utils_copy_address_list(const mdns_ip_addr_t *address_list);

/**
 * @brief  Free address list
 */
void mdns_utils_free_address_list(mdns_ip_addr_t *address_list);

/**
 * @brief  appends uint16_t in a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  value        the value to set
 *
 * @return length of added data: 0 on error or 2 on success
 */
uint8_t mdns_utils_append_u16(uint8_t *packet, uint16_t *index, uint16_t value);

/**
 * @brief  appends byte in a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  value        the value to set
 *
 * @return length of added data: 0 on error or 1 on success
 */
static inline uint8_t mdns_utils_append_u8(uint8_t *packet, uint16_t *index, uint8_t value)
{
    if (*index >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    packet[*index] = value;
    *index += 1;
    return 1;
}
