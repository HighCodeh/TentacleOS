/*
 * SPDX-FileCopyrightText: 2015-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "mdns.h"
#include "mdns_private.h"
#include "mdns_mem_caps.h"
#include "mdns_utils.h"
#include "mdns_send.h"
#include "mdns_querier.h"
#include "mdns_pcb.h"
#include "mdns_service.h"

typedef struct mdns_server_s {
    const char *hostname;
    const char *instance;
    mdns_srv_item_t *services;
    mdns_host_item_t *host_list;
    mdns_host_item_t self_host;
    SemaphoreHandle_t action_sema;
} mdns_server_t;

static const char *TAG = "mdns_responder";
static mdns_server_t *s_server = NULL;

esp_err_t mdns_priv_responder_init(void)
{
    s_server = (mdns_server_t *)mdns_mem_malloc(sizeof(mdns_server_t));
    if (!s_server) {
        HOOK_MALLOC_FAILED;
        return ESP_ERR_NO_MEM;
    }
    memset((uint8_t *)s_server, 0, sizeof(mdns_server_t));

    s_server->action_sema = xSemaphoreCreateBinary();
    if (!s_server->action_sema) {
        mdns_mem_free(s_server);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void free_delegated_hostnames(void)
{
    mdns_host_item_t *host = s_server->host_list;
    while (host != NULL) {
        mdns_utils_free_address_list(host->address_list);
        mdns_mem_free((char *)host->hostname);
        mdns_host_item_t *item = host;
        host = host->next;
        mdns_mem_free(item);
    }
    s_server->host_list = NULL;
}

void mdns_priv_responder_free(void)
{
    if (s_server->action_sema != NULL) {
        vSemaphoreDelete(s_server->action_sema);
        s_server->action_sema = NULL;
    }
    if (s_server == NULL) {
        return;
    }
    mdns_mem_free((char *)s_server->hostname);
    mdns_mem_free((char *)s_server->instance);
    free_delegated_hostnames();
    mdns_mem_free(s_server);
    s_server = NULL;
}

const char *mdns_priv_get_global_hostname(void)
{
    return s_server ? s_server->hostname : NULL;
}

mdns_srv_item_t *mdns_priv_get_services(void)
{
    return s_server ? s_server->services : NULL;
}

mdns_host_item_t *mdns_priv_get_hosts(void)
{
    return s_server ? s_server->host_list : NULL;
}

mdns_host_item_t *mdns_priv_get_self_host(void)
{
    return s_server ? &s_server->self_host : NULL;
}

void mdns_priv_set_global_hostname(const char *hostname)
{
    if (s_server) {
        if (s_server->hostname) {
            mdns_mem_free((void *)s_server->hostname);
        }
        s_server->hostname = hostname;
        s_server->self_host.hostname = hostname;
    }
}

const char *mdns_priv_get_instance(void)
{
    return s_server ? s_server->instance : NULL;
}

void mdns_priv_set_instance(const char *instance)
{
    if (s_server) {
        if (s_server->instance) {
            mdns_mem_free((void *)s_server->instance);
        }
        s_server->instance = instance;
    }
}

bool mdns_priv_is_server_init(void)
{
    return s_server != NULL;
}

static bool can_add_more_services(void)
{
#if MDNS_MAX_SERVICES == 0
    return false;
#else
    mdns_srv_item_t *s = s_server->services;
    uint16_t service_num = 0;
    while (s) {
        service_num ++;
        s = s->next;
        if (service_num >= MDNS_MAX_SERVICES) {
            return false;
        }
    }
    return true;
#endif
}

/**
 * @brief  Send announcement on all active PCBs
 */
static void announce_all_pcbs(mdns_srv_item_t **services, size_t len, bool include_ip)
{
    uint8_t i, j;
    for (i = 0; i < MDNS_MAX_INTERFACES; i++) {
        for (j = 0; j < MDNS_IP_PROTOCOL_MAX; j++) {
            mdns_priv_pcb_announce((mdns_if_t) i, (mdns_ip_protocol_t) j, services, len, include_ip);
        }
    }
}

/**
 * @brief  Send bye to all services
 */
static void send_final_bye(bool include_ip)
{
    //collect all services to send bye packet
    size_t srv_count = 0;
    mdns_srv_item_t *a = s_server->services;
    while (a) {
        srv_count++;
        a = a->next;
    }
    if (!srv_count) {
        return;
    }
    mdns_srv_item_t *services[srv_count];
    size_t i = 0;
    a = s_server->services;
    while (a) {
        services[i++] = a;
        a = a->next;
    }
    mdns_priv_pcb_send_bye_service(services, srv_count, include_ip);
}

/**
 * @brief  Stop the responder on all services without instance
 */
static void send_bye_all_pcbs_no_instance(bool include_ip)
{
    size_t srv_count = 0;
    mdns_srv_item_t *a = s_server->services;
    while (a) {
        if (!a->service->instance) {
            srv_count++;
        }
        a = a->next;
    }
    if (!srv_count) {
        return;
    }
    mdns_srv_item_t *services[srv_count];
    size_t i = 0;
    a = s_server->services;
    while (a) {
        if (!a->service->instance) {
            services[i++] = a;
        }
        a = a->next;
    }
    mdns_priv_pcb_send_bye_service(services, srv_count, include_ip);
}

void mdns_priv_restart_all_pcbs_no_instance(void)
{
    size_t srv_count = 0;
    mdns_srv_item_t *a = s_server->services;
    while (a) {
        if (!a->service->instance) {
            srv_count++;
        }
        a = a->next;
    }
    if (!srv_count) {
        return;
    }
    mdns_srv_item_t *services[srv_count];
    size_t i = 0;
    a = s_server->services;
    while (a) {
        if (!a->service->instance) {
            services[i++] = a;
        }
        a = a->next;
    }
    mdns_priv_probe_all_pcbs(services, srv_count, false, true);
}

