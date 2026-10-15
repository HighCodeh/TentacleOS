/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "sdkconfig.h"
#include "mdns_private.h"
#include "mdns_send.h"
#include "mdns_utils.h"
#include "mdns_networking.h"
#include "mdns_debug.h"
#include "mdns_mem_caps.h"
#include "esp_log.h"
#include "mdns_debug.h"
#include "mdns_netif.h"
#include "mdns_pcb.h"
#include "mdns_responder.h"
#include "mdns_service.h"

static const char *TAG = "mdns_send";
static const char *MDNS_SUB_STR = "_sub";

static mdns_tx_packet_t *s_tx_queue_head;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

/**
 * @brief  appends uint32_t in a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  value        the value to set
 *
 * @return length of added data: 0 on error or 4 on success
 */
static inline uint8_t append_u32(uint8_t *packet, uint16_t *index, uint32_t value)
{
    if ((*index + sizeof(uint32_t)) >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    mdns_utils_append_u8(packet, index, (value >> 24) & 0xFF);
    mdns_utils_append_u8(packet, index, (value >> 16) & 0xFF);
    mdns_utils_append_u8(packet, index, (value >> 8) & 0xFF);
    mdns_utils_append_u8(packet, index, value & 0xFF);
    return sizeof(uint32_t);
}

/**
 * @brief  appends answer type, class, ttl and data length to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  type         answer type
 * @param  ttl          answer ttl
 *
 * @return length of added data: 0 on error or 10 on success
 */
static inline uint8_t append_type(uint8_t *packet, uint16_t *index, uint8_t type, bool flush, uint32_t ttl)
{
    const size_t len = sizeof(uint16_t) * 2 + sizeof(uint32_t) + sizeof(uint16_t);
    if ((*index + len) >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    uint16_t mdns_class = MDNS_CLASS_IN;
    if (flush) {
        mdns_class = MDNS_CLASS_IN_FLUSH_CACHE;
    }
    if (type == MDNS_ANSWER_PTR) {
        mdns_utils_append_u16(packet, index, MDNS_TYPE_PTR);
        mdns_utils_append_u16(packet, index, mdns_class);
    } else if (type == MDNS_ANSWER_TXT) {
        mdns_utils_append_u16(packet, index, MDNS_TYPE_TXT);
        mdns_utils_append_u16(packet, index, mdns_class);
    } else if (type == MDNS_ANSWER_SRV) {
        mdns_utils_append_u16(packet, index, MDNS_TYPE_SRV);
        mdns_utils_append_u16(packet, index, mdns_class);
    } else if (type == MDNS_ANSWER_A) {
        mdns_utils_append_u16(packet, index, MDNS_TYPE_A);
        mdns_utils_append_u16(packet, index, mdns_class);
    } else if (type == MDNS_ANSWER_AAAA) {
        mdns_utils_append_u16(packet, index, MDNS_TYPE_AAAA);
        mdns_utils_append_u16(packet, index, mdns_class);
    } else {
        return 0;
    }
    append_u32(packet, index, ttl);
    mdns_utils_append_u16(packet, index, 0);
    return len;
}

/**
 * @brief  appends single string to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  string       the string to append
 *
 * @return length of added data: 0 on error or length of the string + 1 on success
 */
static inline uint8_t append_string(uint8_t *packet, uint16_t *index, const char *string)
{
    uint8_t len = strlen(string);
    if ((*index + len + 1) >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    mdns_utils_append_u8(packet, index, len);
    memcpy(packet + *index, string, len);
    *index += len;
    return len + 1;
}

int mdns_priv_append_one_txt_record_entry(uint8_t *packet, uint16_t *index, mdns_txt_linked_item_t *txt)
{
    if (txt == NULL || txt->key == NULL) {
        return -1;
    }
    size_t key_len = strlen(txt->key);
    size_t len = key_len + txt->value_len + (txt->value ? 1 : 0);
    if ((*index + len + 1) >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    mdns_utils_append_u8(packet, index, len);
    memcpy(packet + *index, txt->key, key_len);
    if (txt->value) {
        packet[*index + key_len] = '=';
        memcpy(packet + *index + key_len + 1, txt->value, txt->value_len);
    }
    *index += len;
    return len + 1;
}

static uint16_t append_fqdn(uint8_t *packet, uint16_t *index, const char *strings[], uint8_t count, size_t packet_len);

/**
 * @brief  sets uint16_t value in a packet
 *
 * @param  packet       MDNS packet
 * @param  index        offset of uint16_t value
 * @param  value        the value to set
 */
static inline void set_u16(uint8_t *packet, uint16_t index, uint16_t value)
{
    if ((index + 1) >= MDNS_MAX_PACKET_SIZE) {
        return;
    }
    packet[index] = (value >> 8) & 0xFF;
    packet[index + 1] = value & 0xFF;
}

/**
 * @brief  Allocate new packet for sending
 */
mdns_tx_packet_t *mdns_priv_alloc_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    mdns_tx_packet_t *packet = (mdns_tx_packet_t *)mdns_mem_malloc(sizeof(mdns_tx_packet_t));
    if (!packet) {
        HOOK_MALLOC_FAILED;
        return NULL;
    }
    memset((uint8_t *)packet, 0, sizeof(mdns_tx_packet_t));
    packet->tcpip_if = tcpip_if;
    packet->ip_protocol = ip_protocol;
    packet->port = MDNS_SERVICE_PORT;
#ifdef CONFIG_LWIP_IPV4
    if (ip_protocol == MDNS_IP_PROTOCOL_V4) {
        esp_ip_addr_t addr = ESP_IP4ADDR_INIT(224, 0, 0, 251);
        memcpy(&packet->dst, &addr, sizeof(esp_ip_addr_t));
    }
#endif
#ifdef CONFIG_LWIP_IPV6
    if (ip_protocol == MDNS_IP_PROTOCOL_V6) {
        esp_ip_addr_t addr = ESP_IP6ADDR_INIT(0x000002ff, 0, 0, 0xfb000000);
        memcpy(&packet->dst, &addr, sizeof(esp_ip_addr_t));
    }
#endif
    return packet;
}

/**
 * @brief  frees a packet
 *
 * @param  packet       the packet
 */
void mdns_priv_free_tx_packet(mdns_tx_packet_t *packet)
{
    if (!packet) {
        return;
    }
    mdns_out_question_t *q = packet->questions;
    while (q) {
        mdns_out_question_t *next = q->next;
        if (q->own_dynamic_memory) {
            mdns_mem_free((char *)q->host);
            mdns_mem_free((char *)q->service);
            mdns_mem_free((char *)q->proto);
            mdns_mem_free((char *)q->domain);
        }
        mdns_mem_free(q);
        q = next;
    }
    queueFree(mdns_out_answer_t, packet->answers);
    queueFree(mdns_out_answer_t, packet->servers);
    queueFree(mdns_out_answer_t, packet->additional);
    mdns_mem_free(packet);
}

/**
 * @brief  Allocate new answer and add it to answer list (destination)
 */
bool mdns_priv_create_answer(mdns_out_answer_t **destination, uint16_t type, mdns_service_t *service,
                             mdns_host_item_t *host, bool flush, bool bye)
{
    mdns_out_answer_t *d = *destination;
    while (d) {
        if (d->type == type && d->service == service && d->host == host) {
            return true;
        }
        d = d->next;
    }

    mdns_out_answer_t *a = (mdns_out_answer_t *)mdns_mem_malloc(sizeof(mdns_out_answer_t));
    if (!a) {
        HOOK_MALLOC_FAILED;
        return false;
    }
    a->type = type;
    a->service = service;
    a->host = host;
    a->custom_service = NULL;
    a->bye = bye;
    a->flush = flush;
    a->next = NULL;
    queueToEnd(mdns_out_answer_t, *destination, a);
    return true;
}

static mdns_host_item_t *get_host_item(const char *hostname)
{
    if (hostname == NULL || strcasecmp(hostname, mdns_priv_get_global_hostname()) == 0) {
        return mdns_priv_get_self_host();
    }
    mdns_host_item_t *host = mdns_priv_get_hosts();
    while (host != NULL) {
        if (strcasecmp(host->hostname, hostname) == 0) {
            return host;
        }
        host = host->next;
    }
    return NULL;
}

static bool create_answer_from_service(mdns_tx_packet_t *packet, mdns_service_t *service,
                                       mdns_parsed_question_t *question, bool shared, bool send_flush)
{
    mdns_host_item_t *host = get_host_item(service->hostname);
    bool is_delegated = (host != mdns_priv_get_self_host());
    bool is_instance_question = !mdns_utils_str_null_or_empty(question->host);
    if ((question->type == MDNS_TYPE_ANY) && is_instance_question) {
        // Instance-scoped ANY queries expect the resolved data (SRV/TXT) to be
        // part of the answer section so that queriers can stop searching as soon
        // as they receive this packet (RFC6762 section 6, RFC6763 section 12.1).
        if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_SRV, service, NULL, send_flush, false) ||
                !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_TXT, service, NULL, send_flush, false) ||
                !mdns_priv_create_answer(&packet->additional, MDNS_TYPE_A, service, host, send_flush, false) ||
                !mdns_priv_create_answer(&packet->additional, MDNS_TYPE_AAAA, service, host, send_flush, false)) {
            return false;
        }
    } else if (question->type == MDNS_TYPE_PTR || (question->type == MDNS_TYPE_ANY && !is_instance_question)) {
        // For service discovery (PTR or wildcard ANY) RFC6763 section 12.1
        // requires SRV/TXT/address records to stay in the additional section.
        if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_PTR, service, NULL, false, false) ||
                !mdns_priv_create_answer(&packet->additional, MDNS_TYPE_SRV, service, NULL, send_flush, false) ||
                !mdns_priv_create_answer(&packet->additional, MDNS_TYPE_TXT, service, NULL, send_flush, false) ||
                !mdns_priv_create_answer((shared || is_delegated) ? &packet->additional : &packet->answers, MDNS_TYPE_A,
                                         service, host, send_flush,
                                         false) ||
                !mdns_priv_create_answer((shared || is_delegated) ? &packet->additional : &packet->answers,
                                         MDNS_TYPE_AAAA, service, host,
                                         send_flush, false)) {
            return false;
        }
    } else if (question->type == MDNS_TYPE_SRV) {
        if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_SRV, service, NULL, send_flush, false) ||
                !mdns_priv_create_answer(&packet->additional, MDNS_TYPE_A, service, host, send_flush, false) ||
                !mdns_priv_create_answer(&packet->additional, MDNS_TYPE_AAAA, service, host, send_flush, false)) {
            return false;
        }
    } else if (question->type == MDNS_TYPE_TXT) {
        if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_TXT, service, NULL, send_flush, false)) {
            return false;
        }
    } else if (question->type == MDNS_TYPE_SDPTR) {
        if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_SDPTR, service, NULL, false, false)) {
            return false;
        }
    }
    return true;
}

