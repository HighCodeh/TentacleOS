#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_mac_type_t;
#define ESP_MAC_WIFI_STA 0
#define ESP_MAC_WIFI_SOFTAP 1
#define ESP_MAC_BT 2
#define ESP_MAC_ETH 3

esp_err_t esp_read_mac(uint8_t *mac, int type);
esp_err_t esp_efuse_mac_get_default(uint8_t *mac);
esp_err_t esp_base_mac_addr_get(uint8_t *mac);

#ifdef __cplusplus
}
#endif