void mdns_priv_restart_all_pcbs(void)
{
    mdns_priv_clear_tx_queue();
    size_t srv_count = 0;
    mdns_srv_item_t *a = s_server->services;
    while (a) {
        srv_count++;
        a = a->next;
    }
    if (srv_count == 0) {
        mdns_priv_probe_all_pcbs(NULL, 0, true, true);
        return;
    }
    mdns_srv_item_t *services[srv_count];
    size_t l = 0;
    a = s_server->services;
    while (a) {
        services[l++] = a;
        a = a->next;
    }

    mdns_priv_probe_all_pcbs(services, srv_count, true, true);
}

/**
 * @brief  creates/allocates new text item list
 * @param  num_items     service number of txt items or 0
 * @param  txt           service txt items array or NULL
 *
 * @return pointer to the linked txt item list or NULL
 */
static mdns_txt_linked_item_t *allocate_txt(size_t num_items, mdns_txt_item_t txt[])
{
    mdns_txt_linked_item_t *new_txt = NULL;
    size_t i = 0;
    if (num_items) {
        for (i = 0; i < num_items; i++) {
            mdns_txt_linked_item_t *new_item = (mdns_txt_linked_item_t *)mdns_mem_malloc(sizeof(mdns_txt_linked_item_t));
            if (!new_item) {
                HOOK_MALLOC_FAILED;
                break;
            }
            new_item->key = mdns_mem_strdup(txt[i].key);
            if (!new_item->key) {
                mdns_mem_free(new_item);
                break;
            }
            if (txt[i].value) {
                new_item->value = mdns_mem_strdup(txt[i].value);
                if (!new_item->value) {
                    mdns_mem_free((char *)new_item->key);
                    mdns_mem_free(new_item);
                    break;
                }
                new_item->value_len = strlen(new_item->value);
            } else {
                new_item->value = NULL;
                new_item->value_len = 0;
            }
            new_item->next = new_txt;
            new_txt = new_item;
        }
    }
    return new_txt;
}

/**
 * @brief Deallocate the txt linked list
 * @param txt pointer to the txt pointer to free, noop if txt==NULL
 */
static void free_linked_txt(mdns_txt_linked_item_t *txt)
{
    mdns_txt_linked_item_t *t;
    while (txt) {
        t = txt;
        txt = txt->next;
        mdns_mem_free((char *)t->value);
        mdns_mem_free((char *)t->key);
        mdns_mem_free(t);
    }
}

/**
 * @brief  creates/allocates new service
 * @param  service       service type
 * @param  proto         service proto
 * @param  hostname      service hostname
 * @param  port          service port
 * @param  instance      service instance
 * @param  num_items     service number of txt items or 0
 * @param  txt           service txt items array or NULL
 *
 * @return pointer to the service or NULL on error
 */
static mdns_service_t *create_service(const char *service, const char *proto, const char *hostname,
                                      uint16_t port, const char *instance, size_t num_items,
                                      mdns_txt_item_t txt[])
{
    mdns_service_t *s = (mdns_service_t *)mdns_mem_calloc(1, sizeof(mdns_service_t));
    if (!s) {
        HOOK_MALLOC_FAILED;
        return NULL;
    }

    mdns_txt_linked_item_t *new_txt = allocate_txt(num_items, txt);
    if (num_items && new_txt == NULL) {
        goto fail;
    }

    s->priority = 0;
    s->weight = 0;
    s->instance = instance ? mdns_mem_strndup(instance, MDNS_NAME_BUF_LEN - 1) : NULL;
    s->txt = new_txt;
    s->port = port;
    s->subtype = NULL;

    if (hostname) {
        s->hostname = mdns_mem_strndup(hostname, MDNS_NAME_BUF_LEN - 1);
        if (!s->hostname) {
            goto fail;
        }
    } else {
        s->hostname = NULL;
    }

    s->service = mdns_mem_strndup(service, MDNS_NAME_BUF_LEN - 1);
    if (!s->service) {
        goto fail;
    }

    s->proto = mdns_mem_strndup(proto, MDNS_NAME_BUF_LEN - 1);
    if (!s->proto) {
        goto fail;
    }
    return s;

fail:
    free_linked_txt(s->txt);
    mdns_mem_free((char *)s->instance);
    mdns_mem_free((char *)s->service);
    mdns_mem_free((char *)s->proto);
    mdns_mem_free((char *)s->hostname);
    mdns_mem_free(s);

    return NULL;
}

static void free_subtype(mdns_subtype_t *subtype)
{
    while (subtype) {
        mdns_subtype_t *next = subtype->next;
        mdns_mem_free((char *)subtype->subtype);
        mdns_mem_free(subtype);
        subtype = next;
    }
}

static void free_service_subtype(mdns_service_t *service)
{
    free_subtype(service->subtype);
    service->subtype = NULL;
}

/**
 * @brief  free service memory
 *
 * @param  service      the service
 */
static void free_service(mdns_service_t *service)
{
    if (!service) {
        return;
    }
    mdns_mem_free((char *)service->instance);
    mdns_mem_free((char *)service->service);
    mdns_mem_free((char *)service->proto);
    mdns_mem_free((char *)service->hostname);
    while (service->txt) {
        mdns_txt_linked_item_t *s = service->txt;
        service->txt = service->txt->next;
        mdns_mem_free((char *)s->key);
        mdns_mem_free((char *)s->value);
        mdns_mem_free(s);
    }
    free_service_subtype(service);
    mdns_mem_free(service);
}

bool mdns_priv_delegate_hostname_add(const char *hostname, mdns_ip_addr_t *address_list)
{
    if (mdns_utils_hostname_is_ours(hostname)) {
        return false;
    }

    mdns_host_item_t *host = (mdns_host_item_t *)mdns_mem_malloc(sizeof(mdns_host_item_t));

    if (host == NULL) {
        return false;
    }
    host->address_list = address_list;
    host->hostname = hostname;
    host->next = s_server->host_list;
    s_server->host_list = host;
    return true;
}