static bool create_answer_from_hostname(mdns_tx_packet_t *packet, const char *hostname, bool send_flush)
{
    mdns_host_item_t *host = get_host_item(hostname);
    if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_A, NULL, host, send_flush, false) ||
            !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_AAAA, NULL, host, send_flush, false)) {
        return false;
    }
    return true;
}

static bool service_match_ptr_question(const mdns_service_t *service, const mdns_parsed_question_t *question)
{
    if (!mdns_utils_service_match(service, question->service, question->proto, NULL)) {
        return false;
    }
    // The question parser stores anything before _type._proto in question->host
    // So the question->host can be subtype or instance name based on its content
    if (question->sub) {
        mdns_subtype_t *subtype = service->subtype;
        while (subtype) {
            if (!strcasecmp(subtype->subtype, question->host)) {
                return true;
            }
            subtype = subtype->next;
        }
        return false;
    }
    if (question->host) {
        if (strcasecmp(mdns_utils_get_service_instance_name(service), question->host) != 0) {
            return false;
        }
    }
    return true;
}
static bool append_host(mdns_out_answer_t **destination, mdns_host_item_t *host, bool flush, bool bye)
{
    if (!mdns_priv_create_answer(destination, MDNS_TYPE_A, NULL, host, flush, bye)) {
        return false;
    }
    if (!mdns_priv_create_answer(destination, MDNS_TYPE_AAAA, NULL, host, flush, bye)) {
        return false;
    }
    return true;
}

bool mdns_priv_append_host_list_in_services(mdns_out_answer_t **destination, mdns_srv_item_t *services[],
                                            size_t services_len, bool flush, bool bye)
{
    if (services == NULL) {
        mdns_host_item_t *host = get_host_item(mdns_priv_get_global_hostname());
        if (host != NULL) {
            return append_host(destination, host, flush, bye);
        }
        return true;
    }
    for (size_t i = 0; i < services_len; i++) {
        mdns_host_item_t *host = get_host_item(services[i]->service->hostname);
        if (!append_host(destination, host, flush, bye)) {
            return false;
        }
    }
    return true;
}

static bool append_host_list(mdns_out_answer_t **destination, bool flush, bool bye)
{
    if (!mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname())) {
        mdns_host_item_t *self_host = get_host_item(mdns_priv_get_global_hostname());
        if (!append_host(destination, self_host, flush, bye)) {
            return false;
        }
    }
    mdns_host_item_t *host = mdns_priv_get_hosts();
    while (host != NULL) {
        host = host->next;
        if (!append_host(destination, host, flush, bye)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief  Check if question is already in the list
 */
static bool question_exists(mdns_out_question_t *needle, mdns_out_question_t *haystack)
{
    while (haystack) {
        if (haystack->type == needle->type
                && haystack->host == needle->host
                && haystack->service == needle->service
                && haystack->proto == needle->proto) {
            return true;
        }
        haystack = haystack->next;
    }
    return false;
}

static bool append_host_question(mdns_out_question_t **questions, const char *hostname, bool unicast)
{
    mdns_out_question_t *q = (mdns_out_question_t *)mdns_mem_malloc(sizeof(mdns_out_question_t));
    if (!q) {
        HOOK_MALLOC_FAILED;
        return false;
    }
    q->next = NULL;
    q->unicast = unicast;
    q->type = MDNS_TYPE_ANY;
    q->host = hostname;
    q->service = NULL;
    q->proto = NULL;
    q->domain = MDNS_UTILS_DEFAULT_DOMAIN;
    q->own_dynamic_memory = false;
    if (question_exists(q, *questions)) {
        mdns_mem_free(q);
    } else {
        queueToEnd(mdns_out_question_t, *questions, q);
    }
    return true;
}

static bool append_host_questions_for_services(mdns_out_question_t **questions, mdns_srv_item_t *services[],
                                               size_t len, bool unicast)
{
    if (!mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname()) &&
            !append_host_question(questions, mdns_priv_get_global_hostname(), unicast)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!append_host_question(questions, services[i]->service->hostname, unicast)) {
            return false;
        }
    }
    return true;
}

