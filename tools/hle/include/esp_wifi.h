#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int wifi_interface_t;
#define WIFI_IF_STA 0
#define WIFI_IF_AP  1

int esp_wifi_init(const void *cfg);
int esp_wifi_start(void);
int esp_wifi_stop(void);
int esp_wifi_disconnect(void);
int esp_wifi_scan_start(const void *cfg, bool block);
int esp_wifi_scan_stop(void);
int esp_wifi_scan_get_ap_num(uint16_t *num);
int esp_wifi_scan_get_ap_records(uint16_t *num, wifi_ap_record_t *rec);
int esp_wifi_set_promiscuous(bool enable);
int esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter);
int esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t cb);
int esp_wifi_set_channel(uint8_t primary, int secondary);
int esp_wifi_get_channel(uint8_t *primary, int *secondary);

#define WIFI_PROTOCOL_11B   1
#define WIFI_PROTOCOL_11G   2

#ifdef __cplusplus
}
#endif