static bool delegate_hostname_set_address(const char *hostname, mdns_ip_addr_t *address_list)
{
    if (!mdns_utils_str_null_or_empty(s_server->hostname) &&
            strcasecmp(hostname, s_server->hostname) == 0) {
        return false;
    }
    mdns_host_item_t *host = s_server->host_list;
    while (host != NULL) {
        if (strcasecmp(hostname, host->hostname) == 0) {
            // free previous address list
            mdns_utils_free_address_list(host->address_list);
            // set current address list to the host
            host->address_list = address_list;
            return true;
        }
        host = host->next;
    }
    return false;
}

static bool delegate_hostname_remove(const char *hostname)
{
    mdns_srv_item_t *srv = s_server->services;
    mdns_srv_item_t *prev_srv = NULL;
    while (srv) {
        if (strcasecmp(srv->service->hostname, hostname) == 0) {
            mdns_srv_item_t *to_free = srv;
            mdns_priv_pcb_send_bye_service(&srv, 1, false);
            mdns_priv_remove_scheduled_service_packets(srv->service);
            if (prev_srv == NULL) {
                s_server->services = srv->next;
                srv = srv->next;
            } else {
                prev_srv->next = srv->next;
                srv = srv->next;
            }
            free_service(to_free->service);
            mdns_mem_free(to_free);
        } else {
            prev_srv = srv;
            srv = srv->next;
        }
    }
    mdns_host_item_t *host = s_server->host_list;
    mdns_host_item_t *prev_host = NULL;
    while (host != NULL) {
        if (strcasecmp(hostname, host->hostname) == 0) {
            if (prev_host == NULL) {
                s_server->host_list = host->next;
            } else {
                prev_host->next = host->next;
            }
            mdns_utils_free_address_list(host->address_list);
            mdns_mem_free((char *)host->hostname);
            mdns_mem_free(host);
            break;
        } else {
            prev_host = host;
            host = host->next;
        }
    }
    return true;
}

void mdns_priv_remap_self_service_hostname(const char *old_hostname, const char *new_hostname)
{
    mdns_srv_item_t *service = s_server->services;

    while (service) {
        if (service->service->hostname &&
                strcmp(service->service->hostname, old_hostname) == 0) {
            mdns_mem_free((char *)service->service->hostname);
            service->service->hostname = mdns_mem_strdup(new_hostname);
        }
        service = service->next;
    }
}

void mdns_priv_responder_action(mdns_action_t *action, mdns_action_subtype_t type)
{
    if (type == ACTION_RUN) {
        switch (action->type) {
        case ACTION_HOSTNAME_SET:
            send_bye_all_pcbs_no_instance(true);
            mdns_priv_remap_self_service_hostname(s_server->hostname, action->data.hostname_set.hostname);
            mdns_mem_free((char *)s_server->hostname);
            s_server->hostname = action->data.hostname_set.hostname;
            s_server->self_host.hostname = action->data.hostname_set.hostname;
            mdns_priv_restart_all_pcbs();
            xSemaphoreGive(s_server->action_sema);
            break;
        case ACTION_INSTANCE_SET:
            send_bye_all_pcbs_no_instance(false);
            mdns_mem_free((char *)s_server->instance);
            s_server->instance = action->data.instance;
            mdns_priv_restart_all_pcbs_no_instance();
            break;
        case ACTION_DELEGATE_HOSTNAME_ADD:
            if (!mdns_priv_delegate_hostname_add(action->data.delegate_hostname.hostname,
                                                 action->data.delegate_hostname.address_list)) {
                mdns_mem_free((char *)action->data.delegate_hostname.hostname);
                mdns_utils_free_address_list(action->data.delegate_hostname.address_list);
            }
            xSemaphoreGive(s_server->action_sema);
            break;
        case ACTION_DELEGATE_HOSTNAME_SET_ADDR:
            if (!delegate_hostname_set_address(action->data.delegate_hostname.hostname,
                                               action->data.delegate_hostname.address_list)) {
                mdns_utils_free_address_list(action->data.delegate_hostname.address_list);
            }
            mdns_mem_free((char *)action->data.delegate_hostname.hostname);
            break;
        case ACTION_DELEGATE_HOSTNAME_REMOVE:
            delegate_hostname_remove(action->data.delegate_hostname.hostname);
            mdns_mem_free((char *)action->data.delegate_hostname.hostname);
            break;
        default:
            abort();
        }
        return;
    }
    if (type == ACTION_CLEANUP) {
        switch (action->type) {
        case ACTION_HOSTNAME_SET:
            mdns_mem_free(action->data.hostname_set.hostname);
            break;
        case ACTION_INSTANCE_SET:
            mdns_mem_free(action->data.instance);
            break;
        case ACTION_DELEGATE_HOSTNAME_SET_ADDR:
        case ACTION_DELEGATE_HOSTNAME_ADD:
            mdns_mem_free((char *)action->data.delegate_hostname.hostname);
            mdns_utils_free_address_list(action->data.delegate_hostname.address_list);
            break;
        case ACTION_DELEGATE_HOSTNAME_REMOVE:
            mdns_mem_free((char *)action->data.delegate_hostname.hostname);
            break;
        default:
            abort();
        }
        return;
    }
}

/**
 * @ingroup PUBLIC_API
 */