#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
static inline int append_single_str(uint8_t *packet, uint16_t *index, const char *str, int len)
{
    if ((*index + len + 1) >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    if (!mdns_utils_append_u8(packet, index, len)) {
        return 0;
    }
    memcpy(packet + *index, str, len);
    *index += len;
    return *index;
}

/**
 * @brief  appends FQDN to a packet from hostname separated by dots. This API works the same way as
 * append_fqdn(), but refrains from DNS compression (as it's mainly used for IP addresses (many short items),
 * where we gain very little (or compression even gets counter-productive mainly for IPv6 addresses)
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  name         name representing FQDN in '.' separated parts
 * @param  last         true if appending the last part (domain, typically "arpa")
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_fqdn_dots(uint8_t *packet, uint16_t *index, const char *name, bool last)
{
    int len = strlen(name);
    char *host = (char *)name;
    char *end = host;
    char *start = host;
    do  {
        end = memchr(start, '.', host + len - start);
        end = end ? end : host + len;
        int part_len = end - start;
        if (!append_single_str(packet, index, start, part_len)) {
            return 0;
        }
        start = ++end;
    } while (end < name + len);

    if (!append_single_str(packet, index, "arpa", sizeof("arpa") - 1)) {
        return 0;
    }

    //empty string so terminate
    if (!mdns_utils_append_u8(packet, index, 0)) {
        return 0;
    }
    return *index;
}

/**
 * @brief Appends reverse lookup PTR record
 */
static uint8_t append_reverse_ptr_record(uint8_t *packet, uint16_t *index, const char *name)
{
    if (strstr(name, "in-addr") == NULL && strstr(name, "ip6") == NULL) {
        return 0;
    }

    if (!append_fqdn_dots(packet, index, name, false)) {
        return 0;
    }

    if (!append_type(packet, index, MDNS_ANSWER_PTR, false, 10 /* TTL set to 10s*/)) {
        return 0;
    }

    uint16_t data_len_location = *index - 2; /* store the position of size (2=16bis) of this record */
    const char *str[2] = {mdns_priv_get_self_host()->hostname, MDNS_UTILS_DEFAULT_DOMAIN };

    int part_length = append_fqdn(packet, index, str, 2, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }

    set_u16(packet, data_len_location, part_length);
    return 1; /* appending only 1 record */
}
#endif /* CONFIG_MDNS_RESPOND_REVERSE_QUERIES */

void mdns_priv_create_answer_from_parsed_packet(mdns_parsed_packet_t *parsed_packet)
{
    if (!parsed_packet->questions) {
        return;
    }
    bool send_flush = parsed_packet->src_port == MDNS_SERVICE_PORT;
    bool unicast = false;
    bool shared = false;
    mdns_tx_packet_t *packet = mdns_priv_alloc_packet(parsed_packet->tcpip_if, parsed_packet->ip_protocol);
    if (!packet) {
        return;
    }
    packet->flags = MDNS_FLAGS_QR_AUTHORITATIVE;
    packet->distributed = parsed_packet->distributed;
    packet->id = parsed_packet->id;

    mdns_parsed_question_t *q = parsed_packet->questions;
    uint32_t out_record_nums = 0;
    while (q) {
        shared = q->type == MDNS_TYPE_PTR || q->type == MDNS_TYPE_SDPTR || !parsed_packet->probe;
        if (q->type == MDNS_TYPE_SRV || q->type == MDNS_TYPE_TXT) {
            mdns_srv_item_t *service = mdns_utils_get_service_item_instance(q->host, q->service, q->proto, NULL);
            if (service == NULL) {  // Service not found, but we continue to the next question
                q = q->next;
                continue;
            }
            if (!create_answer_from_service(packet, service->service, q, shared, send_flush)) {
                mdns_priv_free_tx_packet(packet);
                return;
            } else {
                out_record_nums++;
            }
        } else if (q->service && q->proto) {
            mdns_srv_item_t *service = mdns_priv_get_services();
            while (service) {
                if (service_match_ptr_question(service->service, q)) {
                    mdns_parsed_record_t *r = parsed_packet->records;
                    bool is_record_exist = false;
                    while (r) {
                        if (service->service->instance && r->host) {
                            if (mdns_utils_service_match_instance(service->service, r->host, r->service, r->proto, NULL) && r->ttl > (MDNS_ANSWER_PTR_TTL / 2)) {
                                is_record_exist = true;
                                break;
                            }
                        } else if (!service->service->instance && !r->host) {
                            if (mdns_utils_service_match(service->service, r->service, r->proto, NULL) && r->ttl > (MDNS_ANSWER_PTR_TTL / 2)) {
                                is_record_exist = true;
                                break;
                            }
                        }
                        r = r->next;
                    }
                    if (!is_record_exist) {
                        if (!create_answer_from_service(packet, service->service, q, shared, send_flush)) {
                            mdns_priv_free_tx_packet(packet);
                            return;
                        } else {
                            out_record_nums++;
                        }
                    }
                }
                service = service->next;
            }
        } else if (q->type == MDNS_TYPE_A || q->type == MDNS_TYPE_AAAA) {
            if (!create_answer_from_hostname(packet, q->host, send_flush)) {
                mdns_priv_free_tx_packet(packet);
                return;
            } else {
                out_record_nums++;
            }
        } else if (q->type == MDNS_TYPE_ANY) {
            if (!append_host_list(&packet->answers, send_flush, false)) {
                mdns_priv_free_tx_packet(packet);
                return;
            } else {
                out_record_nums++;
            }
#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
        } else if (q->type == MDNS_TYPE_PTR) {
            mdns_host_item_t *host = get_host_item(q->host);
            if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_PTR, NULL, host, send_flush, false)) {
                mdns_priv_free_tx_packet(packet);
                return;
            } else {
                out_record_nums++;
            }
#endif /* CONFIG_MDNS_RESPOND_REVERSE_QUERIES */
        } else if (!mdns_priv_create_answer(&packet->answers, q->type, NULL, NULL, send_flush, false)) {
            mdns_priv_free_tx_packet(packet);
            return;
        } else {
            out_record_nums++;
        }

        if (parsed_packet->src_port != MDNS_SERVICE_PORT &&  // Repeat the queries only for "One-Shot mDNS queries"
                (q->type == MDNS_TYPE_ANY || q->type == MDNS_TYPE_A || q->type == MDNS_TYPE_AAAA
#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
                 || q->type == MDNS_TYPE_PTR
#endif /* CONFIG_MDNS_RESPOND_REVERSE_QUERIES */
                )) {
            mdns_out_question_t *out_question = mdns_mem_malloc(sizeof(mdns_out_question_t));
            if (out_question == NULL) {
                HOOK_MALLOC_FAILED;
                mdns_priv_free_tx_packet(packet);
                return;
            }
            out_question->type = q->type;
            out_question->unicast = q->unicast;
            out_question->host = q->host;
            q->host = NULL;
            out_question->service = q->service;
            q->service = NULL;
            out_question->proto = q->proto;
            q->proto = NULL;
            out_question->domain = q->domain;
            q->domain = NULL;
            out_question->next = NULL;
            out_question->own_dynamic_memory = true;
            queueToEnd(mdns_out_question_t, packet->questions, out_question);
        }
        if (q->unicast) {
            unicast = true;
        }
        q = q->next;
    }
    if (out_record_nums == 0) {
        mdns_priv_free_tx_packet(packet);
        return;
    }
    if (unicast || !send_flush) {
        memcpy(&packet->dst, &parsed_packet->src, sizeof(esp_ip_addr_t));
        packet->port = parsed_packet->src_port;
    }

    static uint8_t share_step = 0;
    if (shared) {
        mdns_priv_send_after(packet, 25 + (share_step * 25));
        share_step = (share_step + 1) & 0x03;
    } else {
        mdns_priv_dispatch_tx_packet(packet);
        mdns_priv_free_tx_packet(packet);
    }
}

