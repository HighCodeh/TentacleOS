#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

esp_err_t esp_now_init(void);
esp_err_t esp_now_deinit(void);
esp_err_t esp_now_register_send_cb(void *cb);
esp_err_t esp_now_register_recv_cb(void *cb);
esp_err_t esp_now_send(const uint8_t *peer, const uint8_t *data, size_t len);

typedef struct { uint8_t mac[6]; } esp_now_peer_info_t;
typedef void (*service_esp_now_send_cb_t)(const uint8_t *mac, int status);
typedef void (*service_esp_now_recv_cb_t)(const uint8_t *mac, const uint8_t *data, int len, int rssi);

#define ESP_IF_WIFI_STA 0
#define ESP_NOW_ETH_ALEN 6

#ifdef __cplusplus
}
#endif