esp_err_t mdns_hostname_set(const char *hostname)
{
    if (!s_server) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mdns_utils_str_null_or_empty(hostname) || strlen(hostname) > (MDNS_NAME_BUF_LEN - 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    char *new_hostname = mdns_mem_strndup(hostname, MDNS_NAME_BUF_LEN - 1);
    if (!new_hostname) {
        return ESP_ERR_NO_MEM;
    }

    mdns_action_t *action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
    if (!action) {
        HOOK_MALLOC_FAILED;
        mdns_mem_free(new_hostname);
        return ESP_ERR_NO_MEM;
    }
    action->type = ACTION_HOSTNAME_SET;
    action->data.hostname_set.hostname = new_hostname;
    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(new_hostname);
        mdns_mem_free(action);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_server->action_sema, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t mdns_hostname_get(char *hostname)
{
    if (!hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_server || !s_server->hostname) {
        return ESP_ERR_INVALID_STATE;
    }

    mdns_priv_service_lock();
    size_t len = strnlen(s_server->hostname, MDNS_NAME_BUF_LEN - 1);
    strncpy(hostname, s_server->hostname, len);
    hostname[len] = 0;
    mdns_priv_service_unlock();
    return ESP_OK;
}

esp_err_t mdns_delegate_hostname_add(const char *hostname, const mdns_ip_addr_t *address_list)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mdns_utils_str_null_or_empty(hostname) || strlen(hostname) > (MDNS_NAME_BUF_LEN - 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    char *new_hostname = mdns_mem_strndup(hostname, MDNS_NAME_BUF_LEN - 1);
    if (!new_hostname) {
        return ESP_ERR_NO_MEM;
    }

    mdns_action_t *action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
    if (!action) {
        HOOK_MALLOC_FAILED;
        mdns_mem_free(new_hostname);
        return ESP_ERR_NO_MEM;
    }
    action->type = ACTION_DELEGATE_HOSTNAME_ADD;
    action->data.delegate_hostname.hostname = new_hostname;
    action->data.delegate_hostname.address_list = mdns_utils_copy_address_list(address_list);
    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(new_hostname);
        mdns_mem_free(action);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_server->action_sema, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t mdns_delegate_hostname_remove(const char *hostname)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mdns_utils_str_null_or_empty(hostname) || strlen(hostname) > (MDNS_NAME_BUF_LEN - 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    char *new_hostname = mdns_mem_strndup(hostname, MDNS_NAME_BUF_LEN - 1);
    if (!new_hostname) {
        return ESP_ERR_NO_MEM;
    }

    mdns_action_t *action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
    if (!action) {
        HOOK_MALLOC_FAILED;
        mdns_mem_free(new_hostname);
        return ESP_ERR_NO_MEM;
    }
    action->type = ACTION_DELEGATE_HOSTNAME_REMOVE;
    action->data.delegate_hostname.hostname = new_hostname;
    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(new_hostname);
        mdns_mem_free(action);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t mdns_delegate_hostname_set_address(const char *hostname, const mdns_ip_addr_t *address_list)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mdns_utils_str_null_or_empty(hostname) || strlen(hostname) > (MDNS_NAME_BUF_LEN - 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    char *new_hostname = mdns_mem_strndup(hostname, MDNS_NAME_BUF_LEN - 1);
    if (!new_hostname) {
        return ESP_ERR_NO_MEM;
    }

    mdns_action_t *action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
    if (!action) {
        HOOK_MALLOC_FAILED;
        mdns_mem_free(new_hostname);
        return ESP_ERR_NO_MEM;
    }
    action->type = ACTION_DELEGATE_HOSTNAME_SET_ADDR;
    action->data.delegate_hostname.hostname = new_hostname;
    action->data.delegate_hostname.address_list = mdns_utils_copy_address_list(address_list);
    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(new_hostname);
        mdns_mem_free(action);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool mdns_hostname_exists(const char *hostname)
{
    bool ret = false;
    mdns_priv_service_lock();
    ret = mdns_utils_hostname_is_ours(hostname);
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_instance_name_set(const char *instance)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mdns_utils_str_null_or_empty(instance) || s_server->hostname == NULL || strlen(instance) > (MDNS_NAME_BUF_LEN - 1)) {
        return ESP_ERR_INVALID_ARG;
    }
    char *new_instance = mdns_mem_strndup(instance, MDNS_NAME_BUF_LEN - 1);
    if (!new_instance) {
        return ESP_ERR_NO_MEM;
    }

    mdns_action_t *action = (mdns_action_t *)mdns_mem_malloc(sizeof(mdns_action_t));
    if (!action) {
        HOOK_MALLOC_FAILED;
        mdns_mem_free(new_instance);
        return ESP_ERR_NO_MEM;
    }
    action->type = ACTION_INSTANCE_SET;
    action->data.instance = new_instance;
    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(new_instance);
        mdns_mem_free(action);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t mdns_service_add_for_host(const char *instance, const char *service, const char *proto, const char *host,
                                    uint16_t port, mdns_txt_item_t txt[], size_t num_items)
{
    if (!s_server || mdns_utils_str_null_or_empty(service) || mdns_utils_str_null_or_empty(proto) || !s_server->hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    const char *hostname = host ? host : s_server->hostname;
    mdns_service_t *s = NULL;

    ESP_GOTO_ON_FALSE(can_add_more_services(), ESP_ERR_NO_MEM, err, TAG,
                      "Cannot add more services, please increase CONFIG_MDNS_MAX_SERVICES (%d)", CONFIG_MDNS_MAX_SERVICES);

    mdns_srv_item_t *item = mdns_utils_get_service_item_instance(instance, service, proto, hostname);
    ESP_GOTO_ON_FALSE(!item, ESP_ERR_INVALID_ARG, err, TAG, "Service already exists");

    s = create_service(service, proto, hostname, port, instance, num_items, txt);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NO_MEM, err, TAG, "Cannot create service: Out of memory");

    item = (mdns_srv_item_t *)mdns_mem_malloc(sizeof(mdns_srv_item_t));
    ESP_GOTO_ON_FALSE(item, ESP_ERR_NO_MEM, err, TAG, "Cannot create service: Out of memory");

    item->service = s;
    item->next = NULL;

    item->next = s_server->services;
    s_server->services = item;
    mdns_priv_probe_all_pcbs(&item, 1, false, false);
    mdns_priv_service_unlock();
    return ESP_OK;

err:
    mdns_priv_service_unlock();
    free_service(s);
    if (ret == ESP_ERR_NO_MEM) {
        HOOK_MALLOC_FAILED;
    }
    return ret;
}

esp_err_t mdns_service_add(const char *instance, const char *service, const char *proto, uint16_t port,
                           mdns_txt_item_t txt[], size_t num_items)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_add_for_host(instance, service, proto, NULL, port, txt, num_items);
}

bool mdns_service_exists(const char *service_type, const char *proto, const char *hostname)
{
    bool ret = false;
    mdns_priv_service_lock();
    ret = mdns_utils_get_service_item(service_type, proto, hostname) != NULL;
    mdns_priv_service_unlock();
    return ret;
}

bool mdns_service_exists_with_instance(const char *instance, const char *service_type, const char *proto,
                                       const char *hostname)
{
    bool ret = false;
    mdns_priv_service_lock();
    ret = mdns_utils_get_service_item_instance(instance, service_type, proto, hostname) != NULL;
    mdns_priv_service_unlock();
    return ret;
}

static mdns_txt_item_t *copy_txt_items(mdns_txt_linked_item_t *items, uint8_t **txt_value_len, size_t *txt_count)
{
    mdns_txt_item_t *ret = NULL;
    size_t ret_index = 0;
    for (mdns_txt_linked_item_t *tmp = items; tmp != NULL; tmp = tmp->next) {
        ret_index++;
    }
    *txt_count = ret_index;
    if (ret_index == 0) {   // handle empty TXT
        *txt_value_len = NULL;
        return NULL;
    }
    ret = (mdns_txt_item_t *)mdns_mem_calloc(ret_index, sizeof(mdns_txt_item_t));
    *txt_value_len = (uint8_t *)mdns_mem_calloc(ret_index, sizeof(uint8_t));
    if (!ret || !(*txt_value_len)) {
        HOOK_MALLOC_FAILED;
        goto handle_error;
    }
    ret_index = 0;
    for (mdns_txt_linked_item_t *tmp = items; tmp != NULL; tmp = tmp->next) {
        size_t key_len = strlen(tmp->key);
        char *key = (char *)mdns_mem_malloc(key_len + 1);
        if (!key) {
            HOOK_MALLOC_FAILED;
            goto handle_error;
        }
        memcpy(key, tmp->key, key_len);
        key[key_len] = 0;
        ret[ret_index].key = key;
        char *value = (char *)mdns_mem_malloc(tmp->value_len + 1);
        if (!value) {
            HOOK_MALLOC_FAILED;
            goto handle_error;
        }
        memcpy(value, tmp->value, tmp->value_len);
        value[tmp->value_len] = 0;
        ret[ret_index].value = value;
        (*txt_value_len)[ret_index] = tmp->value_len;
        ret_index++;
    }
    return ret;

handle_error:
    for (size_t y = 0; y < ret_index + 1 && ret != NULL; y++) {
        mdns_txt_item_t *t = &ret[y];
        mdns_mem_free((char *)t->key);
        mdns_mem_free((char *)t->value);
    }
    mdns_mem_free(*txt_value_len);
    mdns_mem_free(ret);
    return NULL;
}

static mdns_ip_addr_t *copy_delegated_host_address_list(char *hostname)
{
    mdns_host_item_t *host = s_server->host_list;
    while (host) {
        if (strcasecmp(host->hostname, hostname) == 0) {
            return mdns_utils_copy_address_list(host->address_list);
        }
        host = host->next;
    }
    return NULL;
}

static mdns_result_t *lookup_service(const char *instance, const char *service, const char *proto, size_t max_results, bool selfhost)
{
    if (mdns_utils_str_null_or_empty(service) || mdns_utils_str_null_or_empty(proto)) {
        return NULL;
    }
    mdns_result_t *results = NULL;
    size_t num_results = 0;
    mdns_srv_item_t *s = s_server->services;
    while (s) {
        mdns_service_t *srv = s->service;
        if (!srv || !srv->hostname) {
            s = s->next;
            continue;
        }
        bool is_service_selfhosted = !mdns_utils_str_null_or_empty(s_server->hostname) && !strcasecmp(s_server->hostname, srv->hostname);
        bool is_service_delegated = mdns_utils_str_null_or_empty(s_server->hostname) || strcasecmp(s_server->hostname, srv->hostname);
        if ((selfhost && is_service_selfhosted) || (!selfhost && is_service_delegated)) {
            if (!strcasecmp(srv->service, service) && !strcasecmp(srv->proto, proto) &&
                    (mdns_utils_str_null_or_empty(instance) || mdns_utils_instance_name_match(srv->instance, instance))) {
                mdns_result_t *item = (mdns_result_t *)mdns_mem_malloc(sizeof(mdns_result_t));
                if (!item) {
                    HOOK_MALLOC_FAILED;
                    goto handle_error;
                }
                item->next = results;
                results = item;
                item->esp_netif = NULL;
                item->ttl = mdns_utils_str_null_or_empty(instance) ? MDNS_ANSWER_PTR_TTL : MDNS_ANSWER_SRV_TTL;
                item->ip_protocol = MDNS_IP_PROTOCOL_MAX;
                if (srv->instance) {
                    item->instance_name = mdns_mem_strndup(srv->instance, MDNS_NAME_BUF_LEN - 1);
                    if (!item->instance_name) {
                        HOOK_MALLOC_FAILED;
                        goto handle_error;
                    }
                } else {
                    item->instance_name = NULL;
                }
                item->service_type = mdns_mem_strndup(srv->service, MDNS_NAME_BUF_LEN - 1);
                if (!item->service_type) {
                    HOOK_MALLOC_FAILED;
                    goto handle_error;
                }
                item->proto = mdns_mem_strndup(srv->proto, MDNS_NAME_BUF_LEN - 1);
                if (!item->proto) {
                    HOOK_MALLOC_FAILED;
                    goto handle_error;
                }
                item->hostname = mdns_mem_strndup(srv->hostname, MDNS_NAME_BUF_LEN - 1);
                if (!item->hostname) {
                    HOOK_MALLOC_FAILED;
                    goto handle_error;
                }
                item->port = srv->port;
                item->txt = copy_txt_items(srv->txt, &(item->txt_value_len), &(item->txt_count));
                // We should not append addresses for selfhost lookup result as we don't know which interface's address to append.
                if (selfhost) {
                    item->addr = NULL;
                } else {
                    item->addr = copy_delegated_host_address_list(item->hostname);
                    if (!item->addr) {
                        goto handle_error;
                    }
                }
                if (num_results < max_results) {
                    num_results++;
                }
                if (num_results >= max_results) {
                    break;
                }
            }
        }
        s = s->next;
    }
    return results;
handle_error:
    mdns_priv_query_results_free(results);
    return NULL;
}

esp_err_t mdns_service_port_set_for_host(const char *instance, const char *service, const char *proto, const char *host, uint16_t port)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    const char *hostname = host ? host : s_server->hostname;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto) && port,
                      ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");
    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    s->service->port = port;
    announce_all_pcbs(&s, 1, true);

err:
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_service_port_set(const char *service, const char *proto, uint16_t port)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_port_set_for_host(NULL, service, proto, NULL, port);
}

esp_err_t mdns_service_txt_set_for_host(const char *instance, const char *service, const char *proto, const char *host,
                                        mdns_txt_item_t txt_items[], uint8_t num_items)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    const char *hostname = host ? host : s_server->hostname;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto) && !(num_items && txt_items == NULL),
                      ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");
    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    mdns_txt_linked_item_t *new_txt = NULL;
    if (num_items) {
        new_txt = allocate_txt(num_items, txt_items);
        if (!new_txt) {
            return ESP_ERR_NO_MEM;
        }
    }
    mdns_service_t *srv = s->service;
    mdns_txt_linked_item_t *txt = srv->txt;
    srv->txt = NULL;
    free_linked_txt(txt);
    srv->txt = new_txt;
    announce_all_pcbs(&s, 1, false);

err:
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_service_txt_set(const char *service, const char *proto, mdns_txt_item_t txt[], uint8_t num_items)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_txt_set_for_host(NULL, service, proto, NULL, txt, num_items);
}

