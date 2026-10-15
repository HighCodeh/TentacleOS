
/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "mdns_private.h"
#include "mdns_networking.h"
#include "mdns.h"
#include "mdns_mem_caps.h"
#include "mdns_utils.h"
#include "mdns_debug.h"
#include "mdns_netif.h"
#include "mdns_send.h"
#include "mdns_browser.h"
#include "mdns_querier.h"
#include "mdns_pcb.h"
#include "mdns_responder.h"

static const char *TAG = "mdns_receive";

/**
 * @brief  Check if parsed name is discovery
 */
static bool is_discovery(mdns_name_t *name, uint16_t type)
{
    return (
               (name->host[0] && !strcasecmp(name->host, "_services"))
               && (name->service[0] && !strcasecmp(name->service, "_dns-sd"))
               && (name->proto[0] && !strcasecmp(name->proto, "_udp"))
               && (name->domain[0] && !strcasecmp(name->domain, MDNS_UTILS_DEFAULT_DOMAIN))
               && type == MDNS_TYPE_PTR
           );
}

static mdns_srv_item_t *get_service_item_subtype(const char *subtype, const char *service, const char *proto)
{
    mdns_srv_item_t *s = mdns_priv_get_services();
    while (s) {
        if (mdns_utils_service_match(s->service, service, proto, NULL)) {
            mdns_subtype_t *subtype_item = s->service->subtype;
            while (subtype_item) {
                if (!strcasecmp(subtype_item->subtype, subtype)) {
                    return s;
                }
                subtype_item = subtype_item->next;
            }
        }
        s = s->next;
    }
    return NULL;
}

/**
 * @brief  Check if the parsed name is ours (matches service or host name)
 */
static bool is_ours(mdns_name_t *name)
{
    //domain have to be "local"
    if (mdns_utils_str_null_or_empty(name->domain) || (strcasecmp(name->domain, MDNS_UTILS_DEFAULT_DOMAIN)
#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
                                                       && strcasecmp(name->domain, "arpa")
#endif /* CONFIG_MDNS_RESPOND_REVERSE_QUERIES */
                                                      )) {
        return false;
    }

    //if service and proto are empty, host must match out hostname
    if (mdns_utils_str_null_or_empty(name->service) && mdns_utils_str_null_or_empty(name->proto)) {
        if (!mdns_utils_str_null_or_empty(name->host)
                && !mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname())
                && mdns_utils_hostname_is_ours(name->host)) {
            return true;
        }
        return false;
    }

    //if service or proto is empty, name is invalid
    if (mdns_utils_str_null_or_empty(name->service) || mdns_utils_str_null_or_empty(name->proto)) {
        return false;
    }


    //find the service
    mdns_srv_item_t *service;
    if (name->sub) {
        service = get_service_item_subtype(name->host, name->service, name->proto);
    } else if (mdns_utils_str_null_or_empty(name->host)) {
        service = mdns_utils_get_service_item(name->service, name->proto, NULL);
    } else {
        service = mdns_utils_get_service_item_instance(name->host, name->service, name->proto, NULL);
    }
    if (!service) {
        return false;
    }

    //if query is PTR query and we have service, we have success
    if (name->sub || mdns_utils_str_null_or_empty(name->host)) {
        return true;
    }

    //OK we have host in the name. find what is the instance of the service
    const char *instance = mdns_utils_get_service_instance_name(service->service);
    if (instance == NULL) {
        return false;
    }

    //compare the instance against the name
    if (strcasecmp(name->host, instance) == 0) {
        return true;
    }

    return false;
}

/**
 * @brief  Duplicate string or return error
 */