/**
 * @brief  appends FQDN to a packet, incrementing the index and
 *         compressing the output if previous occurrence of the string (or part of it) has been found
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  strings      string array containing the parts of the FQDN
 * @param  count        number of strings in the array
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_fqdn(uint8_t *packet, uint16_t *index, const char *strings[], uint8_t count, size_t packet_len)
{
    if (!count) {
        // empty string so terminate
        return mdns_utils_append_u8(packet, index, 0);
    }
    static char buf[MDNS_NAME_BUF_LEN];
    uint8_t len = strlen(strings[0]);
    // try to find first the string length in the packet (if it exists)
    uint8_t *len_location = (uint8_t *)memchr(packet, (char)len, *index);
    while (len_location) {
        mdns_name_t name;
        // check if the string after len_location is the string that we are looking for
        if (memcmp(len_location + 1, strings[0], len)) { //not continuing with our string
search_next:
            // try and find the length byte further in the packet
            len_location = (uint8_t *)memchr(len_location + 1, (char)len, *index - (len_location + 1 - packet));
            continue;
        }
        // seems that we might have found the string that we are looking for
        // read the destination into name and compare
        name.parts = 0;
        name.sub = 0;
        name.invalid = false;
        name.host[0] = 0;
        name.service[0] = 0;
        name.proto[0] = 0;
        name.domain[0] = 0;
        const uint8_t *content = mdns_utils_read_fqdn(packet, len_location, &name, buf, packet_len);
        if (!content) {
            // not a readable fqdn?
            goto search_next; // could be our unfinished fqdn, continue searching
        }
        if (name.parts == count) {
            uint8_t i;
            for (i = 0; i < count; i++) {
                if (strcasecmp(strings[i], (const char *)&name + (i * (MDNS_NAME_BUF_LEN)))) {
                    // not our string! let's search more
                    goto search_next;
                }
            }
            // we actually have found the string
            break;
        } else {
            goto search_next;
        }
    }
    // string is not yet in the packet, so let's add it
    if (!len_location) {
        uint8_t written = append_string(packet, index, strings[0]);
        if (!written) {
            return 0;
        }
        // run the same for the other strings in the name
        return written + append_fqdn(packet, index, &strings[1], count - 1, packet_len);
    }

    // we have found the string so let's insert a pointer to it instead
    uint16_t offset = len_location - packet;
    offset |= MDNS_NAME_REF;
    return mdns_utils_append_u16(packet, index, offset);
}


/**
 * @brief  Append question to packet
 */
static uint16_t append_question(uint8_t *packet, uint16_t *index, mdns_out_question_t *q)
{
    uint8_t part_length;
#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
    if (q->host && (strstr(q->host, "in-addr") || strstr(q->host, "ip6"))) {
        part_length = append_fqdn_dots(packet, index, q->host, false);
        if (!part_length) {
            return 0;
        }
    } else
#endif /* CONFIG_MDNS_RESPOND_REVERSE_QUERIES */
    {
        const char *str[4];
        uint8_t str_index = 0;
        if (q->host) {
            str[str_index++] = q->host;
        }
        if (q->service) {
            str[str_index++] = q->service;
        }
        if (q->proto) {
            str[str_index++] = q->proto;
        }
        if (q->domain) {
            str[str_index++] = q->domain;
        }
        part_length = append_fqdn(packet, index, str, str_index, MDNS_MAX_PACKET_SIZE);
        if (!part_length) {
            return 0;
        }
    }

    part_length += mdns_utils_append_u16(packet, index, q->type);
    part_length += mdns_utils_append_u16(packet, index, q->unicast ? 0x8001 : 0x0001);
    return part_length;
}

/**
 * @brief  appends PTR record for service to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  server       the server that is hosting the service
 * @param  service      the service to add record for
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_ptr_record(uint8_t *packet, uint16_t *index, const char *instance, const char *service, const char *proto, bool flush, bool bye)
{
    const char *str[4];
    uint16_t record_length = 0;
    uint8_t part_length;

    if (service == NULL) {
        return 0;
    }

    str[0] = instance;
    str[1] = service;
    str[2] = proto;
    str[3] = MDNS_UTILS_DEFAULT_DOMAIN;

    part_length = append_fqdn(packet, index, str + 1, 3, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    part_length = append_type(packet, index, MDNS_ANSWER_PTR, false, bye ? 0 : MDNS_ANSWER_PTR_TTL);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    uint16_t data_len_location = *index - 2;
    part_length = append_fqdn(packet, index, str, 4, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    set_u16(packet, data_len_location, part_length);
    record_length += part_length;
    return record_length;
}

/**
 * @brief  appends PTR record for a subtype to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  instance     the service instance name
 * @param  subtype      the service subtype
 * @param  proto        the service protocol
 * @param  flush        whether to set the flush flag
 * @param  bye          whether to set the bye flag
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_subtype_ptr_record(uint8_t *packet, uint16_t *index, const char *instance,
                                          const char *subtype, const char *service, const char *proto, bool flush,
                                          bool bye)
{
    const char *subtype_str[5] = {subtype, MDNS_SUB_STR, service, proto, MDNS_UTILS_DEFAULT_DOMAIN};
    const char *instance_str[4] = {instance, service, proto, MDNS_UTILS_DEFAULT_DOMAIN};
    uint16_t record_length = 0;
    uint8_t part_length;

    if (service == NULL) {
        return 0;
    }

    part_length = append_fqdn(packet, index, subtype_str, ARRAY_SIZE(subtype_str), MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    part_length = append_type(packet, index, MDNS_ANSWER_PTR, false, bye ? 0 : MDNS_ANSWER_PTR_TTL);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    uint16_t data_len_location = *index - 2;
    part_length = append_fqdn(packet, index, instance_str, ARRAY_SIZE(instance_str), MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    set_u16(packet, data_len_location, part_length);
    record_length += part_length;
    return record_length;
}
/**
 * @brief  Append PTR answers to packet
 *
 *  @return number of answers added to the packet
 */
static uint8_t append_service_ptr_answers(uint8_t *packet, uint16_t *index, mdns_service_t *service, bool flush,
                                          bool bye)
{
    uint8_t appended_answers = 0;

    if (append_ptr_record(packet, index, mdns_utils_get_service_instance_name(service), service->service,
                          service->proto, flush, bye) <= 0) {
        return appended_answers;
    }
    appended_answers++;

    mdns_subtype_t *subtype = service->subtype;
    while (subtype) {
        appended_answers +=
            (append_subtype_ptr_record(packet, index, mdns_utils_get_service_instance_name(service), subtype->subtype,
                                       service->service, service->proto, flush, bye) > 0);
        subtype = subtype->next;
    }

    return appended_answers;
}

