/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "sdkconfig.h"
#include "mdns_private.h"
#include "mdns_querier.h"
#include "mdns_mem_caps.h"
#include "mdns_utils.h"
#include "mdns_send.h"
#include "esp_log.h"
#include "mdns_pcb.h"
#include "mdns_netif.h"
#include "mdns_responder.h"
#include "mdns_service.h"

static const char *TAG = "mdns_querier";
static mdns_search_once_t *s_search_once;

static esp_err_t send_search_action(mdns_action_type_t type, mdns_search_once_t *search);
static void search_free(mdns_search_once_t *search);

void mdns_priv_query_results_free(mdns_result_t *results)
{
    mdns_result_t *r;
    mdns_ip_addr_t *a;

    while (results) {
        r = results;

        mdns_mem_free((char *)(r->hostname));
        mdns_mem_free((char *)(r->instance_name));
        mdns_mem_free((char *)(r->service_type));
        mdns_mem_free((char *)(r->proto));

        for (size_t i = 0; i < r->txt_count; i++) {
            mdns_mem_free((char *)(r->txt[i].key));
            mdns_mem_free((char *)(r->txt[i].value));
        }
        mdns_mem_free(r->txt);
        mdns_mem_free(r->txt_value_len);

        while (r->addr) {
            a = r->addr;
            r->addr = r->addr->next;
            mdns_mem_free(a);
        }

        results = results->next;
        mdns_mem_free(r);
    }
}

/**
 * @brief  Check if a search is still in the search_once linked list.
 *         Must be called with service lock held.
 */
static bool search_is_in_list(mdns_search_once_t *search)
{
    mdns_search_once_t *s = s_search_once;
    while (s) {
        if (s == search) {
            return true;
        }
        s = s->next;
    }
    return false;
}

/**
 * @brief  Mark search as finished and remove it from search chain.
 *
 * Guards against stale pointers: mdns_query_async_delete() may have already
 * detached and freed this search while an ACTION_SEARCH_END was still queued.
 * The notifier is cleared before calling to prevent double-invocation if
 * search_finish() is reached from both mdns_priv_query_done() (max results)
 * and ACTION_SEARCH_END (timeout) for the same search.
 */
static void search_finish(mdns_search_once_t *search)
{
    if (!search_is_in_list(search)) {
        return;  /* Already freed by mdns_query_async_delete() */
    }
    search->state = SEARCH_OFF;
    queueDetach(mdns_search_once_t, s_search_once, search);
    if (search->notifier) {
        mdns_query_notify_t notifier = search->notifier;
        search->notifier = NULL;
        notifier(search);
    }
    xSemaphoreGive(search->done_semaphore);
}

/**
 * @brief  Add new search to the search chain
 */
void search_add(mdns_search_once_t *search)
{
    search->next = s_search_once;
    s_search_once = search;
}

/**
 * @brief  Send search packet to all available interfaces
 */
static void search_send(mdns_search_once_t *search)
{
    mdns_search_once_t *queue = s_search_once;
    bool found = false;
    // looking for this search in active searches
    while (queue) {
        if (queue == search) {
            found = true;
            break;
        }
        queue = queue->next;
    }

    if (!found) {
        // no longer active -> skip sending this search
        return;
    }

    uint8_t i, j;
    for (i = 0; i < MDNS_MAX_INTERFACES; i++) {
        for (j = 0; j < MDNS_IP_PROTOCOL_MAX; j++) {
            mdns_priv_query_send(search, (mdns_if_t) i, (mdns_ip_protocol_t) j);
        }
    }
}

void mdns_priv_query_action(mdns_action_t *action, mdns_action_subtype_t type)
{
    if (type == ACTION_RUN) {
        switch (action->type) {
        case ACTION_SEARCH_ADD:
            search_add(action->data.search_add.search);
            break;
        case ACTION_SEARCH_SEND:
            search_send(action->data.search_add.search);
            break;
        case ACTION_SEARCH_END:
            search_finish(action->data.search_add.search);
            break;
        default:
            abort();
        }
        return;
    }
    if (type == ACTION_CLEANUP) {
        /* ADD actions hold searches not yet in the list — always safe to free.
         * SEND/END actions reference searches that may have been removed and
         * freed by mdns_query_async_delete(). Only free if still in the list. */
        if (action->type == ACTION_SEARCH_ADD ||
                search_is_in_list(action->data.search_add.search)) {
            search_free(action->data.search_add.search);
        }
    }
}

