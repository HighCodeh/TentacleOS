/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mdns.h"
#include "mdns_private.h"
#include "mdns_mem_caps.h"
#include "mdns_utils.h"
#include "mdns_debug.h"
#include "mdns_browser.h"
#include "mdns_netif.h"
#include "mdns_pcb.h"
#include "mdns_responder.h"
#include "mdns_service.h"

static const char *TAG = "mdns_netif";

#if CONFIG_ETH_ENABLED && CONFIG_MDNS_PREDEF_NETIF_ETH
#include "esp_eth.h"
#endif

#if ESP_IDF_VERSION <= ESP_IDF_VERSION_VAL(5, 1, 0)
#define MDNS_ESP_WIFI_ENABLED CONFIG_SOC_WIFI_SUPPORTED
#else
#define MDNS_ESP_WIFI_ENABLED (CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_WIFI_REMOTE_ENABLED)
#endif

#if MDNS_ESP_WIFI_ENABLED && (CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP)
#include "esp_wifi.h"
#endif

typedef enum {
    MDNS_IF_STA = 0,
    MDNS_IF_AP = 1,
    MDNS_IF_ETH = 2,
} mdns_predef_if_t;

typedef struct mdns_interfaces mdns_interfaces_t;

struct mdns_interfaces {
    const bool predefined;
    esp_netif_t *netif;
    const mdns_predef_if_t predef_if;
    mdns_if_t duplicate;
};

/*
 * @brief  Internal collection of mdns supported interfaces
 *
 */
static mdns_interfaces_t s_esp_netifs[MDNS_MAX_INTERFACES] = {
#if CONFIG_MDNS_PREDEF_NETIF_STA
    { .predefined = true, .netif = NULL, .predef_if = MDNS_IF_STA, .duplicate = MDNS_MAX_INTERFACES },
#endif
#if CONFIG_MDNS_PREDEF_NETIF_AP
    { .predefined = true, .netif = NULL, .predef_if = MDNS_IF_AP,  .duplicate = MDNS_MAX_INTERFACES },
#endif
#if CONFIG_MDNS_PREDEF_NETIF_ETH
    { .predefined = true, .netif = NULL, .predef_if = MDNS_IF_ETH, .duplicate = MDNS_MAX_INTERFACES },
#endif
};

/**
 * @brief  Helper to get either ETH or STA if the other is provided
 *          Used when two interfaces are on the same subnet
 */
mdns_if_t mdns_priv_netif_get_other_interface(mdns_if_t tcpip_if)
{
    if (tcpip_if < MDNS_MAX_INTERFACES) {
        return s_esp_netifs[tcpip_if].duplicate;
    }
    return MDNS_MAX_INTERFACES;
}

/**
 * @brief  Convert Predefined interface to the netif id from the internal netif list
 * @param  predef_if Predefined interface enum
 * @return Ordinal number of internal list of mdns network interface.
 *         Returns MDNS_MAX_INTERFACES if the predefined interface wasn't found in the list
 */
static mdns_if_t mdns_if_from_preset(mdns_predef_if_t predef_if)
{
    for (int i = 0; i < MDNS_MAX_INTERFACES; ++i) {
        if (s_esp_netifs[i].predefined && s_esp_netifs[i].predef_if == predef_if) {
            return i;
        }
    }
    return MDNS_MAX_INTERFACES;
}

/**
 * @brief  Convert Predefined interface to esp-netif handle
 * @param  predef_if Predefined interface enum
 * @return esp_netif pointer from system list of network interfaces
 */
static inline esp_netif_t *netif_from_preset(mdns_predef_if_t predef_if)
{
    switch (predef_if) {
    case MDNS_IF_STA:
        return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    case MDNS_IF_AP:
        return esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
#if CONFIG_ETH_ENABLED && CONFIG_MDNS_PREDEF_NETIF_ETH
    case MDNS_IF_ETH:
        return esp_netif_get_handle_from_ifkey("ETH_DEF");
#endif
    default:
        return NULL;
    }
}