/**
 * @brief  appends SRV record for service to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  server       the server that is hosting the service
 * @param  service      the service to add record for
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_srv_record(uint8_t *packet, uint16_t *index, mdns_service_t *service, bool flush, bool bye)
{
    const char *str[4];
    uint16_t record_length = 0;
    uint8_t part_length;

    if (service == NULL) {
        return 0;
    }

    str[0] = mdns_utils_get_service_instance_name(service);
    str[1] = service->service;
    str[2] = service->proto;
    str[3] = MDNS_UTILS_DEFAULT_DOMAIN;

    if (!str[0]) {
        return 0;
    }

    part_length = append_fqdn(packet, index, str, 4, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    part_length = append_type(packet, index, MDNS_ANSWER_SRV, flush, bye ? 0 : MDNS_ANSWER_SRV_TTL);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    uint16_t data_len_location = *index - 2;

    part_length = 0;
    part_length += mdns_utils_append_u16(packet, index, service->priority);
    part_length += mdns_utils_append_u16(packet, index, service->weight);
    part_length += mdns_utils_append_u16(packet, index, service->port);
    if (part_length != 6) {
        return 0;
    }

    if (service->hostname) {
        str[0] = service->hostname;
    } else {
        str[0] = mdns_priv_get_global_hostname();
    }
    str[1] = MDNS_UTILS_DEFAULT_DOMAIN;

    if (mdns_utils_str_null_or_empty(str[0])) {
        return 0;
    }

    part_length = append_fqdn(packet, index, str, 2, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    set_u16(packet, data_len_location, part_length + 6);

    record_length += part_length + 6;
    return record_length;
}

/**
 * @brief  appends TXT record for service to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  server       the server that is hosting the service
 * @param  service      the service to add record for
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_txt_record(uint8_t *packet, uint16_t *index, mdns_service_t *service, bool flush, bool bye)
{
    const char *str[4];
    uint16_t record_length = 0;
    uint8_t part_length;

    if (service == NULL) {
        return 0;
    }

    str[0] = mdns_utils_get_service_instance_name(service);
    str[1] = service->service;
    str[2] = service->proto;
    str[3] = MDNS_UTILS_DEFAULT_DOMAIN;

    if (!str[0]) {
        return 0;
    }

    part_length = append_fqdn(packet, index, str, 4, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    part_length = append_type(packet, index, MDNS_ANSWER_TXT, flush, bye ? 0 : MDNS_ANSWER_TXT_TTL);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    uint16_t data_len_location = *index - 2;
    uint16_t data_len = 0;

    mdns_txt_linked_item_t *txt = service->txt;
    while (txt) {
        int l = mdns_priv_append_one_txt_record_entry(packet, index, txt);
        if (l > 0) {
            data_len += l;
        } else if (l == 0) { // TXT entry won't fit into the mdns packet
            return 0;
        }
        txt = txt->next;
    }
    if (!data_len) {
        data_len = 1;
        packet[*index] = 0;
        *index = *index + 1;
    }
    set_u16(packet, data_len_location, data_len);
    record_length += data_len;
    return record_length;
}

/**
 * @brief  appends DNS-SD PTR record for service to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  server       the server that is hosting the service
 * @param  service      the service to add record for
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_sdptr_record(uint8_t *packet, uint16_t *index, mdns_service_t *service, bool flush, bool bye)
{
    const char *str[3];
    const char *sd_str[4];
    uint16_t record_length = 0;
    uint8_t part_length;

    if (service == NULL) {
        return 0;
    }

    sd_str[0] = (char *)"_services";
    sd_str[1] = (char *)"_dns-sd";
    sd_str[2] = (char *)"_udp";
    sd_str[3] = MDNS_UTILS_DEFAULT_DOMAIN;

    str[0] = service->service;
    str[1] = service->proto;
    str[2] = MDNS_UTILS_DEFAULT_DOMAIN;

    part_length = append_fqdn(packet, index, sd_str, 4, MDNS_MAX_PACKET_SIZE);

    record_length += part_length;

    part_length = append_type(packet, index, MDNS_ANSWER_PTR, flush, MDNS_ANSWER_PTR_TTL);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    uint16_t data_len_location = *index - 2;
    part_length = append_fqdn(packet, index, str, 3, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    set_u16(packet, data_len_location, part_length);
    record_length += part_length;
    return record_length;
}

#ifdef CONFIG_LWIP_IPV4
/**
 * @brief  appends A record to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  hostname     the hostname address to add
 * @param  ip           the IP address to add
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_a_record(uint8_t *packet, uint16_t *index, const char *hostname, uint32_t ip, bool flush, bool bye)
{
    const char *str[2];
    uint16_t record_length = 0;
    uint8_t part_length;

    str[0] = hostname;
    str[1] = MDNS_UTILS_DEFAULT_DOMAIN;

    if (mdns_utils_str_null_or_empty(str[0])) {
        return 0;
    }

    part_length = append_fqdn(packet, index, str, 2, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    part_length = append_type(packet, index, MDNS_ANSWER_A, flush, bye ? 0 : MDNS_ANSWER_A_TTL);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    uint16_t data_len_location = *index - 2;

    if ((*index + 3) >= MDNS_MAX_PACKET_SIZE) {
        return 0;
    }
    mdns_utils_append_u8(packet, index, ip & 0xFF);
    mdns_utils_append_u8(packet, index, (ip >> 8) & 0xFF);
    mdns_utils_append_u8(packet, index, (ip >> 16) & 0xFF);
    mdns_utils_append_u8(packet, index, (ip >> 24) & 0xFF);
    set_u16(packet, data_len_location, 4);

    record_length += 4;
    return record_length;
}
#endif /* CONFIG_LWIP_IPV4 */

#ifdef CONFIG_LWIP_IPV6
/**
 * @brief  appends AAAA record to a packet, incrementing the index
 *
 * @param  packet       MDNS packet
 * @param  index        offset in the packet
 * @param  hostname     the hostname address to add
 * @param  ipv6         the IPv6 address to add
 *
 * @return length of added data: 0 on error or length on success
 */
static uint16_t append_aaaa_record(uint8_t *packet, uint16_t *index, const char *hostname, uint8_t *ipv6, bool flush, bool bye)
{
    const char *str[2];
    uint16_t record_length = 0;
    uint8_t part_length;

    str[0] = hostname;
    str[1] = MDNS_UTILS_DEFAULT_DOMAIN;

    if (mdns_utils_str_null_or_empty(str[0])) {
        return 0;
    }


    part_length = append_fqdn(packet, index, str, 2, MDNS_MAX_PACKET_SIZE);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    part_length = append_type(packet, index, MDNS_ANSWER_AAAA, flush, bye ? 0 : MDNS_ANSWER_AAAA_TTL);
    if (!part_length) {
        return 0;
    }
    record_length += part_length;

    uint16_t data_len_location = *index - 2;

    if ((*index + MDNS_ANSWER_AAAA_SIZE) > MDNS_MAX_PACKET_SIZE) {
        return 0;
    }

    part_length = MDNS_ANSWER_AAAA_SIZE;
    memcpy(packet + *index, ipv6, part_length);
    *index += part_length;
    set_u16(packet, data_len_location, part_length);
    record_length += part_length;
    return record_length;
}
#endif

static uint8_t append_host_answer(uint8_t *packet, uint16_t *index, mdns_host_item_t *host,
                                  uint8_t address_type, bool flush, bool bye)
{
    mdns_ip_addr_t *addr = host->address_list;
    uint8_t num_records = 0;

    while (addr != NULL) {
        if (addr->addr.type == address_type) {
#ifdef CONFIG_LWIP_IPV4
            if (address_type == ESP_IPADDR_TYPE_V4 &&
                    append_a_record(packet, index, host->hostname, addr->addr.u_addr.ip4.addr, flush, bye) <= 0) {
                break;
            }
#endif /* CONFIG_LWIP_IPV4 */
#ifdef CONFIG_LWIP_IPV6
            if (address_type == ESP_IPADDR_TYPE_V6 &&
                    append_aaaa_record(packet, index, host->hostname, (uint8_t *)addr->addr.u_addr.ip6.addr, flush,
                                       bye) <= 0) {
                break;
            }
#endif /* CONFIG_LWIP_IPV6 */
            num_records++;
        }
        addr = addr->next;
    }
    return num_records;
}

/**
 * @brief  Append answer to packet
 *
 *  @return number of answers added to the packet
 */