/**
 * @brief  Called from timer task to run active searches
 */
void mdns_priv_query_start_stop(void)
{
    mdns_priv_service_lock();
    mdns_search_once_t *s = s_search_once;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (!s) {
        mdns_priv_service_unlock();
        return;
    }
    while (s) {
        if (s->state != SEARCH_OFF) {
            if (now > (s->started_at + s->timeout)) {
                s->state = SEARCH_OFF;
                if (send_search_action(ACTION_SEARCH_END, s) != ESP_OK) {
                    s->state = SEARCH_RUNNING;
                }
            } else if (s->state == SEARCH_INIT || (now - s->sent_at) > 1000) {
                s->state = SEARCH_RUNNING;
                s->sent_at = now;
                if (send_search_action(ACTION_SEARCH_SEND, s) != ESP_OK) {
                    s->sent_at -= 1000;
                }
            }
        }
        s = s->next;
    }
    mdns_priv_service_unlock();
}

void mdns_priv_query_free(void)
{
    while (s_search_once) {
        mdns_search_once_t *h = s_search_once;
        s_search_once = h->next;
        mdns_mem_free(h->instance);
        mdns_mem_free(h->service);
        mdns_mem_free(h->proto);
        vSemaphoreDelete(h->done_semaphore);
        if (h->result) {
            mdns_priv_query_results_free(h->result);
        }
        mdns_mem_free(h);
    }
}

/**
 * @brief  Called from parser to finish any searches that have reached maximum results
 */
void mdns_priv_query_done(void)
{
    mdns_search_once_t *search = s_search_once;
    mdns_search_once_t *s = NULL;
    while (search) {
        s = search;
        search = search->next;
        if (s->max_results && s->num_results >= s->max_results) {
            search_finish(s);
        }
    }
}

/**
 * @brief  Called from packet parser to find matching running search
 */
mdns_search_once_t *mdns_priv_query_find_from(mdns_search_once_t *s, mdns_name_t *name, uint16_t type, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    mdns_result_t *r = NULL;
    while (s) {
        if (s->state == SEARCH_OFF) {
            s = s->next;
            continue;
        }

        if (type == MDNS_TYPE_A || type == MDNS_TYPE_AAAA) {
            if ((s->type == MDNS_TYPE_ANY && s->service != NULL)
                    || (s->type != MDNS_TYPE_ANY && s->type != type && s->type != MDNS_TYPE_PTR && s->type != MDNS_TYPE_SRV)) {
                s = s->next;
                continue;
            }
            if (s->type != MDNS_TYPE_PTR && s->type != MDNS_TYPE_SRV) {
                if (!strcasecmp(name->host, s->instance)) {
                    return s;
                }
                s = s->next;
                continue;
            }
            r = s->result;
            while (r) {
                if (r->esp_netif == mdns_priv_get_esp_netif(tcpip_if) && r->ip_protocol == ip_protocol && !mdns_utils_str_null_or_empty(r->hostname) && !strcasecmp(name->host, r->hostname)) {
                    return s;
                }
                r = r->next;
            }
            s = s->next;
            continue;
        }

        if (type == MDNS_TYPE_SRV || type == MDNS_TYPE_TXT) {
            if ((s->type == MDNS_TYPE_ANY && s->service == NULL)
                    || (s->type != MDNS_TYPE_ANY && s->type != type && s->type != MDNS_TYPE_PTR)) {
                s = s->next;
                continue;
            }
            if (strcasecmp(name->service, s->service)
                    || strcasecmp(name->proto, s->proto)) {
                s = s->next;
                continue;
            }
            if (s->type != MDNS_TYPE_PTR) {
                if (s->instance && strcasecmp(name->host, s->instance) == 0) {
                    return s;
                }
                s = s->next;
                continue;
            }
            return s;
        }

        if (type == MDNS_TYPE_PTR && type == s->type) {
            // Multi-label service names (e.g. "_services._dns-sd") are parsed into
            // host + service fields; reconstruct the compound name for comparison.
            if (name->host[0] && s->service && strchr(s->service, '.')) {
                char compound[MDNS_NAME_BUF_LEN * 2];
                snprintf(compound, sizeof(compound), "%s.%s", name->host, name->service);
                if (!strcasecmp(compound, s->service) && !strcasecmp(name->proto, s->proto)) {
                    return s;
                }
            } else if (s->service && s->proto && !strcasecmp(name->service, s->service) && !strcasecmp(name->proto, s->proto)) {
                return s;
            }
        }

        s = s->next;
    }

    return NULL;
}

