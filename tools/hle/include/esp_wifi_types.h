#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WEP,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_AUTH_WAPI_PSK,
    WIFI_AUTH_MAX,
} wifi_auth_mode_t;

typedef enum {
    WIFI_CIPHER_TYPE_NONE = 0,
    WIFI_CIPHER_TYPE_WEP40,
    WIFI_CIPHER_TYPE_WEP104,
    WIFI_CIPHER_TYPE_TKIP,
    WIFI_CIPHER_TYPE_CCMP,
    WIFI_CIPHER_TYPE_TKIP_CCMP,
    WIFI_CIPHER_TYPE_AES_CMAC128,
    WIFI_CIPHER_TYPE_SMS4,
    WIFI_CIPHER_TYPE_GCMP,
    WIFI_CIPHER_TYPE_GCMP256,
} wifi_cipher_type_t;

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    int8_t rssi;
    wifi_auth_mode_t authmode;
    wifi_cipher_type_t pairwise_cipher;
    wifi_cipher_type_t group_cipher;
    uint16_t count;
} wifi_ap_record_t;

typedef enum {
    WIFI_FILTER_BEACON = 0,
    WIFI_FILTER_PROBE_REQ,
    WIFI_FILTER_MGMT_MASK,
    WIFI_FILTER_DATA_MASK,
    WIFI_FILTER_CTRL_MASK,
} wifi_promiscuous_filter_type_t;

typedef enum {
    WIFI_PKT_MGMT = 0,
    WIFI_PKT_CTRL,
    WIFI_PKT_DATA,
    WIFI_PKT_MISC,
} wifi_promiscuous_pkt_type_t;

typedef struct {
    uint32_t filter_mask;
} wifi_promiscuous_filter_t;

typedef void (*wifi_promiscuous_cb_t)(void *buf, wifi_promiscuous_pkt_type_t type);

typedef struct {
    int dummy;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_PROMISC_FILTER_MASK_ALL 0xFFFFFFFF
#define WIFI_PROMIS_FILTER_MASK_ALL  0xFFFFFFFF

typedef struct { int dummy; } wifi_tx_info_t;

#ifdef __cplusplus
}
#endif
