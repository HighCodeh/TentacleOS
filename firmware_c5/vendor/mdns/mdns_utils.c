/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "sdkconfig.h"
#include "mdns_private.h"
#include "mdns_mem_caps.h"
#include "esp_log.h"
#include "mdns_utils.h"
#include "mdns_responder.h"

static const char *TAG = "mdns_utils";
static const char *MDNS_DEFAULT_DOMAIN = "local";
static const char *MDNS_SUB_STR = "_sub";

const uint8_t *mdns_utils_read_fqdn(const uint8_t *packet, const uint8_t *start, mdns_name_t *name, char *buf, size_t packet_len)
{
    size_t index = 0;
    const uint8_t *packet_end = packet + packet_len;
    while (start + index < packet_end && start[index]) {
        if (name->parts == 4) {
            name->invalid = true;
        }
        uint8_t len = start[index++];
        if (len < 0xC0) {
            if (len > 63) {
                //length can not be more than 63
                return NULL;
            }
            uint8_t i;
            for (i = 0; i < len; i++) {
                if (start + index >= packet_end) {
                    return NULL;
                }
                buf[i] = start[index++];
            }
            buf[len] = '\0';
            if (name->parts == 1 && buf[0] != '_'
                    && (strcasecmp(buf, MDNS_DEFAULT_DOMAIN) != 0)
                    && (strcasecmp(buf, "arpa") != 0)
#ifndef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
                    && (strcasecmp(buf, "ip6") != 0)
                    && (strcasecmp(buf, "in-addr") != 0)
#endif
               ) {
                strlcat(name->host, ".", sizeof(name->host));
                strlcat(name->host, buf, sizeof(name->host));
            } else if (strcasecmp(buf, MDNS_SUB_STR) == 0) {
                name->sub = 1;
            } else if (!name->invalid) {
                char *mdns_name_ptrs[] = {name->host, name->service, name->proto, name->domain};
                memcpy(mdns_name_ptrs[name->parts++], buf, len + 1);
            }
        } else {
            if (start + index >= packet_end) {
                // truncated compression pointer (second byte would be out of bounds)
                return NULL;
            }
            size_t address = (((uint16_t)len & 0x3F) << 8) | start[index++];
            if ((packet + address) >= start) {
                //reference address can not be after where we are
                return NULL;
            }
            if (mdns_utils_read_fqdn(packet, packet + address, name, buf, packet_len)) {
                return start + index;
            }
            return NULL;
        }
    }
    return start + index + 1;
}

const uint8_t *mdns_utils_parse_fqdn(const uint8_t *packet, const uint8_t *start, mdns_name_t *name, size_t packet_len)
{
    name->parts = 0;
    name->sub = 0;
    name->host[0] = 0;
    name->service[0] = 0;
    name->proto[0] = 0;
    name->domain[0] = 0;
    name->invalid = false;

    static char buf[MDNS_NAME_BUF_LEN];

    const uint8_t *next_data = (uint8_t *) mdns_utils_read_fqdn(packet, start, name, buf, packet_len);
    if (!next_data) {
        return 0;
    }
    if (!name->parts || name->invalid) {
        return next_data;
    }
    if (name->parts == 3) {
        memmove((uint8_t *)name + (MDNS_NAME_BUF_LEN), (uint8_t *)name, 3 * (MDNS_NAME_BUF_LEN));
        name->host[0] = 0;
    } else if (name->parts == 2) {
        memmove((uint8_t *)(name->domain), (uint8_t *)(name->service), (MDNS_NAME_BUF_LEN));
        name->service[0] = 0;
        name->proto[0] = 0;
    }
    if (strcasecmp(name->domain, MDNS_DEFAULT_DOMAIN) == 0 || strcasecmp(name->domain, "arpa") == 0) {
        return next_data;
    }
    name->invalid = true; // mark the current name invalid, but continue with other question
    return next_data;
}

bool mdns_utils_hostname_is_ours(const char *hostname)
{
    if (!mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname()) &&
            strcasecmp(hostname, mdns_priv_get_global_hostname()) == 0) {
        return true;
    }
    mdns_host_item_t *host = mdns_priv_get_hosts();
    while (host != NULL) {
        if (strcasecmp(hostname, host->hostname) == 0) {
            return true;
        }
        host = host->next;
    }
    return false;
}