esp_err_t mdns_service_txt_item_set_for_host_with_explicit_value_len(const char *instance, const char *service, const char *proto,
                                                                     const char *host, const char *key, const char *value_arg, uint8_t value_len)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    char *value = NULL;
    mdns_txt_linked_item_t *new_txt = NULL;
    const char *hostname = host ? host : s_server->hostname;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto) && !mdns_utils_str_null_or_empty(key) &&
                      !((!value_arg && value_len)), ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");

    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    mdns_service_t *srv = s->service;
    if (value_len > 0) {
        value = (char *) mdns_mem_malloc(value_len);
        ESP_GOTO_ON_FALSE(value, ESP_ERR_NO_MEM, out_of_mem, TAG, "Out of memory");
        memcpy(value, value_arg, value_len);
    } else {
        value_len = 0;
    }
    mdns_txt_linked_item_t *txt = srv->txt;
    while (txt) {
        if (strcmp(txt->key, key) == 0) {
            mdns_mem_free((char *)txt->value);
            txt->value = value;
            txt->value_len = value_len;
            break;
        }
        txt = txt->next;
    }
    if (!txt) {
        new_txt = (mdns_txt_linked_item_t *)mdns_mem_malloc(sizeof(mdns_txt_linked_item_t));
        ESP_GOTO_ON_FALSE(new_txt, ESP_ERR_NO_MEM, out_of_mem, TAG, "Out of memory");
        new_txt->key = mdns_mem_strdup(key);
        ESP_GOTO_ON_FALSE(new_txt->key, ESP_ERR_NO_MEM, out_of_mem, TAG, "Out of memory");
        new_txt->value = value;
        new_txt->value_len = value_len;
        new_txt->next = srv->txt;
        srv->txt = new_txt;
    }

    announce_all_pcbs(&s, 1, false);