static esp_err_t strdup_check(char **out, char *in)
{
    if (in && in[0]) {
        *out = mdns_mem_strdup(in);
        if (!*out) {
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    *out = NULL;
    return ESP_OK;
}

/*
 * @brief  Appends/increments a number to name/instance in case of collision
 * */
static char *mangle_name(char *in)
{
    char *p = strrchr(in, '-');
    int suffix = 0;
    if (p == NULL) {
        //No - in ``in``
        suffix = 2;
    } else {
        char *endp = NULL;
        suffix = strtol(p + 1, &endp, 10);
        if (*endp != 0) {
            //suffix is not numerical
            suffix = 2;
            p = NULL; //so we append -suffix to the entire string
        }
    }
    char *ret;
    if (p == NULL) {
        //need to add -2 to string
        ret = mdns_mem_malloc(strlen(in) + 3);
        if (ret == NULL) {
            HOOK_MALLOC_FAILED;
            return NULL;
        }
        sprintf(ret, "%s-2", in);
    } else {
        size_t in_len = strlen(in);
        ret = mdns_mem_malloc(in_len + 2); //one extra byte in case 9-10 or 99-100 etc
        if (ret == NULL) {
            HOOK_MALLOC_FAILED;
            return NULL;
        }
        memcpy(ret, in, in_len);
        int baseLen = p - in; //length of 'bla' in 'bla-123'
        //overwrite suffix with new suffix
        sprintf(ret + baseLen, "-%d", suffix + 1);
    }
    return ret;
}

/**
 * @brief  Get number of items in TXT parsed data
 */
static int get_txt_items_count(const uint8_t *data, size_t len)
{
    if (len == 1) {
        return 0;
    }

    int num_items = 0;
    uint16_t i = 0;
    size_t partLen = 0;

    while (i < len) {
        partLen = data[i++];
        if (!partLen) {
            break;
        }
        if ((i + partLen) > len) {
            return -1;//error
        }
        i += partLen;
        num_items++;
    }
    return num_items;
}

/**
 * @brief  Get the length of TXT item's key name
 */
static int get_txt_item_len(const uint8_t *data, size_t len)
{
    if (*data == '=') {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '=') {
            return i;
        }
    }
    return len;
}

/**
 * @brief  Create TXT result array from parsed TXT data
 */
static void result_txt_create(const uint8_t *data, size_t len, mdns_txt_item_t **out_txt, uint8_t **out_value_len,
                              size_t *out_count)
{
    *out_txt = NULL;
    *out_count = 0;
    uint16_t i = 0, y;
    size_t part_len = 0;
    int num_items = get_txt_items_count(data, len);
    if (num_items < 0 || num_items > SIZE_MAX / sizeof(mdns_txt_item_t)) {
        // Error: num_items is incorrect (or too large to allocate)
        return;
    }

    if (!num_items) {
        return;
    }

    mdns_txt_item_t *txt = (mdns_txt_item_t *)mdns_mem_malloc(sizeof(mdns_txt_item_t) * num_items);
    if (!txt) {
        HOOK_MALLOC_FAILED;
        return;
    }
    uint8_t *txt_value_len = (uint8_t *)mdns_mem_malloc(num_items);
    if (!txt_value_len) {
        mdns_mem_free(txt);
        HOOK_MALLOC_FAILED;
        return;
    }
    memset(txt, 0, sizeof(mdns_txt_item_t) * num_items);
    memset(txt_value_len, 0, num_items);
    size_t txt_num = 0;

    while (i < len && txt_num < num_items) {
        part_len = data[i++];
        if (!part_len) {
            break;
        }

        if ((i + part_len) > len) {
            goto handle_error;//error
        }

        int name_len = get_txt_item_len(data + i, part_len);
        if (name_len < 0) {  //invalid item (no name)
            i += part_len;
            continue;
        }
        char *key = (char *) mdns_mem_malloc(name_len + 1);
        if (!key) {
            HOOK_MALLOC_FAILED;
            goto handle_error;//error
        }

        mdns_txt_item_t *t = &txt[txt_num];
        uint8_t *value_len = &txt_value_len[txt_num];
        txt_num++;

        memcpy(key, data + i, name_len);
        key[name_len] = 0;
        i += name_len + (name_len < part_len); // skip '=' only if present
        t->key = key;

        int new_value_len = part_len - name_len - 1;
        if (new_value_len > 0) {
            char *value = (char *) mdns_mem_malloc(new_value_len + 1);
            if (!value) {
                HOOK_MALLOC_FAILED;
                goto handle_error;//error
            }
            memcpy(value, data + i, new_value_len);
            value[new_value_len] = 0;
            *value_len = new_value_len;
            i += new_value_len;
            t->value = value;
        } else {
            t->value = NULL;
        }
    }

    *out_txt = txt;
    *out_count = txt_num;
    *out_value_len = txt_value_len;
    return;

handle_error :
    for (y = 0; y < txt_num; y++) {
        mdns_txt_item_t *t = &txt[y];
        mdns_mem_free((char *)t->key);
        mdns_mem_free((char *)t->value);
    }
    mdns_mem_free(txt_value_len);
    mdns_mem_free(txt);
}

#ifdef CONFIG_LWIP_IPV4
/**
 * @brief  Detect IPv4 address collision
 */
static int check_a_collision(esp_ip4_addr_t *ip, mdns_if_t tcpip_if)
{
    esp_netif_ip_info_t if_ip_info;
    esp_netif_ip_info_t other_ip_info;
    if (!ip->addr) {
        return 1;//denial! they win
    }
    if (esp_netif_get_ip_info(mdns_priv_get_esp_netif(tcpip_if), &if_ip_info)) {
        return 1;//they win
    }
    int ret = memcmp((uint8_t *)&if_ip_info.ip.addr, (uint8_t *)&ip->addr, sizeof(esp_ip4_addr_t));
    if (ret > 0) {
        return -1;//we win
    } else if (ret < 0) {
        //is it the other interface?
        mdns_if_t other_if = mdns_priv_netif_get_other_interface(tcpip_if);
        if (other_if == MDNS_MAX_INTERFACES) {
            return 1;//AP interface! They win
        }
        if (esp_netif_get_ip_info(mdns_priv_get_esp_netif(other_if), &other_ip_info)) {
            return 1;//IPv4 not active! They win
        }
        if (ip->addr != other_ip_info.ip.addr) {
            return 1;//IPv4 not ours! They win
        }
        mdns_priv_pcb_set_duplicate(tcpip_if);
        return 2;//they win
    }
    return 0;//same
}
#endif /* CONFIG_LWIP_IPV4 */

#ifdef CONFIG_LWIP_IPV6
/**
 * @brief  Detect IPv6 address collision
 */
static int check_aaaa_collision(esp_ip6_addr_t *ip, mdns_if_t tcpip_if)
{
    struct esp_ip6_addr if_ip6;
    struct esp_ip6_addr other_ip6;
    if (mdns_utils_ipv6_address_is_zero(*ip)) {
        return 1;//denial! they win
    }
    if (esp_netif_get_ip6_linklocal(mdns_priv_get_esp_netif(tcpip_if), &if_ip6)) {
        return 1;//they win
    }
    int ret = memcmp((uint8_t *)&if_ip6.addr, (uint8_t *)ip->addr, MDNS_UTILS_SIZEOF_IP6_ADDR);
    if (ret > 0) {
        return -1;//we win
    } else if (ret < 0) {
        //is it the other interface?
        mdns_if_t other_if = mdns_priv_netif_get_other_interface(tcpip_if);
        if (other_if == MDNS_MAX_INTERFACES) {
            return 1;//AP interface! They win
        }
        if (esp_netif_get_ip6_linklocal(mdns_priv_get_esp_netif(other_if), &other_ip6)) {
            return 1;//IPv6 not active! They win
        }
        if (memcmp((uint8_t *)&other_ip6.addr, (uint8_t *)ip->addr, MDNS_UTILS_SIZEOF_IP6_ADDR)) {
            return 1;//IPv6 not ours! They win
        }
        mdns_priv_pcb_set_duplicate(tcpip_if);
        return 2;//they win
    }
    return 0;//same
}
#endif /* CONFIG_LWIP_IPV6 */

/**
 * @brief  Detect TXT collision
 */
static int check_txt_collision(mdns_service_t *service, const uint8_t *data, size_t len)
{
    size_t data_len = 0;
    if (len <= 1 && service->txt) {     // len==0 means incorrect packet (and handled by the packet parser)
        // but handled here again to fix clang-tidy warning on VLA "uint8_t our[0];"
        return -1;//we win
    } else if (len > 1 && !service->txt) {
        return 1;//they win
    } else if (len <= 1 && !service->txt) {
        return 0;//same
    }

    mdns_txt_linked_item_t *txt = service->txt;
    while (txt) {
        data_len += 1 /* record-len */ + strlen(txt->key) + txt->value_len + (txt->value ? 1 : 0 /* "=" */);
        txt = txt->next;
    }

    if (len > data_len) {
        return 1;//they win
    } else if (len < data_len) {
        return -1;//we win
    }

    uint8_t ours[len];
    uint16_t index = 0;

    txt = service->txt;
    while (txt) {
        mdns_priv_append_one_txt_record_entry(ours, &index, txt);
        txt = txt->next;
    }

    int ret = memcmp(ours, data, len);
    if (ret > 0) {
        return -1;//we win
    } else if (ret < 0) {
        return 1;//they win
    }
    return 0;//same
}

/**
 * @brief  Detect SRV collision
 */
static int check_srv_collision(mdns_service_t *service, uint16_t priority, uint16_t weight, uint16_t port, const char *host, const char *domain)
{
    if (mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname())) {
        return 0;
    }

    size_t our_host_len = strlen(mdns_priv_get_global_hostname());
    size_t our_len = 14 + our_host_len;

    size_t their_host_len = strlen(host);
    size_t their_domain_len = strlen(domain);
    size_t their_len = 9 + their_host_len + their_domain_len;

    if (their_len > our_len) {
        return 1;//they win
    } else if (their_len < our_len) {
        return -1;//we win
    }

    uint16_t our_index = 0;
    uint8_t our_data[our_len];
    mdns_utils_append_u16(our_data, &our_index, service->priority);
    mdns_utils_append_u16(our_data, &our_index, service->weight);
    mdns_utils_append_u16(our_data, &our_index, service->port);
    our_data[our_index++] = our_host_len;
    memcpy(our_data + our_index, mdns_priv_get_global_hostname(), our_host_len);
    our_index += our_host_len;
    our_data[our_index++] = 5;
    memcpy(our_data + our_index, MDNS_UTILS_DEFAULT_DOMAIN, 5);
    our_index += 5;
    our_data[our_index++] = 0;

    uint16_t their_index = 0;
    uint8_t their_data[their_len];
    mdns_utils_append_u16(their_data, &their_index, priority);
    mdns_utils_append_u16(their_data, &their_index, weight);
    mdns_utils_append_u16(their_data, &their_index, port);
    their_data[their_index++] = their_host_len;
    memcpy(their_data + their_index, host, their_host_len);
    their_index += their_host_len;
    their_data[their_index++] = their_domain_len;
    memcpy(their_data + their_index, domain, their_domain_len);
    their_index += their_domain_len;
    their_data[their_index++] = 0;

    int ret = memcmp(our_data, their_data, our_len);
    if (ret > 0) {
        return -1;//we win
    } else if (ret < 0) {
        return 1;//they win
    }
    return 0;//same
}