bool mdns_utils_service_match(const mdns_service_t *srv, const char *service, const char *proto,
                              const char *hostname)
{
    if (!service || !proto || !srv || !srv->hostname) {
        return false;
    }
    return !strcasecmp(srv->service, service) && !strcasecmp(srv->proto, proto) &&
           (mdns_utils_str_null_or_empty(hostname) || !strcasecmp(srv->hostname, hostname));
}

mdns_srv_item_t *mdns_utils_get_service_item(const char *service, const char *proto, const char *hostname)
{
    mdns_srv_item_t *s = mdns_priv_get_services();
    while (s) {
        if (mdns_utils_service_match(s->service, service, proto, hostname)) {
            return s;
        }
        s = s->next;
    }
    return NULL;
}

mdns_srv_item_t *mdns_utils_get_service_item_instance(const char *instance, const char *service, const char *proto,
                                                      const char *hostname)
{
    mdns_srv_item_t *s = mdns_priv_get_services();
    while (s) {
        if (instance) {
            if (mdns_utils_service_match_instance(s->service, instance, service, proto, hostname)) {
                return s;
            }
        } else {
            if (mdns_utils_service_match(s->service, service, proto, hostname)) {
                return s;
            }
        }
        s = s->next;
    }
    return NULL;
}

bool mdns_utils_service_match_instance(const mdns_service_t *srv, const char *instance, const char *service,
                                       const char *proto, const char *hostname)
{
    // service and proto must be supplied, if not this instance won't match
    if (!service || !proto) {
        return false;
    }
    // instance==NULL -> mdns_utils_instance_name_match() will check the default instance
    // hostname==NULL -> matches if instance, service and proto matches
    return !strcasecmp(srv->service, service) && mdns_utils_instance_name_match(srv->instance, instance) &&
           !strcasecmp(srv->proto, proto) && (mdns_utils_str_null_or_empty(hostname) || !strcasecmp(srv->hostname, hostname));
}

static const char *get_default_instance_name(void)
{
    const char* instance = mdns_priv_get_instance();
    if (!mdns_utils_str_null_or_empty(instance)) {
        return instance;
    }

    const char* host = mdns_priv_get_global_hostname();
    if (!mdns_utils_str_null_or_empty(host)) {
        return host;
    }
    return NULL;
}

/**
 * @brief  Get the service name of a service
 */
const char *mdns_utils_get_service_instance_name(const mdns_service_t *service)
{
    if (service && !mdns_utils_str_null_or_empty(service->instance)) {
        return service->instance;
    }

    return get_default_instance_name();
}


bool mdns_utils_instance_name_match(const char *lhs, const char *rhs)
{
    if (lhs == NULL) {
        lhs = get_default_instance_name();
    }
    if (rhs == NULL) {
        rhs = get_default_instance_name();
    }
    return !strcasecmp(lhs, rhs);
}


mdns_ip_addr_t *mdns_utils_copy_address_list(const mdns_ip_addr_t *address_list)
{
    mdns_ip_addr_t *head = NULL;
    mdns_ip_addr_t *tail = NULL;
    while (address_list != NULL) {
        mdns_ip_addr_t *addr = (mdns_ip_addr_t *)mdns_mem_malloc(sizeof(mdns_ip_addr_t));
        if (addr == NULL) {
            HOOK_MALLOC_FAILED;
            mdns_utils_free_address_list(head);
            return NULL;
        }
        addr->addr = address_list->addr;
        addr->next = NULL;
        if (head == NULL) {
            head = addr;
            tail = addr;
        } else {
            tail->next = addr;
            tail = tail->next;
        }
        address_list = address_list->next;
    }
    return head;
}

void mdns_utils_free_address_list(mdns_ip_addr_t *address_list)
{
    while (address_list != NULL) {
        mdns_ip_addr_t *next = address_list->next;
        mdns_mem_free(address_list);
        address_list = next;
    }
}

#ifdef CONFIG_LWIP_IPV6
bool mdns_utils_ipv6_address_is_zero(esp_ip6_addr_t ip6)
{
    uint8_t i;
    uint8_t *data = (uint8_t *)ip6.addr;
    for (i = 0; i < MDNS_UTILS_SIZEOF_IP6_ADDR; i++) {
        if (data[i]) {
            return false;
        }
    }
    return true;
}
#endif /* CONFIG_LWIP_IPV6 */

uint8_t mdns_utils_append_u16(uint8_t *packet, uint16_t *index, uint16_t value)
{
    if ((*index + 1) >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    mdns_utils_append_u8(packet, index, (value >> 8) & 0xFF);
    mdns_utils_append_u8(packet, index, value & 0xFF);
    return 2;
}