err:
    mdns_priv_service_unlock();
    return ret;
out_of_mem:
    mdns_priv_service_unlock();
    HOOK_MALLOC_FAILED;
    mdns_mem_free(value);
    mdns_mem_free(new_txt);
    return ret;
}

esp_err_t mdns_service_txt_item_set_for_host(const char *instance, const char *service, const char *proto, const char *hostname,
                                             const char *key, const char *value)
{
    return mdns_service_txt_item_set_for_host_with_explicit_value_len(instance, service, proto, hostname, key, value,
                                                                      strlen(value));
}


esp_err_t mdns_service_txt_item_set(const char *service, const char *proto, const char *key, const char *value)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_txt_item_set_for_host_with_explicit_value_len(NULL, service, proto, NULL, key,
                                                                      value, strlen(value));
}

esp_err_t mdns_service_txt_item_set_with_explicit_value_len(const char *service, const char *proto, const char *key,
                                                            const char *value, uint8_t value_len)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_txt_item_set_for_host_with_explicit_value_len(NULL, service, proto, NULL, key, value, value_len);
}

esp_err_t mdns_service_txt_item_remove_for_host(const char *instance, const char *service, const char *proto, const char *host,
                                                const char *key)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    const char *hostname = host ? host : s_server->hostname;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto) && !mdns_utils_str_null_or_empty(key),
                      ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");

    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    mdns_service_t *srv = s->service;
    mdns_txt_linked_item_t *txt = srv->txt;
    if (!txt) {
        goto err;
    }
    if (strcmp(txt->key, key) == 0) {
        srv->txt = txt->next;
        mdns_mem_free((char *)txt->key);
        mdns_mem_free((char *)txt->value);
        mdns_mem_free(txt);
    } else {
        while (txt->next) {
            if (strcmp(txt->next->key, key) == 0) {
                mdns_txt_linked_item_t *t = txt->next;
                txt->next = t->next;
                mdns_mem_free((char *)t->key);
                mdns_mem_free((char *)t->value);
                mdns_mem_free(t);
                break;
            } else {
                txt = txt->next;
            }
        }
    }

    announce_all_pcbs(&s, 1, false);

err:
    mdns_priv_service_unlock();
    if (ret == ESP_ERR_NO_MEM) {
        HOOK_MALLOC_FAILED;
    }
    return ret;
}