static uint8_t append_answer(uint8_t *packet, uint16_t *index, mdns_out_answer_t *answer, mdns_if_t tcpip_if)
{
    if (answer->host) {
        bool is_host_valid = (mdns_priv_get_self_host() == answer->host);
        mdns_host_item_t *target_host = mdns_priv_get_hosts();
        while (target_host && !is_host_valid) {
            if (target_host == answer->host) {
                is_host_valid = true;
            }
            target_host = target_host->next;
        }
        if (!is_host_valid) {
            return 0;
        }
    }

    if (answer->type == MDNS_TYPE_PTR) {
        if (answer->service) {
            return append_service_ptr_answers(packet, index, answer->service, answer->flush, answer->bye);
#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
        } else if (answer->host && answer->host->hostname &&
                   (strstr(answer->host->hostname, "in-addr") || strstr(answer->host->hostname, "ip6"))) {
            return append_reverse_ptr_record(packet, index, answer->host->hostname) > 0;
#endif /* CONFIG_MDNS_RESPOND_REVERSE_QUERIES */
        } else {
            return append_ptr_record(packet, index,
                                     answer->custom_instance, answer->custom_service, answer->custom_proto,
                                     answer->flush, answer->bye) > 0;
        }
    } else if (answer->type == MDNS_TYPE_SRV) {
        return append_srv_record(packet, index, answer->service, answer->flush, answer->bye) > 0;
    } else if (answer->type == MDNS_TYPE_TXT) {
        return append_txt_record(packet, index, answer->service, answer->flush, answer->bye) > 0;
    } else if (answer->type == MDNS_TYPE_SDPTR) {
        return append_sdptr_record(packet, index, answer->service, answer->flush, answer->bye) > 0;
    }
#ifdef CONFIG_LWIP_IPV4
    else if (answer->type == MDNS_TYPE_A) {
        if (answer->host == mdns_priv_get_self_host()) {
            esp_netif_ip_info_t if_ip_info;
            if (!mdns_priv_if_ready(tcpip_if, MDNS_IP_PROTOCOL_V4) && !mdns_priv_pcb_is_duplicate(tcpip_if,
                                                                                                  MDNS_IP_PROTOCOL_V4)) {
                return 0;
            }
            if (esp_netif_get_ip_info(mdns_priv_get_esp_netif(tcpip_if), &if_ip_info)) {
                return 0;
            }
            if (append_a_record(packet, index, mdns_priv_get_global_hostname(), if_ip_info.ip.addr, answer->flush, answer->bye) <= 0) {
                return 0;
            }
            if (!mdns_priv_pcb_check_for_duplicates(tcpip_if)) {
                return 1;
            }
            mdns_if_t other_if = mdns_priv_netif_get_other_interface(tcpip_if);
            if (esp_netif_get_ip_info(mdns_priv_get_esp_netif(other_if), &if_ip_info)) {
                return 1;
            }
            if (append_a_record(packet, index, mdns_priv_get_global_hostname(), if_ip_info.ip.addr, answer->flush, answer->bye) > 0) {
                return 2;
            }
            return 1;
        } else if (answer->host != NULL) {
            return append_host_answer(packet, index, answer->host, ESP_IPADDR_TYPE_V4, answer->flush, answer->bye);
        }
    }
#endif /* CONFIG_LWIP_IPV4 */
#ifdef CONFIG_LWIP_IPV6
    else if (answer->type == MDNS_TYPE_AAAA) {
        if (answer->host == mdns_priv_get_self_host()) {
            struct esp_ip6_addr if_ip6s[NETIF_IPV6_MAX_NUMS];
            uint8_t count = 0;
            if (!mdns_priv_if_ready(tcpip_if, MDNS_IP_PROTOCOL_V6) && !mdns_priv_pcb_is_duplicate(tcpip_if,
                                                                                                  MDNS_IP_PROTOCOL_V6)) {
                return 0;
            }
            count = esp_netif_get_all_ip6(mdns_priv_get_esp_netif(tcpip_if), if_ip6s);
            assert(count <= NETIF_IPV6_MAX_NUMS);
            for (int i = 0; i < count; i++) {
                if (mdns_utils_ipv6_address_is_zero(if_ip6s[i])) {
                    return 0;
                }
                if (append_aaaa_record(packet, index, mdns_priv_get_global_hostname(), (uint8_t *)if_ip6s[i].addr,
                                       answer->flush, answer->bye) <= 0) {
                    return 0;
                }
            }
            if (!mdns_priv_pcb_check_for_duplicates(tcpip_if)) {
                return count;
            }

            mdns_if_t other_if = mdns_priv_netif_get_other_interface(tcpip_if);
            struct esp_ip6_addr other_ip6;
            if (esp_netif_get_ip6_linklocal(mdns_priv_get_esp_netif(other_if), &other_ip6)) {
                return count;
            }
            if (append_aaaa_record(packet, index, mdns_priv_get_global_hostname(), (uint8_t *)other_ip6.addr,
                                   answer->flush, answer->bye) > 0) {
                return 1 + count;
            }
            return count;
        } else if (answer->host != NULL) {
            return append_host_answer(packet, index, answer->host, ESP_IPADDR_TYPE_V6, answer->flush,
                                      answer->bye);
        }
    }
#endif /* CONFIG_LWIP_IPV6 */
    return 0;
}

/**
 * @brief  sends a packet
 *
 * @param  p       the packet
 */
void mdns_priv_dispatch_tx_packet(mdns_tx_packet_t *p)
{
    static uint8_t packet[MDNS_MAX_PACKET_SIZE];
    uint16_t index = MDNS_HEAD_LEN;
    memset(packet, 0, MDNS_HEAD_LEN);
    mdns_out_question_t *q;
    mdns_out_answer_t *a;
    uint8_t count;

    set_u16(packet, MDNS_HEAD_FLAGS_OFFSET, p->flags);
    set_u16(packet, MDNS_HEAD_ID_OFFSET, p->id);

    count = 0;
    q = p->questions;
    while (q) {
        if (append_question(packet, &index, q)) {
            count++;
        }
        q = q->next;
    }
    set_u16(packet, MDNS_HEAD_QUESTIONS_OFFSET, count);

    count = 0;
    a = p->answers;
    while (a) {
        count += append_answer(packet, &index, a, p->tcpip_if);
        a = a->next;
    }
    set_u16(packet, MDNS_HEAD_ANSWERS_OFFSET, count);

    count = 0;
    a = p->servers;
    while (a) {
        count += append_answer(packet, &index, a, p->tcpip_if);
        a = a->next;
    }
    set_u16(packet, MDNS_HEAD_SERVERS_OFFSET, count);

    count = 0;
    a = p->additional;
    while (a) {
        count += append_answer(packet, &index, a, p->tcpip_if);
        a = a->next;
    }
    set_u16(packet, MDNS_HEAD_ADDITIONAL_OFFSET, count);

    DBG_TX_PACKET(p, packet, index);

    mdns_priv_if_write(p->tcpip_if, p->ip_protocol, &p->dst, p->port, packet, index);
}

/**
 * @brief  Create probe packet for particular services on particular PCB
 */
mdns_tx_packet_t *mdns_priv_create_probe_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t *services[], size_t len, bool first, bool include_ip)
{
    mdns_tx_packet_t *packet = mdns_priv_alloc_packet(tcpip_if, ip_protocol);
    if (!packet) {
        return NULL;
    }

    size_t i;
    for (i = 0; i < len; i++) {
        mdns_out_question_t *q = (mdns_out_question_t *)mdns_mem_malloc(sizeof(mdns_out_question_t));
        if (!q) {
            HOOK_MALLOC_FAILED;
            mdns_priv_free_tx_packet(packet);
            return NULL;
        }
        q->next = NULL;
        q->unicast = first;
        q->type = MDNS_TYPE_ANY;
        q->host = mdns_utils_get_service_instance_name(services[i]->service);
        q->service = services[i]->service->service;
        q->proto = services[i]->service->proto;
        q->domain = MDNS_UTILS_DEFAULT_DOMAIN;
        q->own_dynamic_memory = false;
        if (!q->host || question_exists(q, packet->questions)) {
            mdns_mem_free(q);
            continue;
        } else {
            queueToEnd(mdns_out_question_t, packet->questions, q);
        }

        if (!q->host || !mdns_priv_create_answer(&packet->servers, MDNS_TYPE_SRV, services[i]->service, NULL, false,
                                                 false)) {
            mdns_priv_free_tx_packet(packet);
            return NULL;
        }
    }

    if (include_ip) {
        if (!append_host_questions_for_services(&packet->questions, services, len, first)) {
            mdns_priv_free_tx_packet(packet);
            return NULL;
        }

        if (!mdns_priv_append_host_list_in_services(&packet->servers, services, len, false, false)) {
            mdns_priv_free_tx_packet(packet);
            return NULL;
        }
    }

    return packet;
}

