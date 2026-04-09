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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi.h"

#define WIFI_SCAN_LIST_SIZE      20
#define WIFI_AP_CONFIG_FILE      "config/wifi/wifi_ap.conf"
#define WIFI_KNOWN_NETWORKS_FILE "storage/wifi/know_networks.json"

/**
 * @brief Initialize the Wi-Fi subsystem.
 */
void wifi_service_init(void);

/**
 * @brief Deinitialize the Wi-Fi subsystem and release resources.
 */
void wifi_service_deinit(void);

/**
 * @brief Stop the Wi-Fi radio.
 */
void wifi_service_stop(void);

/**
 * @brief Start the Wi-Fi radio.
 */
void wifi_service_start(void);

/**
 * @brief Switch Wi-Fi to hotspot mode with the given SSID.
 *
 * @param new_ssid  SSID for the new access point.
 */
void wifi_service_change_to_hotspot(const char *new_ssid);

/**
 * @brief Connect to an access point.
 *
 * @param ssid      Target network SSID.
 * @param password  Network password, or NULL for open networks.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if ssid is NULL
 */
esp_err_t wifi_service_connect_to_ap(const char *ssid, const char *password);

/**
 * @brief Check if the device is connected to an access point.
 *
 * @return true if connected, false otherwise.
 */
bool wifi_service_is_connected(void);

/**
 * @brief Check if the Wi-Fi subsystem is active.
 *
 * @return true if active, false otherwise.
 */
bool wifi_service_is_active(void);

/**
 * @brief Get the SSID of the currently connected access point.
 *
 * @return Pointer to the SSID string, or NULL if not connected.
 */
const char *wifi_service_get_connected_ssid(void);

/**
 * @brief Scan for nearby access points.
 */
void wifi_service_scan(void);

/**
 * @brief Get the number of access points found in the last scan.
 *
 * @return Number of access points.
 */
uint16_t wifi_service_get_ap_count(void);

/**
 * @brief Get the record for an access point by index.
 *
 * @param index  Index into the scan results.
 * @return Pointer to the AP record, or NULL if index is out of range.
 */
wifi_ap_record_t *wifi_service_get_ap_record(uint16_t index);

/**
 * @brief Save the full AP configuration to persistent storage.
 *
 * @param ssid      AP SSID. Must not be NULL.
 * @param password  AP password, or NULL for open network.
 * @param max_conn  Maximum number of connected stations.
 * @param ip_addr   Static IP address string, or NULL for default.
 * @param enabled   Whether the AP should be enabled.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if ssid is NULL
 *   - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t wifi_service_save_ap_config(
    const char *ssid, const char *password, uint8_t max_conn, const char *ip_addr, bool enabled);

/**
 * @brief Enable or disable the Wi-Fi AP.
 *
 * @param enabled  true to enable, false to disable.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t wifi_service_set_enabled(bool enabled);

/**
 * @brief Set the AP SSID.
 *
 * @param ssid  New SSID string.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t wifi_service_set_ap_ssid(const char *ssid);

/**
 * @brief Set the AP password.
 *
 * @param password  New password string.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t wifi_service_set_ap_password(const char *password);

/**
 * @brief Set the AP maximum connection count.
 *
 * @param max_conn  Maximum number of simultaneous connections.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t wifi_service_set_ap_max_conn(uint8_t max_conn);

/**
 * @brief Set the AP static IP address.
 *
 * @param ip_addr  IP address string.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t wifi_service_set_ap_ip(const char *ip_addr);

/**
 * @brief Enable promiscuous (monitor) mode.
 *
 * @param cb      Callback for received frames.
 * @param filter  Promiscuous filter, or NULL for no filter.
 */
void wifi_service_promiscuous_start(wifi_promiscuous_cb_t cb, wifi_promiscuous_filter_t *filter);

/**
 * @brief Disable promiscuous (monitor) mode.
 */
void wifi_service_promiscuous_stop(void);

/**
 * @brief Start channel hopping across all Wi-Fi channels.
 */
void wifi_service_start_channel_hopping(void);

/**
 * @brief Stop channel hopping.
 */
void wifi_service_stop_channel_hopping(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_SERVICE_H