esp_err_t mdns_service_txt_item_remove(const char *service, const char *proto, const char *key)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_txt_item_remove_for_host(NULL, service, proto, NULL, key);
}

static esp_err_t service_subtype_remove_for_host(mdns_srv_item_t *service, const char *subtype)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    mdns_subtype_t *srv_subtype = service->service->subtype;
    mdns_subtype_t *pre = service->service->subtype;
    while (srv_subtype) {
        if (strcmp(srv_subtype->subtype, subtype) == 0) {
            // Target subtype is found.
            if (srv_subtype == service->service->subtype) {
                // The first node needs to be removed
                service->service->subtype = service->service->subtype->next;
            } else {
                pre->next = srv_subtype->next;
            }
            mdns_mem_free((char *)srv_subtype->subtype);
            mdns_mem_free(srv_subtype);
            ret = ESP_OK;
            break;
        }
        pre = srv_subtype;
        srv_subtype = srv_subtype->next;
    }
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "Subtype : %s doesn't exist", subtype);
    }

    return ret;
}

esp_err_t mdns_service_subtype_remove_for_host(const char *instance_name, const char *service, const char *proto,
                                               const char *hostname, const char *subtype)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto) &&
                      !mdns_utils_str_null_or_empty(subtype), ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");

    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance_name, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    ret = service_subtype_remove_for_host(s, subtype);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to remove the subtype: %s", subtype);

    // Transmit a sendbye message for the removed subtype.
    mdns_subtype_t *remove_subtypes = (mdns_subtype_t *)mdns_mem_malloc(sizeof(mdns_subtype_t));
    ESP_GOTO_ON_FALSE(remove_subtypes, ESP_ERR_NO_MEM, out_of_mem, TAG, "Out of memory");
    remove_subtypes->subtype = mdns_mem_strdup(subtype);
    ESP_GOTO_ON_FALSE(remove_subtypes->subtype, ESP_ERR_NO_MEM, out_of_mem, TAG, "Out of memory");
    remove_subtypes->next = NULL;

    mdns_priv_send_bye_subtype(s, instance_name, remove_subtypes);
    free_subtype(remove_subtypes);
err:
    mdns_priv_service_unlock();
    return ret;
out_of_mem:
    HOOK_MALLOC_FAILED;
    mdns_mem_free(remove_subtypes);
    mdns_priv_service_unlock();
    return ret;
}

static esp_err_t service_subtype_add_for_host(mdns_srv_item_t *service, const char *subtype)
{
    esp_err_t ret = ESP_OK;
    mdns_subtype_t *srv_subtype = service->service->subtype;
    while (srv_subtype) {
        ESP_GOTO_ON_FALSE(strcmp(srv_subtype->subtype, subtype) != 0, ESP_ERR_INVALID_ARG, err, TAG, "Subtype: %s has already been added", subtype);
        srv_subtype = srv_subtype->next;
    }

    mdns_subtype_t *subtype_item = (mdns_subtype_t *)mdns_mem_malloc(sizeof(mdns_subtype_t));
    ESP_GOTO_ON_FALSE(subtype_item, ESP_ERR_NO_MEM, out_of_mem, TAG, "Out of memory");
    subtype_item->subtype = mdns_mem_strdup(subtype);
    ESP_GOTO_ON_FALSE(subtype_item->subtype, ESP_ERR_NO_MEM, out_of_mem, TAG, "Out of memory");
    subtype_item->next = service->service->subtype;
    service->service->subtype = subtype_item;

err:
    return ret;
out_of_mem:
    HOOK_MALLOC_FAILED;
    mdns_mem_free(subtype_item);
    return ret;
}

esp_err_t mdns_service_subtype_add_multiple_items_for_host(const char *instance_name, const char *service, const char *proto,
                                                           const char *hostname, mdns_subtype_item_t subtype[], uint8_t num_items)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    int cur_index = 0;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto) &&
                      (num_items > 0), ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");

    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance_name, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    for (; cur_index < num_items; cur_index++) {
        ret = service_subtype_add_for_host(s, subtype[cur_index].subtype);
        if (ret == ESP_OK) {
            continue;
        } else if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGE(TAG, "Out of memory");
            goto err;
        } else {
            ESP_LOGE(TAG, "Failed to add subtype: %s", subtype[cur_index].subtype);
            goto exit;
        }
    }

    announce_all_pcbs(&s, 1, false);
err:
    if (ret == ESP_ERR_NO_MEM) {
        for (int idx = 0; idx < cur_index; idx++) {
            service_subtype_remove_for_host(s, subtype[idx].subtype);
        }
    }
exit:
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_service_subtype_add_for_host(const char *instance_name, const char *service_type, const char *proto,
                                            const char *hostname, const char *subtype)
{
    mdns_subtype_item_t _subtype[1];
    _subtype[0].subtype = subtype;
    return mdns_service_subtype_add_multiple_items_for_host(instance_name, service_type, proto, hostname, _subtype, 1);
}

static mdns_subtype_t *service_find_subtype_needed_sendbye(mdns_service_t *service, mdns_subtype_item_t subtype[],
                                                           uint8_t num_items)
{
    if (!service) {
        return NULL;
    }

    mdns_subtype_t *current = service->subtype;
    mdns_subtype_t *prev = NULL;
    mdns_subtype_t *prev_goodbye = NULL;
    mdns_subtype_t *out_goodbye_subtype = NULL;

    while (current) {
        bool subtype_in_update = false;

        for (int i = 0; i < num_items; i++) {
            if (strcmp(subtype[i].subtype, current->subtype) == 0) {
                subtype_in_update = true;
                break;
            }
        }

        if (!subtype_in_update) {
            // Remove from original list
            if (prev) {
                prev->next = current->next;
            } else {
                service->subtype = current->next;
            }

            mdns_subtype_t *to_move = current;
            current = current->next;

            // Add to goodbye list
            to_move->next = NULL;
            if (prev_goodbye) {
                prev_goodbye->next = to_move;
            } else {
                out_goodbye_subtype = to_move;
            }
            prev_goodbye = to_move;
        } else {
            prev = current;
            current = current->next;
        }
    }

    return out_goodbye_subtype;
}