esp_netif_t *mdns_priv_get_esp_netif(mdns_if_t tcpip_if)
{
    if (tcpip_if < MDNS_MAX_INTERFACES) {
        if (s_esp_netifs[tcpip_if].netif == NULL && s_esp_netifs[tcpip_if].predefined) {
            // If the local copy is NULL and this netif is predefined -> we can find it in the global netif list
            s_esp_netifs[tcpip_if].netif = netif_from_preset(s_esp_netifs[tcpip_if].predef_if);
            // failing to find it means that the netif is *not* available -> return NULL
        }
        return s_esp_netifs[tcpip_if].netif;
    }
    return NULL;
}

/*
 * @brief Clean internal mdns interface's pointer
 */
void mdns_priv_netif_disable(mdns_if_t tcpip_if)
{
    if (tcpip_if < MDNS_MAX_INTERFACES) {
        s_esp_netifs[tcpip_if].netif = NULL;
    }
}

/*
 * @brief  Convert esp-netif handle to mdns if
 */
static mdns_if_t get_if_from_netif(esp_netif_t *esp_netif)
{
    for (int i = 0; i < MDNS_MAX_INTERFACES; ++i) {
        // The predefined netifs in the static array are NULL when firstly calling this function
        // if IPv4 is disabled. Set these netifs here.
        if (s_esp_netifs[i].netif == NULL && s_esp_netifs[i].predefined) {
            s_esp_netifs[i].netif = netif_from_preset(s_esp_netifs[i].predef_if);
        }
        if (esp_netif == s_esp_netifs[i].netif) {
            return i;
        }
    }
    return MDNS_MAX_INTERFACES;
}

static esp_err_t post_custom_action(mdns_if_t mdns_if, mdns_event_actions_t event_action)
{
    if (!mdns_priv_is_server_init() || mdns_if >= MDNS_MAX_INTERFACES) {
        return ESP_ERR_INVALID_STATE;
    }

    mdns_action_t *action = (mdns_action_t *)mdns_mem_calloc(1, sizeof(mdns_action_t));
    if (!action) {
        HOOK_MALLOC_FAILED;
        return ESP_ERR_NO_MEM;
    }
    action->type = ACTION_SYSTEM_EVENT;
    action->data.sys_event.event_action = event_action;
    action->data.sys_event.interface = mdns_if;

    if (!mdns_priv_queue_action(action)) {
        mdns_mem_free(action);
    }
    return ESP_OK;
}

/**
 * @brief  Dispatch interface changes based on system events
 */
static inline void post_disable_pcb(mdns_predef_if_t preset_if, mdns_ip_protocol_t protocol)
{
    post_custom_action(mdns_if_from_preset(preset_if),
                       protocol == MDNS_IP_PROTOCOL_V4 ? MDNS_EVENT_DISABLE_IP4 : MDNS_EVENT_DISABLE_IP6);
}

static inline void post_enable_pcb(mdns_predef_if_t preset_if, mdns_ip_protocol_t protocol)
{
    post_custom_action(mdns_if_from_preset(preset_if),
                       protocol == MDNS_IP_PROTOCOL_V4 ? MDNS_EVENT_ENABLE_IP4 : MDNS_EVENT_ENABLE_IP6);
}

static inline void post_announce_pcb(mdns_predef_if_t preset_if, mdns_ip_protocol_t protocol)
{
    post_custom_action(mdns_if_from_preset(preset_if),
                       protocol == MDNS_IP_PROTOCOL_V4 ? MDNS_EVENT_ANNOUNCE_IP4 : MDNS_EVENT_ANNOUNCE_IP6);
}