mdns_search_once_t *mdns_priv_query_find(mdns_name_t *name, uint16_t type, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    return mdns_priv_query_find_from(s_search_once, name, type, tcpip_if, ip_protocol);
}

/**
 * @brief  Create search packet for particular interface
 */
static mdns_tx_packet_t *create_search_packet(mdns_search_once_t *search, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    mdns_result_t *r = NULL;
    mdns_tx_packet_t *packet = mdns_priv_alloc_packet(tcpip_if, ip_protocol);
    if (!packet) {
        return NULL;
    }

    mdns_out_question_t *q = (mdns_out_question_t *)mdns_mem_malloc(sizeof(mdns_out_question_t));
    if (!q) {
        HOOK_MALLOC_FAILED;
        mdns_priv_free_tx_packet(packet);
        return NULL;
    }
    q->next = NULL;
    q->unicast = search->unicast;
    q->type = search->type;
    q->host = search->instance;
    q->service = search->service;
    q->proto = search->proto;
    q->domain = MDNS_UTILS_DEFAULT_DOMAIN;
    q->own_dynamic_memory = false;

    // Multi-label service names (e.g. "_services._dns-sd") need splitting into
    // separate DNS labels: host="_services", service="_dns-sd"
    if (search->service) {
        char *dot = strchr(search->service, '.');
        if (dot && dot > search->service) {
            size_t host_len = dot - search->service;
            size_t service_len = strlen(dot + 1);
            if (host_len < MDNS_NAME_BUF_LEN && service_len < MDNS_NAME_BUF_LEN && service_len > 0) {
                size_t proto_len = search->proto ? strlen(search->proto) : 0;
                size_t domain_len = strlen(MDNS_UTILS_DEFAULT_DOMAIN);
                char *host_part = mdns_mem_malloc(host_len + 1);
                char *service_part = mdns_mem_malloc(service_len + 1);
                char *proto_part = proto_len > 0 ? mdns_mem_malloc(proto_len + 1) : NULL;
                char *domain_part = mdns_mem_malloc(domain_len + 1);

                if (!host_part || !service_part || !domain_part || (proto_len && !proto_part)) {
                    ESP_LOGW(TAG, "Failed to allocate memory for multi-label service query");
                    mdns_mem_free(host_part);
                    mdns_mem_free(service_part);
                    mdns_mem_free(proto_part);
                    mdns_mem_free(domain_part);
                    mdns_mem_free(q);
                    mdns_priv_free_tx_packet(packet);
                    return NULL;
                }

                memcpy(host_part, search->service, host_len);
                host_part[host_len] = '\0';
                memcpy(service_part, dot + 1, service_len);
                service_part[service_len] = '\0';
                if (proto_part) {
                    memcpy(proto_part, search->proto, proto_len);
                    proto_part[proto_len] = '\0';
                }
                memcpy(domain_part, MDNS_UTILS_DEFAULT_DOMAIN, domain_len);
                domain_part[domain_len] = '\0';

                q->host = host_part;
                q->service = service_part;
                q->proto = proto_part;
                q->domain = domain_part;
                q->own_dynamic_memory = true;
            }
        }
    }

    queueToEnd(mdns_out_question_t, packet->questions, q);

    if (search->type == MDNS_TYPE_PTR) {
        r = search->result;
        while (r) {
            //full record on the same interface is available
            if (r->esp_netif != mdns_priv_get_esp_netif(tcpip_if) || r->ip_protocol != ip_protocol || r->instance_name == NULL || r->hostname == NULL || r->addr == NULL) {
                r = r->next;
                continue;
            }
            mdns_out_answer_t *a = (mdns_out_answer_t *)mdns_mem_malloc(sizeof(mdns_out_answer_t));
            if (!a) {
                HOOK_MALLOC_FAILED;
                mdns_priv_free_tx_packet(packet);
                return NULL;
            }
            a->type = MDNS_TYPE_PTR;
            a->service = NULL;
            a->custom_instance = r->instance_name;
            a->custom_service = search->service;
            a->custom_proto = search->proto;
            a->bye = false;
            a->flush = false;
            a->next = NULL;
            queueToEnd(mdns_out_answer_t, packet->answers, a);
            r = r->next;
        }
    }

    return packet;
}


