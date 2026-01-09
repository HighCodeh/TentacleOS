// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "esp_err.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#define WIFI_SCAN_LIST_SIZE 20

// initialization
void wifi_init(void);
void wifi_deinit(void);
void wifi_stop(void);
void wifi_start(void);

// management functions
void wifi_change_to_hotspot(const char *new_ssid);
esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password);

// functions to scan and storage
void wifi_service_scan(void);
uint16_t wifi_service_get_ap_count(void);
wifi_ap_record_t* wifi_service_get_ap_record(uint16_t index);

// functions to load and save configs 
esp_err_t wifi_save_ap_config(const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr);

// promiscuous mode management
void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter);
void wifi_service_promiscuous_stop(void);

// channel hopping management
void wifi_service_start_channel_hopping(void);
void wifi_service_stop_channel_hopping(void);

#endif // WIFI_SERVICE_H