esp_err_t mdns_service_subtype_update_multiple_items_for_host(const char *instance_name, const char *service_type, const char *proto,
                                                              const char *hostname, mdns_subtype_item_t subtype[], uint8_t num_items)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    int cur_index = 0;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service_type) && !mdns_utils_str_null_or_empty(proto),
                      ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");

    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance_name, service_type, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    mdns_subtype_t *goodbye_subtype = service_find_subtype_needed_sendbye(s->service, subtype, num_items);

    if (goodbye_subtype) {
        mdns_priv_send_bye_subtype(s, instance_name, goodbye_subtype);
    }

    free_subtype(goodbye_subtype);
    free_service_subtype(s->service);

    for (; cur_index < num_items; cur_index++) {
        ret = service_subtype_add_for_host(s, subtype[cur_index].subtype);
        if (ret == ESP_OK) {
            continue;
        } else if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGE(TAG, "Out of memory");
            goto err;
        } else {
            ESP_LOGE(TAG, "Failed to add subtype: %s", subtype[cur_index].subtype);
            goto exit;
        }
    }
    if (num_items) {
        announce_all_pcbs(&s, 1, false);
    }
err:
    if (ret == ESP_ERR_NO_MEM) {
        for (int idx = 0; idx < cur_index; idx++) {
            service_subtype_remove_for_host(s, subtype[idx].subtype);
        }
    }
exit:
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_service_instance_name_set_for_host(const char *instance_old, const char *service, const char *proto, const char *host,
                                                  const char *instance)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    const char *hostname = host ? host : s_server->hostname;

    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto) &&
                      !mdns_utils_str_null_or_empty(instance) && strlen(instance) <= (MDNS_NAME_BUF_LEN - 1), ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");

    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance_old, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    if (s->service->instance) {
        mdns_priv_pcb_send_bye_service(&s, 1, false);
        mdns_mem_free((char *)s->service->instance);
    }
    s->service->instance = mdns_mem_strndup(instance, MDNS_NAME_BUF_LEN - 1);
    ESP_GOTO_ON_FALSE(s->service->instance, ESP_ERR_NO_MEM, err, TAG, "Out of memory");
    mdns_priv_probe_all_pcbs(&s, 1, false, false);

err:
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_service_instance_name_set(const char *service, const char *proto, const char *instance)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_instance_name_set_for_host(NULL, service, proto, NULL, instance);
}

esp_err_t mdns_service_remove_for_host(const char *instance, const char *service, const char *proto, const char *host)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    const char *hostname = host ? host : s_server->hostname;
    ESP_GOTO_ON_FALSE(s_server && s_server->services && !mdns_utils_str_null_or_empty(service) && !mdns_utils_str_null_or_empty(proto),
                      ESP_ERR_INVALID_ARG, err, TAG, "Invalid state or arguments");
    mdns_srv_item_t *s = mdns_utils_get_service_item_instance(instance, service, proto, hostname);
    ESP_GOTO_ON_FALSE(s, ESP_ERR_NOT_FOUND, err, TAG, "Service doesn't exist");

    mdns_srv_item_t *a = s_server->services;
    mdns_srv_item_t *b = a;
    if (instance) {
        while (a) {
            if (mdns_utils_service_match_instance(a->service, instance, service, proto, hostname)) {
                if (s_server->services != a) {
                    b->next = a->next;
                } else {
                    s_server->services = a->next;
                }
                mdns_priv_pcb_send_bye_service(&a, 1, false);
                mdns_priv_remove_scheduled_service_packets(a->service);
                free_service(a->service);
                mdns_mem_free(a);
                break;
            }
            b = a;
            a = a->next;
        }
    } else {
        while (a) {
            if (mdns_utils_service_match(a->service, service, proto, hostname)) {
                if (s_server->services != a) {
                    b->next = a->next;
                } else {
                    s_server->services = a->next;
                }
                mdns_priv_pcb_send_bye_service(&a, 1, false);
                mdns_priv_remove_scheduled_service_packets(a->service);
                free_service(a->service);
                mdns_mem_free(a);
                break;
            }
            b = a;
            a = a->next;
        }
    }

err:
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_service_remove(const char *service_type, const char *proto)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    return mdns_service_remove_for_host(NULL, service_type, proto, NULL);
}

esp_err_t mdns_service_remove_all(void)
{
    mdns_priv_service_lock();
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(s_server, ESP_ERR_INVALID_ARG, done, TAG, "Invalid state");
    if (!s_server->services) {
        goto done;
    }

    send_final_bye(false);
    mdns_srv_item_t *services = s_server->services;
    s_server->services = NULL;
    while (services) {
        mdns_srv_item_t *s = services;
        services = services->next;
        mdns_priv_remove_scheduled_service_packets(s->service);
        free_service(s->service);
        mdns_mem_free(s);
    }

done:
    mdns_priv_service_unlock();
    return ret;
}

esp_err_t mdns_lookup_delegated_service(const char *instance, const char *service, const char *proto, size_t max_results,
                                        mdns_result_t **result)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!result || mdns_utils_str_null_or_empty(service) || mdns_utils_str_null_or_empty(proto)) {
        return ESP_ERR_INVALID_ARG;
    }
    mdns_priv_service_lock();
    *result = lookup_service(instance, service, proto, max_results, false);
    mdns_priv_service_unlock();
    return ESP_OK;
}

esp_err_t mdns_lookup_selfhosted_service(const char *instance, const char *service, const char *proto, size_t max_results,
                                         mdns_result_t **result)
{
    if (!s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!result || mdns_utils_str_null_or_empty(service) || mdns_utils_str_null_or_empty(proto)) {
        return ESP_ERR_INVALID_ARG;
    }
    mdns_priv_service_lock();
    *result = lookup_service(instance, service, proto, max_results, true);
    mdns_priv_service_unlock();
    return ESP_OK;
}
