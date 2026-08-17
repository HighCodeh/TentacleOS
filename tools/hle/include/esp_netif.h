#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_netif_t;
typedef int esp_netif_config_t;

typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gw;
} esp_netif_ip_info_t;

#define IP2STR(ip) ((ip) & 0xFF), (((ip)>>8) & 0xFF), (((ip)>>16) & 0xFF), (((ip)>>24) & 0xFF)
#define IPSTR "%d.%d.%d.%d"
#define MAC2STR(m) (m)[0], (m)[1], (m)[2], (m)[3], (m)[4], (m)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
void esp_netif_destroy(esp_netif_t *netif);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip_info);
esp_err_t esp_netif_set_ip_info(esp_netif_t *netif, const esp_netif_ip_info_t *ip_info);

#ifdef __cplusplus
}
#endif