/**
 * @brief  Send search packet to particular interface
 */
void mdns_priv_query_send(mdns_search_once_t *search, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol)
{
    mdns_tx_packet_t *packet = NULL;
    if (mdsn_priv_pcb_is_inited(tcpip_if, ip_protocol)) {
        packet = create_search_packet(search, tcpip_if, ip_protocol);
        if (!packet) {
            return;
        }
        mdns_priv_dispatch_tx_packet(packet);
        mdns_priv_free_tx_packet(packet);
    }
}

/**
 * @brief  Free search structure (except the results)
 */
static void search_free(mdns_search_once_t *search)
{
    mdns_mem_free(search->instance);
    mdns_mem_free(search->service);
    mdns_mem_free(search->proto);
    vSemaphoreDelete(search->done_semaphore);
    mdns_mem_free(search);
}

/**
 * @brief  Allocate new search structure
 */
static mdns_search_once_t *search_init(const char *name, const char *service, const char *proto, uint16_t type, bool unicast,
                                       uint32_t timeout, uint8_t max_results, mdns_query_notify_t notifier)
{
    mdns_search_once_t *search = (mdns_search_once_t *)mdns_mem_malloc(sizeof(mdns_search_once_t));
    if (!search) {
        HOOK_MALLOC_FAILED;
        return NULL;
    }
    memset(search, 0, sizeof(mdns_search_once_t));

    search->done_semaphore = xSemaphoreCreateBinary();
    if (!search->done_semaphore) {
        mdns_mem_free(search);
        return NULL;
    }

    if (!mdns_utils_str_null_or_empty(name)) {
        search->instance = mdns_mem_strndup(name, MDNS_NAME_BUF_LEN - 1);
        if (!search->instance) {
            search_free(search);
            return NULL;
        }
    }

    if (!mdns_utils_str_null_or_empty(service)) {
        search->service = mdns_mem_strndup(service, MDNS_NAME_BUF_LEN - 1);
        if (!search->service) {
            search_free(search);
            return NULL;
        }
    }

    if (!mdns_utils_str_null_or_empty(proto)) {
        search->proto = mdns_mem_strndup(proto, MDNS_NAME_BUF_LEN - 1);
        if (!search->proto) {
            search_free(search);
            return NULL;
        }
    }

    search->type = type;
    search->unicast = unicast;
    search->timeout = timeout;
    search->num_results = 0;
    search->max_results = max_results;
    search->result = NULL;
    search->state = SEARCH_INIT;
    search->sent_at = 0;
    search->started_at = xTaskGetTickCount() * portTICK_PERIOD_MS;
    search->notifier = notifier;
    search->next = NULL;

    return search;
}

/**
 * @brief  Queue search action
 */