/**
 * @brief  Create announce packet for particular services on particular PCB
 */
mdns_tx_packet_t *mdns_priv_create_announce_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t *services[], size_t len, bool include_ip)
{
    mdns_tx_packet_t *packet = mdns_priv_alloc_packet(tcpip_if, ip_protocol);
    if (!packet) {
        return NULL;
    }
    packet->flags = MDNS_FLAGS_QR_AUTHORITATIVE;

    uint8_t i;
    for (i = 0; i < len; i++) {
        if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_SDPTR, services[i]->service, NULL, false, false)
                || !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_PTR, services[i]->service, NULL, false, false)
                || !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_SRV, services[i]->service, NULL, true, false)
                || !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_TXT, services[i]->service, NULL, true, false)) {
            mdns_priv_free_tx_packet(packet);
            return NULL;
        }
    }
    if (include_ip) {
        if (!mdns_priv_append_host_list_in_services(&packet->servers, services, len, true, false)) {
            mdns_priv_free_tx_packet(packet);
            return NULL;
        }
    }
    return packet;
}

/**
 * @brief  Convert probe packet to announce
 */
mdns_tx_packet_t *mdns_priv_create_announce_from_probe(mdns_tx_packet_t *probe)
{
    mdns_tx_packet_t *packet = mdns_priv_alloc_packet(probe->tcpip_if, probe->ip_protocol);
    if (!packet) {
        return NULL;
    }
    packet->flags = MDNS_FLAGS_QR_AUTHORITATIVE;

    mdns_out_answer_t *s = probe->servers;
    while (s) {
        if (s->type == MDNS_TYPE_SRV) {
            if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_SDPTR, s->service, NULL, false, false)
                    || !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_PTR, s->service, NULL, false, false)
                    || !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_SRV, s->service, NULL, true, false)
                    || !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_TXT, s->service, NULL, true, false)) {
                mdns_priv_free_tx_packet(packet);
                return NULL;
            }
            mdns_host_item_t *host = get_host_item(s->service->hostname);
            if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_A, NULL, host, true, false)
                    || !mdns_priv_create_answer(&packet->answers, MDNS_TYPE_AAAA, NULL, host, true, false)) {
                mdns_priv_free_tx_packet(packet);
                return NULL;
            }

        } else if (s->type == MDNS_TYPE_A || s->type == MDNS_TYPE_AAAA) {
            if (!mdns_priv_create_answer(&packet->answers, s->type, NULL, s->host, true, false)) {
                mdns_priv_free_tx_packet(packet);
                return NULL;
            }
        }

        s = s->next;
    }
    return packet;
}

void mdns_priv_send_bye(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, mdns_srv_item_t **services, size_t len, bool include_ip)
{
    mdns_tx_packet_t *packet = mdns_priv_alloc_packet(tcpip_if, ip_protocol);
    if (!packet) {
        return;
    }
    packet->flags = MDNS_FLAGS_QR_AUTHORITATIVE;
    size_t i;
    for (i = 0; i < len; i++) {
        if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_PTR, services[i]->service, NULL, true, true)) {
            mdns_priv_free_tx_packet(packet);
            return;
        }
    }
    if (include_ip) {
        mdns_priv_append_host_list_in_services(&packet->answers, services, len, true, true);
    }
    mdns_priv_dispatch_tx_packet(packet);
    mdns_priv_free_tx_packet(packet);
}

/**
 * @brief  Send bye for particular subtypes
 */
void mdns_priv_send_bye_subtype(mdns_srv_item_t *service, const char *instance_name, mdns_subtype_t *remove_subtypes)
{
    uint8_t i, j;
    for (i = 0; i < MDNS_MAX_INTERFACES; i++) {
        for (j = 0; j < MDNS_IP_PROTOCOL_MAX; j++) {
            if (mdns_priv_if_ready(i, j)) {
                mdns_tx_packet_t *packet = mdns_priv_alloc_packet((mdns_if_t) i, (mdns_ip_protocol_t) j);
                if (packet == NULL) {
                    return;
                }
                packet->flags = MDNS_FLAGS_QR_AUTHORITATIVE;
                if (!mdns_priv_create_answer(&packet->answers, MDNS_TYPE_PTR, service->service, NULL, true, true)) {
                    mdns_priv_free_tx_packet(packet);
                    return;
                }

                static uint8_t pkt[MDNS_MAX_PACKET_SIZE];
                uint16_t index = MDNS_HEAD_LEN;
                memset(pkt, 0, MDNS_HEAD_LEN);
                mdns_out_answer_t *a;
                uint8_t count;

                set_u16(pkt, MDNS_HEAD_FLAGS_OFFSET, packet->flags);
                set_u16(pkt, MDNS_HEAD_ID_OFFSET, packet->id);

                count = 0;
                a = packet->answers;
                while (a) {
                    if (a->type == MDNS_TYPE_PTR && a->service) {
                        const mdns_subtype_t *current_subtype = remove_subtypes;
                        while (current_subtype) {
                            count += (append_subtype_ptr_record(pkt, &index, instance_name, current_subtype->subtype, a->service->service, a->service->proto, a->flush, a->bye) > 0);
                            current_subtype = current_subtype->next;
                        }
                    }
                    a = a->next;
                }
                set_u16(pkt, MDNS_HEAD_ANSWERS_OFFSET, count);

                mdns_priv_if_write(packet->tcpip_if, packet->ip_protocol, &packet->dst, packet->port, pkt, index);

                mdns_priv_free_tx_packet(packet);
            }
        }
    }
}

/**
 * @brief  Find, remove and free answer from the scheduled packets
 */
void mdns_priv_remove_scheduled_answer(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, uint16_t type, mdns_srv_item_t *service)
{
    mdns_srv_item_t s = {NULL, NULL};
    if (!service) {
        service = &s;
    }
    mdns_tx_packet_t *q = s_tx_queue_head;
    while (q) {
        if (q->tcpip_if == tcpip_if && q->ip_protocol == ip_protocol && q->distributed) {
            mdns_out_answer_t *a = q->answers;
            if (a) {
                if (a->type == type && a->service == service->service) {
                    q->answers = q->answers->next;
                    mdns_mem_free(a);
                } else {
                    while (a->next) {
                        if (a->next->type == type && a->next->service == service->service) {
                            mdns_out_answer_t *b = a->next;
                            a->next = b->next;
                            mdns_mem_free(b);
                            break;
                        }
                        a = a->next;
                    }
                }
            }
        }
        q = q->next;
    }
}


/**
 * @brief  schedules a packet to be sent after given milliseconds
 *
 * @param  packet       the packet
 * @param  ms_after     number of milliseconds after which the packet should be dispatched
 */
void mdns_priv_send_after(mdns_tx_packet_t *packet, uint32_t ms_after)
{
    if (!packet) {
        return;
    }
    packet->send_at = (xTaskGetTickCount() * portTICK_PERIOD_MS) + ms_after;
    packet->next = NULL;
    if (!s_tx_queue_head || s_tx_queue_head->send_at > packet->send_at) {
        packet->next = s_tx_queue_head;
        s_tx_queue_head = packet;
        return;
    }
    mdns_tx_packet_t *q = s_tx_queue_head;
    while (q->next && q->next->send_at <= packet->send_at) {
        q = q->next;
    }
    packet->next = q->next;
    q->next = packet;
}