#if CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP || CONFIG_MDNS_PREDEF_NETIF_ETH
static void handle_system_event_for_preset(void *arg, esp_event_base_t event_base,
                                           int32_t event_id, void *event_data)
{
    if (!mdns_priv_is_server_init()) {
        return;
    }

#if MDNS_ESP_WIFI_ENABLED && (CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP)
    if (event_base == WIFI_EVENT) {
        esp_netif_dhcp_status_t dcst;
        switch (event_id) {
        case WIFI_EVENT_STA_CONNECTED:
            if (!esp_netif_dhcpc_get_status(netif_from_preset(MDNS_IF_STA), &dcst)) {
                if (dcst == ESP_NETIF_DHCP_STOPPED) {
                    post_enable_pcb(MDNS_IF_STA, MDNS_IP_PROTOCOL_V4);
                }
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            post_disable_pcb(MDNS_IF_STA, MDNS_IP_PROTOCOL_V4);
            post_disable_pcb(MDNS_IF_STA, MDNS_IP_PROTOCOL_V6);
            break;
        case WIFI_EVENT_AP_START:
            post_enable_pcb(MDNS_IF_AP, MDNS_IP_PROTOCOL_V4);
            break;
        case WIFI_EVENT_AP_STOP:
            post_disable_pcb(MDNS_IF_AP, MDNS_IP_PROTOCOL_V4);
            post_disable_pcb(MDNS_IF_AP, MDNS_IP_PROTOCOL_V6);
            break;
        default:
            break;
        }
    } else
#endif
#if CONFIG_ETH_ENABLED && CONFIG_MDNS_PREDEF_NETIF_ETH
        if (event_base == ETH_EVENT) {
            esp_netif_dhcp_status_t dcst;
            switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                if (!esp_netif_dhcpc_get_status(netif_from_preset(MDNS_IF_ETH), &dcst)) {
                    if (dcst == ESP_NETIF_DHCP_STOPPED) {
                        post_enable_pcb(MDNS_IF_ETH, MDNS_IP_PROTOCOL_V4);
                    }
                }
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                post_disable_pcb(MDNS_IF_ETH, MDNS_IP_PROTOCOL_V4);
                post_disable_pcb(MDNS_IF_ETH, MDNS_IP_PROTOCOL_V6);
                break;
            default:
                break;
            }
        } else
#endif
            if (event_base == IP_EVENT) {
                switch (event_id) {
                case IP_EVENT_STA_GOT_IP:
                    post_enable_pcb(MDNS_IF_STA, MDNS_IP_PROTOCOL_V4);
                    post_announce_pcb(MDNS_IF_STA, MDNS_IP_PROTOCOL_V6);
                    break;
#if CONFIG_ETH_ENABLED && CONFIG_MDNS_PREDEF_NETIF_ETH
                case IP_EVENT_ETH_GOT_IP:
                    post_enable_pcb(MDNS_IF_ETH, MDNS_IP_PROTOCOL_V4);
                    break;
#endif
                case IP_EVENT_GOT_IP6: {
                    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *) event_data;
                    mdns_if_t mdns_if = get_if_from_netif(event->esp_netif);
                    if (mdns_if >= MDNS_MAX_INTERFACES) {
                        return;
                    }
                    post_enable_pcb(mdns_if, MDNS_IP_PROTOCOL_V6);
                    post_announce_pcb(mdns_if, MDNS_IP_PROTOCOL_V4);
                    mdns_priv_browse_send_all(mdns_if);

                }
                break;
                default:
                    break;
                }
            }
}
#endif /* CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP || CONFIG_MDNS_PREDEF_NETIF_ETH */

static inline void set_default_duplicated_interfaces(void)
{
    mdns_if_t wifi_sta_if = MDNS_MAX_INTERFACES;
    mdns_if_t eth_if = MDNS_MAX_INTERFACES;
    for (mdns_if_t i = 0; i < MDNS_MAX_INTERFACES; i++) {
        if (s_esp_netifs[i].predefined && s_esp_netifs[i].predef_if == MDNS_IF_STA) {
            wifi_sta_if = i;
        }
        if (s_esp_netifs[i].predefined && s_esp_netifs[i].predef_if == MDNS_IF_ETH) {
            eth_if = i;
        }
    }
    if (wifi_sta_if != MDNS_MAX_INTERFACES && eth_if != MDNS_MAX_INTERFACES) {
        s_esp_netifs[wifi_sta_if].duplicate = eth_if;
        s_esp_netifs[eth_if].duplicate = wifi_sta_if;
    }
}

void mdns_priv_netif_unregister_predefined_handlers(void)
{
#if MDNS_ESP_WIFI_ENABLED && (CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP)
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, handle_system_event_for_preset);
#endif
#if CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP || CONFIG_MDNS_PREDEF_NETIF_ETH
    esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, handle_system_event_for_preset);
#endif
#if CONFIG_ETH_ENABLED && CONFIG_MDNS_PREDEF_NETIF_ETH
    esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, handle_system_event_for_preset);
#endif
}