static esp_err_t send_search_action(mdns_action_type_t type, mdns_search_once_t *search)
{
    mdns_action_t *action = NULL;

    action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
    if (!action) {
        HOOK_MALLOC_FAILED;
        return ESP_ERR_NO_MEM;
    }

    action->type = type;
    action->data.search_add.search = search;
    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(action);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/**
 * @brief  Called from parser to add TXT data to search result
 */
void mdns_priv_query_result_add_txt(mdns_search_once_t *search, mdns_txt_item_t *txt, uint8_t *txt_value_len,
                                    size_t txt_count, mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol,
                                    uint32_t ttl)
{
    mdns_result_t *r = search->result;
    while (r) {
        if (r->esp_netif == mdns_priv_get_esp_netif(tcpip_if) && r->ip_protocol == ip_protocol) {
            if (r->txt) {
                goto free_txt;
            }
            r->txt = txt;
            r->txt_value_len = txt_value_len;
            r->txt_count = txt_count;
            mdns_priv_query_update_result_ttl(r, ttl);
            return;
        }
        r = r->next;
    }
    if (!search->max_results || search->num_results < search->max_results) {
        r = (mdns_result_t *)mdns_mem_malloc(sizeof(mdns_result_t));
        if (!r) {
            HOOK_MALLOC_FAILED;
            goto free_txt;
        }

        memset(r, 0, sizeof(mdns_result_t));
        r->txt = txt;
        r->txt_value_len = txt_value_len;
        r->txt_count = txt_count;
        r->esp_netif = mdns_priv_get_esp_netif(tcpip_if);
        r->ip_protocol = ip_protocol;
        r->ttl = ttl;
        r->next = search->result;
        search->result = r;
        search->num_results++;
    }
    return;

free_txt:
    for (size_t i = 0; i < txt_count; i++) {
        mdns_mem_free((char *)(txt[i].key));
        mdns_mem_free((char *)(txt[i].value));
    }
    mdns_mem_free(txt);
    mdns_mem_free(txt_value_len);
}

/**
 * @brief  Chain new IP to search result
 */
static void result_add_ip(mdns_result_t *r, esp_ip_addr_t *ip)
{
    mdns_ip_addr_t *a = r->addr;
    while (a) {
        if (a->addr.type == ip->type) {
#ifdef CONFIG_LWIP_IPV4
            if (a->addr.type == ESP_IPADDR_TYPE_V4 && a->addr.u_addr.ip4.addr == ip->u_addr.ip4.addr) {
                return;
            }
#endif
#ifdef CONFIG_LWIP_IPV6
            if (a->addr.type == ESP_IPADDR_TYPE_V6 && !memcmp(a->addr.u_addr.ip6.addr, ip->u_addr.ip6.addr, 16)) {
                return;
            }
#endif
        }
        a = a->next;
    }
    a = mdns_priv_result_addr_create_ip(ip);
    if (!a) {
        return;
    }
    a->next = r->addr;
    r->addr = a;
}

/**
 * @brief  Called from parser to add A/AAAA data to search result
 */
void mdns_priv_query_result_add_ip(mdns_search_once_t *search, const char *hostname, esp_ip_addr_t *ip,
                                   mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, uint32_t ttl)
{
    mdns_result_t *r = NULL;
    mdns_ip_addr_t *a = NULL;

    if ((search->type == MDNS_TYPE_A && ip->type == ESP_IPADDR_TYPE_V4)
            || (search->type == MDNS_TYPE_AAAA && ip->type == ESP_IPADDR_TYPE_V6)
            || search->type == MDNS_TYPE_ANY) {
        r = search->result;
        while (r) {
            if (r->esp_netif == mdns_priv_get_esp_netif(tcpip_if) && r->ip_protocol == ip_protocol) {
                result_add_ip(r, ip);
                mdns_priv_query_update_result_ttl(r, ttl);
                return;
            }
            r = r->next;
        }
        if (!search->max_results || search->num_results < search->max_results) {
            r = (mdns_result_t *)mdns_mem_malloc(sizeof(mdns_result_t));
            if (!r) {
                HOOK_MALLOC_FAILED;
                return;
            }

            memset(r, 0, sizeof(mdns_result_t));

            a = mdns_priv_result_addr_create_ip(ip);
            if (!a) {
                mdns_mem_free(r);
                return;
            }
            a->next = r->addr;
            r->hostname = mdns_mem_strdup(hostname);
            r->addr = a;
            r->esp_netif = mdns_priv_get_esp_netif(tcpip_if);
            r->ip_protocol = ip_protocol;
            r->next = search->result;
            r->ttl = ttl;
            search->result = r;
            search->num_results++;
        }
    } else if (search->type == MDNS_TYPE_PTR || search->type == MDNS_TYPE_SRV) {
        r = search->result;
        while (r) {
            if (r->esp_netif == mdns_priv_get_esp_netif(tcpip_if) && r->ip_protocol == ip_protocol && !mdns_utils_str_null_or_empty(r->hostname) && !strcasecmp(hostname, r->hostname)) {
                result_add_ip(r, ip);
                mdns_priv_query_update_result_ttl(r, ttl);
                break;
            }
            r = r->next;
        }
    }
}

/**
 * @brief  Called from parser to add SRV data to search result
 */
void mdns_priv_query_result_add_srv(mdns_search_once_t *search, const char *hostname, uint16_t port,
                                    mdns_if_t tcpip_if, mdns_ip_protocol_t ip_protocol, uint32_t ttl)
{
    mdns_result_t *r = search->result;
    while (r) {
        if (r->esp_netif == mdns_priv_get_esp_netif(tcpip_if) && r->ip_protocol == ip_protocol && !mdns_utils_str_null_or_empty(r->hostname) && !strcasecmp(hostname, r->hostname)) {
            mdns_priv_query_update_result_ttl(r, ttl);
            return;
        }
        r = r->next;
    }
    if (!search->max_results || search->num_results < search->max_results) {
        r = (mdns_result_t *)mdns_mem_malloc(sizeof(mdns_result_t));
        if (!r) {
            HOOK_MALLOC_FAILED;
            return;
        }

        memset(r, 0, sizeof(mdns_result_t));
        r->hostname = mdns_mem_strdup(hostname);
        if (!r->hostname) {
            mdns_mem_free(r);
            return;
        }
        if (search->instance) {
            r->instance_name = mdns_mem_strdup(search->instance);
        }
        r->service_type = mdns_mem_strdup(search->service);
        r->proto = mdns_mem_strdup(search->proto);
        r->port = port;
        r->esp_netif = mdns_priv_get_esp_netif(tcpip_if);
        r->ip_protocol = ip_protocol;
        r->ttl = ttl;
        r->next = search->result;
        search->result = r;
        search->num_results++;
    }
}

/**
 * @brief  Called from parser to add PTR data to search result
 */
mdns_result_t *mdns_priv_query_result_add_ptr(mdns_search_once_t *search, const char *instance,
                                              const char *service_type, const char *proto, mdns_if_t tcpip_if,
                                              mdns_ip_protocol_t ip_protocol, uint32_t ttl)
{
    mdns_result_t *r = search->result;
    while (r) {
        if (r->esp_netif == mdns_priv_get_esp_netif(tcpip_if) && r->ip_protocol == ip_protocol && !mdns_utils_str_null_or_empty(r->instance_name) && !strcasecmp(instance, r->instance_name)) {
            mdns_priv_query_update_result_ttl(r, ttl);
            return r;
        }
        r = r->next;
    }
    if (!search->max_results || search->num_results < search->max_results) {
        r = (mdns_result_t *)mdns_mem_malloc(sizeof(mdns_result_t));
        if (!r) {
            HOOK_MALLOC_FAILED;
            return NULL;
        }

        memset(r, 0, sizeof(mdns_result_t));
        r->instance_name = mdns_mem_strdup(instance);
        r->service_type = mdns_mem_strdup(service_type);
        r->proto = mdns_mem_strdup(proto);
        if (!r->instance_name) {
            mdns_mem_free(r);
            return NULL;
        }

        r->esp_netif = mdns_priv_get_esp_netif(tcpip_if);
        r->ip_protocol = ip_protocol;
        r->ttl = ttl;
        r->next = search->result;
        search->result = r;
        search->num_results++;
        return r;
    }
    return NULL;
}

mdns_ip_addr_t *mdns_priv_result_addr_create_ip(esp_ip_addr_t *ip)
{
    mdns_ip_addr_t *a = (mdns_ip_addr_t *)mdns_mem_malloc(sizeof(mdns_ip_addr_t));
    if (!a) {
        HOOK_MALLOC_FAILED;
        return NULL;
    }
    memset(a, 0, sizeof(mdns_ip_addr_t));
    a->addr.type = ip->type;
    if (ip->type == ESP_IPADDR_TYPE_V6) {
        memcpy(a->addr.u_addr.ip6.addr, ip->u_addr.ip6.addr, 16);
    } else {
        a->addr.u_addr.ip4.addr = ip->u_addr.ip4.addr;
    }
    return a;
}

/**
 * @brief MDNS_PUBLIC_API
 * */
void mdns_query_results_free(mdns_result_t *results)
{
    mdns_priv_service_lock();
    mdns_priv_query_results_free(results);
    mdns_priv_service_unlock();
}

esp_err_t mdns_query_async_delete(mdns_search_once_t *search)
{
    if (!search) {
        return ESP_ERR_INVALID_ARG;
    }

    mdns_priv_service_lock();
    if (search->state != SEARCH_OFF) {
        mdns_priv_service_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    /* Defensively detach from the search list before freeing.
     * Normally search_finish() already called queueDetach, but in the race
     * window (timer set SEARCH_OFF, ACTION_SEARCH_END not yet processed)
     * the search may still be in the list. Without this, search_finish()
     * or the CLEANUP handler would access freed memory. */
    queueDetach(mdns_search_once_t, s_search_once, search);
    search->notifier = NULL;
    search_free(search);
    mdns_priv_service_unlock();

    return ESP_OK;
}

bool mdns_query_async_get_results(mdns_search_once_t *search, uint32_t timeout, mdns_result_t **results, uint8_t *num_results)
{
    if (xSemaphoreTake(search->done_semaphore, pdMS_TO_TICKS(timeout)) == pdTRUE) {
        if (results) {
            *results = search->result;
        }
        if (num_results) {
            *num_results = search->num_results;
        }
        return true;
    }
    return false;
}

mdns_search_once_t *mdns_query_async_new(const char *name, const char *service, const char *proto, uint16_t type,
                                         uint32_t timeout, size_t max_results, mdns_query_notify_t notifier)
{
    mdns_search_once_t *search = NULL;

    if (!mdns_priv_is_server_init() || !timeout || mdns_utils_str_null_or_empty(service) != mdns_utils_str_null_or_empty(proto)) {
        return NULL;
    }

    search = search_init(name, service, proto, type, type != MDNS_TYPE_PTR, timeout, max_results, notifier);
    if (!search) {
        return NULL;
    }

    if (send_search_action(ACTION_SEARCH_ADD, search)) {
        search_free(search);
        return NULL;
    }

    return search;
}

esp_err_t mdns_query_generic(const char *name, const char *service, const char *proto, uint16_t type, mdns_query_transmission_type_t transmission_type, uint32_t timeout, size_t max_results, mdns_result_t **results)
{
    mdns_search_once_t *search = NULL;

    *results = NULL;

    if (!mdns_priv_is_server_init()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!timeout || mdns_utils_str_null_or_empty(service) != mdns_utils_str_null_or_empty(proto)) {
        return ESP_ERR_INVALID_ARG;
    }

    search = search_init(name, service, proto, type, transmission_type == MDNS_QUERY_UNICAST, timeout, max_results,
                         NULL);
    if (!search) {
        return ESP_ERR_NO_MEM;
    }

    if (send_search_action(ACTION_SEARCH_ADD, search)) {
        search_free(search);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(search->done_semaphore, portMAX_DELAY);

    *results = search->result;
    search_free(search);

    return ESP_OK;
}

esp_err_t mdns_query(const char *name, const char *service_type, const char *proto, uint16_t type, uint32_t timeout, size_t max_results, mdns_result_t **results)
{
    // PTR queries should be multicast, all other types should be unicast
    mdns_query_transmission_type_t transmission_type = (type == MDNS_TYPE_PTR) ? MDNS_QUERY_MULTICAST : MDNS_QUERY_UNICAST;
    return mdns_query_generic(name, service_type, proto, type, transmission_type, timeout, max_results, results);
}

esp_err_t mdns_query_ptr(const char *service, const char *proto, uint32_t timeout, size_t max_results, mdns_result_t **results)
{
    if (mdns_utils_str_null_or_empty(service) || mdns_utils_str_null_or_empty(proto)) {
        return ESP_ERR_INVALID_ARG;
    }

    return mdns_query(NULL, service, proto, MDNS_TYPE_PTR, timeout, max_results, results);
}

esp_err_t mdns_query_srv(const char *instance, const char *service, const char *proto, uint32_t timeout, mdns_result_t **result)
{
    if (mdns_utils_str_null_or_empty(instance) || mdns_utils_str_null_or_empty(service) || mdns_utils_str_null_or_empty(proto)) {
        return ESP_ERR_INVALID_ARG;
    }

    return mdns_query(instance, service, proto, MDNS_TYPE_SRV, timeout, 1, result);
}

esp_err_t mdns_query_txt(const char *instance, const char *service, const char *proto, uint32_t timeout, mdns_result_t **result)
{
    if (mdns_utils_str_null_or_empty(instance) || mdns_utils_str_null_or_empty(service) || mdns_utils_str_null_or_empty(proto)) {
        return ESP_ERR_INVALID_ARG;
    }

    return mdns_query(instance, service, proto, MDNS_TYPE_TXT, timeout, 1, result);
}

#ifdef CONFIG_LWIP_IPV4
esp_err_t mdns_query_a(const char *name, uint32_t timeout, esp_ip4_addr_t *addr)
{
    mdns_result_t *result = NULL;
    esp_err_t err;

    if (mdns_utils_str_null_or_empty(name)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strstr(name, ".local")) {
        ESP_LOGW(TAG, "Please note that hostname must not contain domain name, as mDNS uses '.local' domain");
    }

    err = mdns_query(name, NULL, NULL, MDNS_TYPE_A, timeout, 1, &result);

    if (err) {
        return err;
    }

    if (!result) {
        return ESP_ERR_NOT_FOUND;
    }

    mdns_ip_addr_t *a = result->addr;
    while (a) {
        if (a->addr.type == ESP_IPADDR_TYPE_V4) {
            addr->addr = a->addr.u_addr.ip4.addr;
            mdns_query_results_free(result);
            return ESP_OK;
        }
        a = a->next;
    }

    mdns_query_results_free(result);
    return ESP_ERR_NOT_FOUND;
}
#endif /* CONFIG_LWIP_IPV4 */

#ifdef CONFIG_LWIP_IPV6
esp_err_t mdns_query_aaaa(const char *name, uint32_t timeout, esp_ip6_addr_t *addr)
{
    mdns_result_t *result = NULL;
    esp_err_t err;

    if (mdns_utils_str_null_or_empty(name)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strstr(name, ".local")) {
        ESP_LOGW(TAG, "Please note that hostname must not contain domain name, as mDNS uses '.local' domain");
    }

    err = mdns_query(name, NULL, NULL, MDNS_TYPE_AAAA, timeout, 1, &result);

    if (err) {
        return err;
    }

    if (!result) {
        return ESP_ERR_NOT_FOUND;
    }

    mdns_ip_addr_t *a = result->addr;
    while (a) {
        if (a->addr.type == ESP_IPADDR_TYPE_V6) {
            memcpy(addr->addr, a->addr.u_addr.ip6.addr, 16);
            mdns_query_results_free(result);
            return ESP_OK;
        }
        a = a->next;
    }

    mdns_query_results_free(result);
    return ESP_ERR_NOT_FOUND;
}
#endif /* CONFIG_LWIP_IPV6 */