/**
 * @brief  free all packets scheduled for sending
 */
void mdns_priv_clear_tx_queue(void)
{
    mdns_tx_packet_t *q;
    while (s_tx_queue_head) {
        q = s_tx_queue_head;
        s_tx_queue_head = s_tx_queue_head->next;
        mdns_priv_free_tx_packet(q);
    }
}

/**
 * @brief  clear packets scheduled for sending on a specific interface
 *
 * @param  tcpip_if     the interface
 * @param  ip_protocol     pcb type V4/V6
 */
void mdns_priv_clear_tx_queue_if(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    mdns_tx_packet_t *q, * p;
    while (s_tx_queue_head && s_tx_queue_head->tcpip_if == tcpip_if && s_tx_queue_head->ip_protocol == ip_protocol) {
        q = s_tx_queue_head;
        s_tx_queue_head = s_tx_queue_head->next;
        mdns_priv_free_tx_packet(q);
    }
    if (s_tx_queue_head) {
        q = s_tx_queue_head;
        while (q->next) {
            if (q->next->tcpip_if == tcpip_if && q->next->ip_protocol == ip_protocol) {
                p = q->next;
                q->next = p->next;
                mdns_priv_free_tx_packet(p);
            } else {
                q = q->next;
            }
        }
    }
}

/**
 * @brief  get the next packet scheduled for sending on a specific interface
 *
 * @param  tcpip_if     the interface
 * @param  ip_protocol     pcb type V4/V6
 */
mdns_tx_packet_t *mdns_priv_get_next_packet(mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    mdns_tx_packet_t *q = s_tx_queue_head;
    while (q) {
        if (q->tcpip_if == tcpip_if && q->ip_protocol == ip_protocol) {
            return q;
        }
        q = q->next;
    }
    return NULL;
}

/**
 * @brief  Called from timer task to run mDNS responder
 *
 * periodically checks first unqueued packet (from tx head).
 * if it is scheduled to be transmitted, then pushes the packet to action queue to be handled.
 *
 */
void mdns_priv_send_packets(void)
{
    mdns_priv_service_lock();
    mdns_tx_packet_t *p = s_tx_queue_head;
    mdns_action_t *action = NULL;

    // find first unqueued packet
    while (p && p->queued) {
        p = p->next;
    }
    if (!p) {
        mdns_priv_service_unlock();
        return;
    }
    while (p && (int32_t)(p->send_at - (xTaskGetTickCount() * portTICK_PERIOD_MS)) < 0) {
        action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
        if (action) {
            action->type = ACTION_TX_HANDLE;
            action->data.tx_handle.packet = p;
            p->queued = true;
            if (!mdns_priv_queue_action(action)) {
                mdns_mem_free(action);
                p->queued = false;
            }
        } else {
            HOOK_MALLOC_FAILED;
            break;
        }
        //Find the next unqued packet
        p = p->next;
    }
    mdns_priv_service_unlock();
}



/**
 * @brief  Remove and free service answer from answer list (destination)
 */
static void dealloc_scheduled_service_answers(mdns_out_answer_t **destination, mdns_service_t *service)
{
    mdns_out_answer_t *d = *destination;
    if (!d) {
        return;
    }
    while (d && d->service == service) {
        *destination = d->next;
        mdns_mem_free(d);
        d = *destination;
    }
    while (d && d->next) {
        mdns_out_answer_t *a = d->next;
        if (a->service == service) {
            d->next = a->next;
            mdns_mem_free(a);
        } else {
            d = d->next;
        }
    }
}

/**
 * @brief  Find, remove and free answers and scheduled packets for service
 */
void mdns_priv_remove_scheduled_service_packets(mdns_service_t *service)
{
    if (!service) {
        return;
    }
    mdns_tx_packet_t *p = NULL;
    mdns_tx_packet_t *q = s_tx_queue_head;
    while (q) {
        bool had_answers = (q->answers != NULL);

        dealloc_scheduled_service_answers(&(q->answers), service);
        dealloc_scheduled_service_answers(&(q->additional), service);
        dealloc_scheduled_service_answers(&(q->servers), service);

        if (mdns_priv_if_ready(q->tcpip_if, q->ip_protocol)) {
            bool should_remove_questions = false;
            mdns_priv_pcb_check_probing_services(q->tcpip_if, q->ip_protocol, service,
                                                 had_answers && q->answers == NULL, &should_remove_questions);
            if (should_remove_questions && q->questions) {
                mdns_out_question_t *qsn = NULL;
                mdns_out_question_t *qs = q->questions;
                if (qs->type == MDNS_TYPE_ANY
                        && qs->service && strcmp(qs->service, service->service) == 0
                        && qs->proto && strcmp(qs->proto, service->proto) == 0) {
                    q->questions = q->questions->next;
                    mdns_mem_free(qs);
                } else while (qs->next) {
                        qsn = qs->next;
                        if (qsn->type == MDNS_TYPE_ANY
                                && qsn->service && strcmp(qsn->service, service->service) == 0
                                && qsn->proto && strcmp(qsn->proto, service->proto) == 0) {
                            qs->next = qsn->next;
                            mdns_mem_free(qsn);
                            break;
                        }
                        qs = qs->next;
                    }
            }
        }

        p = q;
        q = q->next;
        if (!p->questions && !p->answers && !p->additional && !p->servers) {
            queueDetach(mdns_tx_packet_t, s_tx_queue_head, p);
            mdns_priv_free_tx_packet(p);
        }
    }
}

static void handle_packet(mdns_tx_packet_t *p)
{
    if (mdns_priv_pcb_is_off(p->tcpip_if, p->ip_protocol)) {
        mdns_priv_free_tx_packet(p);
        return;
    }
    mdns_priv_dispatch_tx_packet(p);
    mdns_priv_pcb_schedule_tx_packet(p);
}

static void send_packet(mdns_tx_packet_t *packet)
{
    mdns_tx_packet_t *p = s_tx_queue_head;
    // packet to be handled should be at tx head, but must be consistent with the one pushed to action queue
    if (p && p == packet && p->queued) {
        p->queued = false; // clearing, as the packet might be reused (pushed and transmitted again)
        s_tx_queue_head = p->next;
        handle_packet(p);
    } else {
        ESP_LOGD(TAG, "Skipping transmit of an unexpected packet!");
    }
}

/**
 * @brief  Remove and free answer from answer list (destination)
 */
void mdns_priv_dealloc_answer(mdns_out_answer_t **destination, uint16_t type, mdns_srv_item_t *service)
{
    mdns_out_answer_t *d = *destination;
    if (!d) {
        return;
    }
    mdns_srv_item_t s = {NULL, NULL};
    if (!service) {
        service = &s;
    }
    if (d->type == type && d->service == service->service) {
        *destination = d->next;
        mdns_mem_free(d);
        return;
    }
    while (d->next) {
        mdns_out_answer_t *a = d->next;
        if (a->type == type && a->service == service->service) {
            d->next = a->next;
            mdns_mem_free(a);
            return;
        }
        d = d->next;
    }
}

void mdns_priv_send_action(mdns_action_t *action, mdns_action_subtype_t type)
{
    if (action->type != ACTION_TX_HANDLE) {
        abort();
    }
    if (type == ACTION_RUN) {
        send_packet(action->data.tx_handle.packet);
    } else if (type == ACTION_CLEANUP) {
        mdns_priv_free_tx_packet(action->data.tx_handle.packet);
    }
}