/**
 * @brief  Check if the parsed name is self-hosted, i.e. we should resolve conflicts
 */
static bool is_name_selfhosted(mdns_name_t *name)
{
    if (mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname())) { // self-hostname needs to be defined
        return false;
    }

    // hostname only -- check if selfhosted name
    if (mdns_utils_str_null_or_empty(name->service) && mdns_utils_str_null_or_empty(name->proto) &&
            strcasecmp(name->host, mdns_priv_get_global_hostname()) == 0) {
        return true;
    }

    // service -- check if selfhosted service
    mdns_srv_item_t *srv = mdns_utils_get_service_item(name->service, name->proto, NULL);
    if (srv && strcasecmp(mdns_priv_get_global_hostname(), srv->service->hostname) == 0) {
        return true;
    }
    return false;
}

/**
 * @brief  Called from parser to check if question matches particular service
 */
static bool question_matches(mdns_parsed_question_t *question, uint16_t type, mdns_srv_item_t *service)
{
    if (question->type != type) {
        return false;
    }
    if (type == MDNS_TYPE_A || type == MDNS_TYPE_AAAA) {
        return true;
    } else if (type == MDNS_TYPE_PTR || type == MDNS_TYPE_SDPTR) {
        if (question->service && question->proto && question->domain
                && !strcasecmp(service->service->service, question->service)
                && !strcasecmp(service->service->proto, question->proto)
                && !strcasecmp(MDNS_UTILS_DEFAULT_DOMAIN, question->domain)) {
            if (!service->service->instance) {
                return true;
            } else if (service->service->instance && question->host && !strcasecmp(service->service->instance, question->host)) {
                return true;
            }
        }
    } else if (service && (type == MDNS_TYPE_SRV || type == MDNS_TYPE_TXT)) {
        const char *name = mdns_utils_get_service_instance_name(service->service);
        if (name && question->host && question->service && question->proto && question->domain
                && !strcasecmp(name, question->host)
                && !strcasecmp(service->service->service, question->service)
                && !strcasecmp(service->service->proto, question->proto)
                && !strcasecmp(MDNS_UTILS_DEFAULT_DOMAIN, question->domain)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief  Removes saved question from parsed data
 */
static void remove_parsed_question(mdns_parsed_packet_t *parsed_packet, uint16_t type, mdns_srv_item_t *service)
{
    mdns_parsed_question_t *q = parsed_packet->questions;

    if (!q) {
        return;
    }

    if (question_matches(q, type, service)) {
        parsed_packet->questions = q->next;
        mdns_mem_free(q->host);
        mdns_mem_free(q->service);
        mdns_mem_free(q->proto);
        mdns_mem_free(q->domain);
        mdns_mem_free(q);
        return;
    }

    while (q->next) {
        mdns_parsed_question_t *p = q->next;
        if (question_matches(p, type, service)) {
            q->next = p->next;
            mdns_mem_free(p->host);
            mdns_mem_free(p->service);
            mdns_mem_free(p->proto);
            mdns_mem_free(p->domain);
            mdns_mem_free(p);
            return;
        }
        q = q->next;
    }
}

/**
 * @brief  main packet parser
 *
 * @param  packet       the packet
 */
static void mdns_parse_packet(mdns_rx_packet_t *packet)
{
    static mdns_name_t n;
    mdns_header_t header;
    const uint8_t *data = mdns_priv_get_packet_data(packet);
    size_t len = mdns_priv_get_packet_len(packet);
    const uint8_t *content = data + MDNS_HEAD_LEN;
    bool do_not_reply = false;
    mdns_search_once_t *search_result = NULL;
    mdns_browse_t *browse_result = NULL;
    char *browse_result_instance = NULL;
    char *browse_result_service = NULL;
    char *browse_result_proto = NULL;
    mdns_browse_sync_t *out_sync_browse = NULL;

    DBG_RX_PACKET(packet, data, len);

#ifndef CONFIG_MDNS_SKIP_SUPPRESSING_OWN_QUERIES
    // Check if the packet wasn't sent by us
#ifdef CONFIG_LWIP_IPV4
    if (packet->ip_protocol == MDNS_IP_PROTOCOL_V4) {
        esp_netif_ip_info_t if_ip_info;
        if (esp_netif_get_ip_info(mdns_priv_get_esp_netif(packet->tcpip_if), &if_ip_info) == ESP_OK &&
                memcmp(&if_ip_info.ip.addr, &packet->src.u_addr.ip4.addr, sizeof(esp_ip4_addr_t)) == 0) {
            return;
        }
    }
#endif /* CONFIG_LWIP_IPV4 */
#ifdef CONFIG_LWIP_IPV6
    if (packet->ip_protocol == MDNS_IP_PROTOCOL_V6) {
        struct esp_ip6_addr if_ip6;
        if (esp_netif_get_ip6_linklocal(mdns_priv_get_esp_netif(packet->tcpip_if), &if_ip6) == ESP_OK &&
                memcmp(&if_ip6, &packet->src.u_addr.ip6, sizeof(esp_ip6_addr_t)) == 0) {
            return;
        }
    }
#endif /* CONFIG_LWIP_IPV6 */
#endif // CONFIG_MDNS_SKIP_SUPPRESSING_OWN_QUERIES

    // Check for the minimum size of mdns packet
    if (len <=  MDNS_HEAD_ADDITIONAL_OFFSET) {
        return;
    }

    mdns_parsed_packet_t *parsed_packet = (mdns_parsed_packet_t *)mdns_mem_malloc(sizeof(mdns_parsed_packet_t));
    if (!parsed_packet) {
        HOOK_MALLOC_FAILED;
        return;
    }
    memset(parsed_packet, 0, sizeof(mdns_parsed_packet_t));

    mdns_name_t *name = &n;
    memset(name, 0, sizeof(mdns_name_t));

    header.id = mdns_utils_read_u16(data, MDNS_HEAD_ID_OFFSET);
    header.flags = mdns_utils_read_u16(data, MDNS_HEAD_FLAGS_OFFSET);
    header.questions = mdns_utils_read_u16(data, MDNS_HEAD_QUESTIONS_OFFSET);
    header.answers = mdns_utils_read_u16(data, MDNS_HEAD_ANSWERS_OFFSET);
    header.servers = mdns_utils_read_u16(data, MDNS_HEAD_SERVERS_OFFSET);
    header.additional = mdns_utils_read_u16(data, MDNS_HEAD_ADDITIONAL_OFFSET);

    if (header.flags == MDNS_FLAGS_QR_AUTHORITATIVE && packet->src_port != MDNS_SERVICE_PORT) {
        mdns_mem_free(parsed_packet);
        return;
    }

    //if we have not set the hostname, we can not answer questions
    if (header.questions && !header.answers && mdns_utils_str_null_or_empty(mdns_priv_get_global_hostname())) {
        mdns_mem_free(parsed_packet);
        return;
    }

    parsed_packet->tcpip_if = packet->tcpip_if;
    parsed_packet->ip_protocol = packet->ip_protocol;
    parsed_packet->multicast = packet->multicast;
    parsed_packet->authoritative = (header.flags == MDNS_FLAGS_QR_AUTHORITATIVE);
    parsed_packet->distributed = header.flags == MDNS_FLAGS_DISTRIBUTED;
    parsed_packet->id = header.id;
    esp_netif_ip_addr_copy(&parsed_packet->src, &packet->src);
    parsed_packet->src_port = packet->src_port;
    parsed_packet->records = NULL;

    if (header.questions) {
        uint8_t qs = header.questions;

        while (qs--) {
            content = mdns_utils_parse_fqdn(data, content, name, len);
            if (!content) {
                header.answers = 0;
                header.additional = 0;
                header.servers = 0;
                goto clear_rx_packet; // error
            }

            if (content + MDNS_CLASS_OFFSET + 1 >= data + len) {
                goto clear_rx_packet; // malformed packet, won't read behind it
            }
            uint16_t type = mdns_utils_read_u16(content, MDNS_TYPE_OFFSET);
            uint16_t mdns_class = mdns_utils_read_u16(content, MDNS_CLASS_OFFSET);
            bool unicast = !!(mdns_class & 0x8000);
            mdns_class &= 0x7FFF;
            content = content + 4;

            if (mdns_class != 0x0001 || name->invalid) { // bad class or invalid name for this question entry
                continue;
            }

            if (is_discovery(name, type)) {
                //service discovery
                parsed_packet->discovery = true;
                mdns_srv_item_t *a = mdns_priv_get_services();
                while (a) {
                    mdns_parsed_question_t *question = (mdns_parsed_question_t *)mdns_mem_calloc(1, sizeof(mdns_parsed_question_t));
                    if (!question) {
                        HOOK_MALLOC_FAILED;
                        goto clear_rx_packet;
                    }
                    question->next = parsed_packet->questions;
                    parsed_packet->questions = question;

                    question->unicast = unicast;
                    question->type = MDNS_TYPE_SDPTR;
                    question->host = NULL;
                    question->service = mdns_mem_strdup(a->service->service);
                    question->proto = mdns_mem_strdup(a->service->proto);
                    question->domain = mdns_mem_strdup(MDNS_UTILS_DEFAULT_DOMAIN);
                    if (!question->service || !question->proto || !question->domain) {
                        goto clear_rx_packet;
                    }
                    a = a->next;
                }
                continue;
            }
            if (!is_ours(name)) {
                continue;
            }

            if (type == MDNS_TYPE_ANY && !mdns_utils_str_null_or_empty(name->host)) {
                parsed_packet->probe = true;
            }

            mdns_parsed_question_t *question = (mdns_parsed_question_t *)mdns_mem_calloc(1, sizeof(mdns_parsed_question_t));
            if (!question) {
                HOOK_MALLOC_FAILED;
                goto clear_rx_packet;
            }
            question->next = parsed_packet->questions;
            parsed_packet->questions = question;

            question->unicast = unicast;
            question->type = type;
            question->sub = name->sub;
            if (strdup_check(&(question->host), name->host)
                    || strdup_check(&(question->service), name->service)
                    || strdup_check(&(question->proto), name->proto)
                    || strdup_check(&(question->domain), name->domain)) {
                goto clear_rx_packet;
            }
        }
    }

    if (header.questions && !parsed_packet->questions && !parsed_packet->discovery && !header.answers) {
        goto clear_rx_packet;
    } else if (header.answers || header.servers || header.additional) {
        uint16_t recordIndex = 0;

        while (content < (data + len)) {

            content = mdns_utils_parse_fqdn(data, content, name, len);
            if (!content) {
                goto clear_rx_packet;
            }

            if (content + MDNS_LEN_OFFSET + 1 >= data + len) {
                goto clear_rx_packet; // malformed packet, won't read behind it
            }
            uint16_t type = mdns_utils_read_u16(content, MDNS_TYPE_OFFSET);
            uint16_t mdns_class = mdns_utils_read_u16(content, MDNS_CLASS_OFFSET);
            uint32_t ttl = mdns_utils_read_u32(content, MDNS_TTL_OFFSET);
            uint16_t data_len = mdns_utils_read_u16(content, MDNS_LEN_OFFSET);
            const uint8_t *data_ptr = content + MDNS_DATA_OFFSET;
            mdns_class &= 0x7FFF;

            content = data_ptr + data_len;
            if (content > (data + len) || data_len == 0) {
                goto clear_rx_packet;
            }

            bool discovery = false;
            bool ours = false;
            mdns_srv_item_t *service = NULL;
            mdns_parsed_record_type_t record_type = MDNS_ANSWER;

            if (recordIndex >= (header.answers + header.servers)) {
                record_type = MDNS_EXTRA;
            } else if (recordIndex >= (header.answers)) {
                record_type = MDNS_NS;
            }
            recordIndex++;

            if (type == MDNS_TYPE_NSEC || type == MDNS_TYPE_OPT) {
                //skip NSEC and OPT
                continue;
            }

            if (parsed_packet->discovery && is_discovery(name, type)) {
                discovery = true;
            } else if (!name->sub && is_ours(name)) {
                ours = true;
                if (name->service[0] && name->proto[0]) {
                    service = mdns_utils_get_service_item(name->service, name->proto, NULL);
                }
            } else {
                if ((header.flags & MDNS_FLAGS_QUERY_REPSONSE) == 0 || record_type == MDNS_NS) {
                    //skip this record
                    continue;
                }
                search_result = mdns_priv_query_find(name, type, packet->tcpip_if, packet->ip_protocol);
                browse_result = mdns_priv_browse_find(name, type, packet->tcpip_if, packet->ip_protocol);
                if (browse_result) {
                    if (!out_sync_browse) {
                        // will be freed in function `browse_sync`
                        out_sync_browse = (mdns_browse_sync_t *)mdns_mem_malloc(sizeof(mdns_browse_sync_t));
                        if (!out_sync_browse) {
                            HOOK_MALLOC_FAILED;
                            goto clear_rx_packet;
                        }
                        out_sync_browse->browse = browse_result;
                        out_sync_browse->sync_result = NULL;
                    }
                    if (!browse_result_service) {
                        browse_result_service = (char *)mdns_mem_malloc(MDNS_NAME_BUF_LEN);
                        if (!browse_result_service) {
                            HOOK_MALLOC_FAILED;
                            goto clear_rx_packet;
                        }
                    }
                    memcpy(browse_result_service, browse_result->service, MDNS_NAME_BUF_LEN);
                    if (!browse_result_proto) {
                        browse_result_proto = (char *)mdns_mem_malloc(MDNS_NAME_BUF_LEN);
                        if (!browse_result_proto) {
                            HOOK_MALLOC_FAILED;
                            goto clear_rx_packet;
                        }
                    }
                    memcpy(browse_result_proto, browse_result->proto, MDNS_NAME_BUF_LEN);
                    if (type == MDNS_TYPE_SRV || type == MDNS_TYPE_TXT) {
                        if (!browse_result_instance) {
                            browse_result_instance = (char *)mdns_mem_malloc(MDNS_NAME_BUF_LEN);
                            if (!browse_result_instance) {
                                HOOK_MALLOC_FAILED;
                                goto clear_rx_packet;
                            }
                        }
                        memcpy(browse_result_instance, name->host, MDNS_NAME_BUF_LEN);
                    }
                }
            }

            if (type == MDNS_TYPE_PTR) {
                if (!mdns_utils_parse_fqdn(data, data_ptr, name, len)) {
                    continue;//error
                }
                if (search_result) {
                    mdns_priv_query_result_add_ptr(search_result, name->host, name->service, name->proto,
                                                   packet->tcpip_if, packet->ip_protocol, ttl);
                } else if ((discovery || ours) && !name->sub && is_ours(name)) {
                    if (name->host[0]) {
                        service = mdns_utils_get_service_item_instance(name->host, name->service, name->proto, NULL);
                    } else {
                        service = mdns_utils_get_service_item(name->service, name->proto, NULL);
                    }
                    if (discovery && service) {
                        remove_parsed_question(parsed_packet, MDNS_TYPE_SDPTR, service);
                    } else if (service && parsed_packet->questions && !parsed_packet->probe) {
                        remove_parsed_question(parsed_packet, type, service);
                    } else if (service) {
                        //check if TTL is more than half of the full TTL value (4500)
                        if (ttl > (MDNS_ANSWER_PTR_TTL / 2)) {
                            mdns_priv_remove_scheduled_answer(packet->tcpip_if, packet->ip_protocol, type, service);
                        }
                    }
                    if (service) {
                        mdns_parsed_record_t *record = mdns_mem_malloc(sizeof(mdns_parsed_record_t));
                        if (!record) {
                            HOOK_MALLOC_FAILED;
                            goto clear_rx_packet;
                        }
                        record->next = parsed_packet->records;
                        parsed_packet->records = record;
                        record->type = MDNS_TYPE_PTR;
                        record->record_type = MDNS_ANSWER;
                        record->ttl = ttl;
                        record->host = NULL;
                        record->service = NULL;
                        record->proto = NULL;
                        if (name->host[0]) {
                            record->host = mdns_mem_malloc(MDNS_NAME_BUF_LEN);
                            if (!record->host) {
                                HOOK_MALLOC_FAILED;
                                goto clear_rx_packet;
                            }
                            memcpy(record->host, name->host, MDNS_NAME_BUF_LEN);
                        }
                        if (name->service[0]) {
                            record->service = mdns_mem_malloc(MDNS_NAME_BUF_LEN);
                            if (!record->service) {
                                HOOK_MALLOC_FAILED;
                                goto clear_rx_packet;
                            }
                            memcpy(record->service, name->service, MDNS_NAME_BUF_LEN);
                        }
                        if (name->proto[0]) {
                            record->proto = mdns_mem_malloc(MDNS_NAME_BUF_LEN);
                            if (!record->proto) {
                                HOOK_MALLOC_FAILED;
                                goto clear_rx_packet;
                            }
                            memcpy(record->proto, name->proto, MDNS_NAME_BUF_LEN);
                        }
                    }
                }
            } else if (type == MDNS_TYPE_SRV) {
                mdns_result_t *result = NULL;
                if (search_result && search_result->type == MDNS_TYPE_PTR) {
                    result = search_result->result;
                    while (result) {
                        if (mdns_priv_get_esp_netif(packet->tcpip_if) == result->esp_netif
                                && packet->ip_protocol == result->ip_protocol
                                && result->instance_name && !strcmp(name->host, result->instance_name)) {
                            break;
                        }
                        result = result->next;
                    }
                    if (!result) {
                        result = mdns_priv_query_result_add_ptr(search_result, name->host, name->service, name->proto,
                                                                packet->tcpip_if, packet->ip_protocol, ttl);
                        if (!result) {
                            continue;
                        }
                    }
                }
                bool is_selfhosted = is_name_selfhosted(name);
                if (!mdns_utils_parse_fqdn(data, data_ptr + MDNS_SRV_FQDN_OFFSET, name, len)) {
                    continue;
                }
                if (data_ptr + MDNS_SRV_PORT_OFFSET + 1 >= data + len) {
                    goto clear_rx_packet; // malformed packet, won't read behind it
                }
                uint16_t priority = mdns_utils_read_u16(data_ptr, MDNS_SRV_PRIORITY_OFFSET);
                uint16_t weight = mdns_utils_read_u16(data_ptr, MDNS_SRV_WEIGHT_OFFSET);
                uint16_t port = mdns_utils_read_u16(data_ptr, MDNS_SRV_PORT_OFFSET);

                if (browse_result) {
                    mdns_priv_browse_result_add_srv(browse_result, name->host, browse_result_instance,
                                                    browse_result_service,
                                                    browse_result_proto, port, packet->tcpip_if, packet->ip_protocol,
                                                    ttl,
                                                    out_sync_browse);
                }
                if (search_result) {
                    if (search_result->type == MDNS_TYPE_PTR) {
                        if (!result->hostname) { // assign host/port for this entry only if not previously set
                            result->port = port;
                            result->hostname = mdns_mem_strdup(name->host);
                        }
                    } else {
                        mdns_priv_query_result_add_srv(search_result, name->host, port, packet->tcpip_if,
                                                       packet->ip_protocol, ttl);
                    }
                } else if (ours) {
                    if (parsed_packet->questions && !parsed_packet->probe) {
                        remove_parsed_question(parsed_packet, type, service);
                        continue;
                    } else if (parsed_packet->distributed) {
                        mdns_priv_remove_scheduled_answer(packet->tcpip_if, packet->ip_protocol, type, service);
                        continue;
                    }
                    if (!is_selfhosted) {
                        continue;
                    }
                    //detect collision (-1=won, 0=none, 1=lost)
                    int col = 0;
                    if (mdns_class > 1) {
                        col = 1;
                    } else if (!mdns_class) {
                        col = -1;
                    } else if (service) { // only detect srv collision if service existed
                        col = check_srv_collision(service->service, priority, weight, port, name->host, name->domain);
                    }
                    if (service && col && (parsed_packet->probe || parsed_packet->authoritative)) {
                        if (col > 0 || !port) {
                            do_not_reply = true;
                            if (mdns_priv_pcb_is_probing(packet)) {
                                mdns_priv_pcb_set_probe_failed(packet);
                                if (!mdns_utils_str_null_or_empty(service->service->instance)) {
                                    char *new_instance = mangle_name((char *) service->service->instance);
                                    if (new_instance) {
                                        mdns_mem_free((char *)service->service->instance);
                                        service->service->instance = new_instance;
                                    }
                                    mdns_priv_probe_all_pcbs(&service, 1, false, false);
                                } else if (!mdns_utils_str_null_or_empty(mdns_priv_get_instance())) {
                                    char *new_instance = mangle_name((char *) mdns_priv_get_instance());
                                    if (new_instance) {
                                        mdns_priv_set_instance(new_instance);
                                    }
                                    mdns_priv_restart_all_pcbs_no_instance();
                                } else {
                                    char *new_host = mangle_name((char *) mdns_priv_get_global_hostname());
                                    if (new_host) {
                                        mdns_priv_remap_self_service_hostname(mdns_priv_get_global_hostname(), new_host);
                                        mdns_priv_set_global_hostname(new_host);
                                    }
                                    mdns_priv_restart_all_pcbs();
                                }
                            } else if (service) {
                                mdns_priv_send_bye(packet->tcpip_if, packet->ip_protocol, &service, 1, false);
                                mdns_priv_init_pcb_probe(packet->tcpip_if, packet->ip_protocol, &service, 1, false);
                            }
                        }
                    } else if (ttl > 60 && !col && !parsed_packet->authoritative && !parsed_packet->probe && !parsed_packet->questions) {
                        mdns_priv_remove_scheduled_answer(packet->tcpip_if, packet->ip_protocol, type, service);
                    }
                }
            } else if (type == MDNS_TYPE_TXT) {
                mdns_txt_item_t *txt = NULL;
                uint8_t *txt_value_len = NULL;
                size_t txt_count = 0;

                mdns_result_t *result = NULL;
                if (browse_result) {
                    result_txt_create(data_ptr, data_len, &txt, &txt_value_len, &txt_count);
                    mdns_priv_browse_result_add_txt(browse_result, browse_result_instance, browse_result_service,
                                                    browse_result_proto,
                                                    txt, txt_value_len, txt_count, packet->tcpip_if,
                                                    packet->ip_protocol,
                                                    ttl, out_sync_browse);
                }
                if (search_result) {
                    if (search_result->type == MDNS_TYPE_PTR) {
                        result = search_result->result;
                        while (result) {
                            if (mdns_priv_get_esp_netif(packet->tcpip_if) == result->esp_netif
                                    && packet->ip_protocol == result->ip_protocol
                                    && result->instance_name && !strcmp(name->host, result->instance_name)) {
                                break;
                            }
                            result = result->next;
                        }
                        if (!result) {
                            result = mdns_priv_query_result_add_ptr(search_result, name->host, name->service,
                                                                    name->proto,
                                                                    packet->tcpip_if, packet->ip_protocol, ttl);
                            if (!result) {
                                continue;
                            }
                        }
                        if (!result->txt) {
                            result_txt_create(data_ptr, data_len, &txt, &txt_value_len, &txt_count);
                            if (txt_count) {
                                result->txt = txt;
                                result->txt_count = txt_count;
                                result->txt_value_len = txt_value_len;
                            }
                        }
                    } else {
                        result_txt_create(data_ptr, data_len, &txt, &txt_value_len, &txt_count);
                        if (txt_count) {
                            mdns_priv_query_result_add_txt(search_result, txt, txt_value_len, txt_count,
                                                           packet->tcpip_if, packet->ip_protocol, ttl);
                        }
                    }
                } else if (ours) {
                    if (parsed_packet->questions && !parsed_packet->probe && service) {
                        remove_parsed_question(parsed_packet, type, service);
                        continue;
                    }
                    if (!is_name_selfhosted(name)) {
                        continue;
                    }
                    //detect collision (-1=won, 0=none, 1=lost)
                    int col = 0;
                    if (mdns_class > 1) {
                        col = 1;
                    } else if (!mdns_class) {
                        col = -1;
                    } else if (service) { // only detect txt collision if service existed
                        col = check_txt_collision(service->service, data_ptr, data_len);
                    }
                    if (col && !mdns_priv_pcb_is_probing(packet) && service) {
                        do_not_reply = true;
                        mdns_priv_init_pcb_probe(packet->tcpip_if, packet->ip_protocol, &service, 1, true);
                    } else if (ttl > (MDNS_ANSWER_TXT_TTL / 2) && !col && !parsed_packet->authoritative && !parsed_packet->probe && !parsed_packet->questions && !mdns_priv_pcb_is_probing(
                                   packet)) {
                        mdns_priv_remove_scheduled_answer(packet->tcpip_if, packet->ip_protocol, type, service);
                    }
                }

            }
#ifdef CONFIG_LWIP_IPV6
            else if (type == MDNS_TYPE_AAAA) { // ipv6
                if (data_len < MDNS_ANSWER_AAAA_SIZE) {
                    continue; // malformed AAAA record, RDATA smaller than an IPv6 address
                }
                esp_ip_addr_t ip6;
                ip6.type = ESP_IPADDR_TYPE_V6;
                memcpy(ip6.u_addr.ip6.addr, data_ptr, MDNS_ANSWER_AAAA_SIZE);
                if (browse_result) {
                    mdns_priv_browse_result_add_ip(browse_result, name->host, &ip6, packet->tcpip_if,
                                                   packet->ip_protocol,
                                                   ttl, out_sync_browse);
                }
                if (search_result) {
                    //check for more applicable searches (PTR & A/AAAA at the same time)
                    while (search_result) {
                        mdns_priv_query_result_add_ip(search_result, name->host, &ip6, packet->tcpip_if,
                                                      packet->ip_protocol, ttl);
                        search_result = mdns_priv_query_find_from(search_result->next, name, type, packet->tcpip_if,
                                                                  packet->ip_protocol);
                    }
                } else if (ours) {
                    if (parsed_packet->questions && !parsed_packet->probe) {
                        remove_parsed_question(parsed_packet, type, NULL);
                        continue;
                    }
                    if (!is_name_selfhosted(name)) {
                        continue;
                    }
                    //detect collision (-1=won, 0=none, 1=lost)
                    int col = 0;
                    if (mdns_class > 1) {
                        col = 1;
                    } else if (!mdns_class) {
                        col = -1;
                    } else {
                        col = check_aaaa_collision(&(ip6.u_addr.ip6), packet->tcpip_if);
                    }
                    if (col == 2) {
                        goto clear_rx_packet;
                    } else if (col == 1) {
                        do_not_reply = true;
                        if (mdns_priv_pcb_is_probing(packet)) {
                            if (col && (parsed_packet->probe || parsed_packet->authoritative)) {
                                mdns_priv_pcb_set_probe_failed(packet);
                                char *new_host = mangle_name((char *) mdns_priv_get_global_hostname());
                                if (new_host) {
                                    mdns_priv_remap_self_service_hostname(mdns_priv_get_global_hostname(), new_host);
                                    mdns_priv_set_global_hostname(new_host);
                                }
                                mdns_priv_restart_all_pcbs();
                            }
                        } else {
                            mdns_priv_init_pcb_probe(packet->tcpip_if, packet->ip_protocol, NULL, 0, true);
                        }
                    } else if (ttl > 60 && !col && !parsed_packet->authoritative && !parsed_packet->probe && !parsed_packet->questions && !mdns_priv_pcb_is_probing(
                                   packet)) {
                        mdns_priv_remove_scheduled_answer(packet->tcpip_if, packet->ip_protocol, type, NULL);
                    }
                }

            }
#endif /* CONFIG_LWIP_IPV6 */
#ifdef CONFIG_LWIP_IPV4
            else if (type == MDNS_TYPE_A) {
                if (data_len < 4) {
                    continue; // malformed A record, RDATA smaller than an IPv4 address
                }
                esp_ip_addr_t ip;
                ip.type = ESP_IPADDR_TYPE_V4;
                memcpy(&(ip.u_addr.ip4.addr), data_ptr, 4);
                if (browse_result) {
                    mdns_priv_browse_result_add_ip(browse_result, name->host, &ip, packet->tcpip_if,
                                                   packet->ip_protocol,
                                                   ttl, out_sync_browse);
                }
                if (search_result) {
                    //check for more applicable searches (PTR & A/AAAA at the same time)
                    while (search_result) {
                        mdns_priv_query_result_add_ip(search_result, name->host, &ip, packet->tcpip_if,
                                                      packet->ip_protocol, ttl);
                        search_result = mdns_priv_query_find_from(search_result->next, name, type, packet->tcpip_if,
                                                                  packet->ip_protocol);
                    }
                } else if (ours) {
                    if (parsed_packet->questions && !parsed_packet->probe) {
                        remove_parsed_question(parsed_packet, type, NULL);
                        continue;
                    }
                    if (!is_name_selfhosted(name)) {
                        continue;
                    }
                    //detect collision (-1=won, 0=none, 1=lost)
                    int col = 0;
                    if (mdns_class > 1) {
                        col = 1;
                    } else if (!mdns_class) {
                        col = -1;
                    } else {
                        col = check_a_collision(&(ip.u_addr.ip4), packet->tcpip_if);
                    }
                    if (col == 2) {
                        goto clear_rx_packet;
                    } else if (col == 1) {
                        do_not_reply = true;
                        if (mdns_priv_pcb_is_probing(packet)) {
                            if (col && (parsed_packet->probe || parsed_packet->authoritative)) {
                                mdns_priv_pcb_set_probe_failed(packet);
                                char *new_host = mangle_name((char *) mdns_priv_get_global_hostname());
                                if (new_host) {
                                    mdns_priv_remap_self_service_hostname(mdns_priv_get_global_hostname(), new_host);
                                    mdns_priv_set_global_hostname(new_host);
                                }
                                mdns_priv_restart_all_pcbs();
                            }
                        } else {
                            mdns_priv_init_pcb_probe(packet->tcpip_if, packet->ip_protocol, NULL, 0, true);
                        }
                    } else if (ttl > 60 && !col && !parsed_packet->authoritative && !parsed_packet->probe && !parsed_packet->questions && !mdns_priv_pcb_is_probing(
                                   packet)) {
                        mdns_priv_remove_scheduled_answer(packet->tcpip_if, packet->ip_protocol, type, NULL);
                    }
                }

            }
#endif /* CONFIG_LWIP_IPV4 */
        }
        //end while
        if (parsed_packet->authoritative) {
            mdns_priv_query_done();
        }
    }

    if (!do_not_reply && mdns_priv_pcb_is_after_probing(packet) && (parsed_packet->questions || parsed_packet->discovery)) {
        mdns_priv_create_answer_from_parsed_packet(parsed_packet);
    }
    if (out_sync_browse) {
        DBG_BROWSE_RESULTS_WITH_MSG(out_sync_browse->browse->result,
                                    "Browse %s%s total result:", out_sync_browse->browse->service, out_sync_browse->browse->proto);
        if (out_sync_browse->sync_result) {
            DBG_BROWSE_RESULTS_WITH_MSG(out_sync_browse->sync_result->result,
                                        "Changed result:");
            mdns_priv_browse_sync(out_sync_browse);
        } else {
            mdns_mem_free(out_sync_browse);
        }
        out_sync_browse = NULL;
    }

clear_rx_packet:
    while (parsed_packet->questions) {
        mdns_parsed_question_t *question = parsed_packet->questions;
        parsed_packet->questions = parsed_packet->questions->next;
        if (question->host) {
            mdns_mem_free(question->host);
        }
        if (question->service) {
            mdns_mem_free(question->service);
        }
        if (question->proto) {
            mdns_mem_free(question->proto);
        }
        if (question->domain) {
            mdns_mem_free(question->domain);
        }
        mdns_mem_free(question);
    }
    while (parsed_packet->records) {
        mdns_parsed_record_t *record = parsed_packet->records;
        parsed_packet->records = parsed_packet->records->next;
        if (record->host) {
            mdns_mem_free(record->host);
        }
        if (record->service) {
            mdns_mem_free(record->service);
        }
        if (record->proto) {
            mdns_mem_free(record->proto);
        }
        record->next = NULL;
        mdns_mem_free(record);
    }
    mdns_mem_free(parsed_packet);
    mdns_mem_free(browse_result_instance);
    mdns_mem_free(browse_result_service);
    mdns_mem_free(browse_result_proto);
    mdns_mem_free(out_sync_browse);
}

void mdns_priv_receive_action(mdns_action_t *action, mdns_action_subtype_t type)
{
    if (action->type != ACTION_RX_HANDLE) {
        abort();
    }
    if (type == ACTION_RUN) {
        mdns_parse_packet(action->data.rx_handle.packet);
        mdns_priv_packet_free(action->data.rx_handle.packet);
    } else if (type == ACTION_CLEANUP) {
        mdns_priv_packet_free(action->data.rx_handle.packet);
    }
}