esp_err_t mdns_priv_netif_init(void)
{
    esp_err_t err = ESP_OK;
    // zero-out local copy of netifs to initiate a fresh search by interface key whenever a netif ptr is needed
    for (mdns_if_t i = 0; i < MDNS_MAX_INTERFACES; ++i) {
        s_esp_netifs[i].netif = NULL;
    }
#if MDNS_ESP_WIFI_ENABLED && (CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP)
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handle_system_event_for_preset, NULL)) != ESP_OK) {
        goto free_event_handlers;
    }
#endif
#if CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP || CONFIG_MDNS_PREDEF_NETIF_ETH
    if ((err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, handle_system_event_for_preset, NULL)) != ESP_OK) {
        goto free_event_handlers;
    }
#endif
#if CONFIG_ETH_ENABLED && CONFIG_MDNS_PREDEF_NETIF_ETH
    if ((err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, handle_system_event_for_preset, NULL)) != ESP_OK) {
        goto free_event_handlers;
    }
#endif

#if CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP || CONFIG_MDNS_PREDEF_NETIF_ETH
    set_default_duplicated_interfaces();
#endif

    uint8_t i;
#ifdef CONFIG_LWIP_IPV6
    esp_ip6_addr_t tmp_addr6;
#endif
#ifdef CONFIG_LWIP_IPV4
    esp_netif_ip_info_t if_ip_info;
#endif

    for (i = 0; i < MDNS_MAX_INTERFACES; i++) {
#ifdef CONFIG_LWIP_IPV6
        if (!esp_netif_get_ip6_linklocal(mdns_priv_get_esp_netif(i), &tmp_addr6) && !mdns_utils_ipv6_address_is_zero(tmp_addr6)) {
            mdns_priv_pcb_enable(i, MDNS_IP_PROTOCOL_V6);
        }
#endif
#ifdef CONFIG_LWIP_IPV4
        if (!esp_netif_get_ip_info(mdns_priv_get_esp_netif(i), &if_ip_info) && if_ip_info.ip.addr) {
            mdns_priv_pcb_enable(i, MDNS_IP_PROTOCOL_V4);
        }
#endif
    }
    return ESP_OK;
#if CONFIG_MDNS_PREDEF_NETIF_STA || CONFIG_MDNS_PREDEF_NETIF_AP || CONFIG_MDNS_PREDEF_NETIF_ETH
free_event_handlers:
    mdns_priv_netif_unregister_predefined_handlers();
#endif
    return err;
}

esp_err_t mdns_priv_netif_deinit(void)
{
    for (int i = 0; i < MDNS_MAX_INTERFACES; i++) {
        mdns_priv_pcb_disable(i, MDNS_IP_PROTOCOL_V6);
        mdns_priv_pcb_disable(i, MDNS_IP_PROTOCOL_V4);
        s_esp_netifs[i].duplicate = MDNS_MAX_INTERFACES;
    }
    return ESP_OK;
}

/*
 * Public Methods
 * */
esp_err_t mdns_netif_action(esp_netif_t *esp_netif, mdns_event_actions_t event_action)
{
    return post_custom_action(get_if_from_netif(esp_netif), event_action);
}

esp_err_t mdns_register_netif(esp_netif_t *esp_netif)
{
    if (!mdns_priv_is_server_init()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NO_MEM;
    mdns_priv_service_lock();
    for (mdns_if_t i = 0; i < MDNS_MAX_INTERFACES; ++i) {
        if (s_esp_netifs[i].netif == esp_netif) {
            mdns_priv_service_unlock();
            return ESP_ERR_INVALID_STATE;
        }
    }

    for (mdns_if_t i = 0; i < MDNS_MAX_INTERFACES; ++i) {
        if (!s_esp_netifs[i].predefined && s_esp_netifs[i].netif == NULL) {
            s_esp_netifs[i].netif = esp_netif;
            err = ESP_OK;
            break;
        }
    }
    mdns_priv_service_unlock();
    return err;
}

esp_err_t mdns_unregister_netif(esp_netif_t *esp_netif)
{
    if (!mdns_priv_is_server_init()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    mdns_priv_service_lock();
    for (mdns_if_t i = 0; i < MDNS_MAX_INTERFACES; ++i) {
        if (!s_esp_netifs[i].predefined && s_esp_netifs[i].netif == esp_netif) {
            s_esp_netifs[i].netif = NULL;
            err = ESP_OK;
            break;
        }
    }
    mdns_priv_service_lock();
    return err;
}
