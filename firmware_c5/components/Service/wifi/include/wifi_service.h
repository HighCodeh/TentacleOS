// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi.h"

/** Capture in WIFI_MODE_NULL; caller must stop normal Wi-Fi first.
 * Runs on the calling worker and holds the scan mutex until cleanup.
 * tick_cb runs once per dwell and returns false to cancel; no payload is retained.
 */
typedef bool (*wifi_service_capture_tick_cb_t)(void *ctx);
typedef struct {
  uint32_t duration_ms;
  uint8_t band;
  wifi_service_capture_tick_cb_t tick_cb;
  void *ctx;
} wifi_service_capture_config_t;
esp_err_t wifi_service_capture_passive(const wifi_service_capture_config_t *config,
                                       wifi_promiscuous_cb_t rx_cb);

#define WIFI_SCAN_LIST_SIZE      64
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

/** @brief True while a capture (promiscuous sniffer) is running. */
bool wifi_service_is_busy(void);

/** @brief Set WiFi modem sleep depth: deep = MAX_MODEM, otherwise MIN_MODEM. */
void wifi_service_set_power_save(bool deep);

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
esp_err_t wifi_service_scan_step(uint8_t slot);

/** Run a scan constrained to one band, or both bands when band is 0. */
void wifi_service_scan_ex(uint8_t band, bool passive, uint16_t dwell_min_ms, uint16_t dwell_max_ms);

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
typedef enum {
  WIFI_SERVICE_CAPTURE_AP,
  WIFI_SERVICE_CAPTURE_ASSOC,
  WIFI_SERVICE_CAPTURE_EAPOL,
  WIFI_SERVICE_CAPTURE_ACTIVITY_COUNT
} wifi_service_capture_activity_t;

/** Configure while stopped; returns ESP_ERR_NO_MEM if PSRAM allocation fails. */
/* Stop clears capture policy but retains bounded PSRAM storage for reuse. */
esp_err_t wifi_service_set_capture_hopping(bool enabled);
/** Callback-safe observation: updates timing only, never switches channels or performs I/O. */
/* BSSID is borrowed only for this call; history is bounded and reset on stop. */
void wifi_service_observe_capture(uint8_t channel,
                                  wifi_service_capture_activity_t activity,
                                  const uint8_t *bssid);

#define WIFI_SERVICE_CAPTURE_MAC_LEN 6

typedef esp_err_t (*wifi_service_capture_action_cb_t)(const uint8_t *bssid, uint8_t channel);
typedef struct {
  uint16_t attempts;
  uint16_t failures;
  uint8_t hold_seconds;
  uint8_t ap_attempt;
} wifi_service_capture_status_t;

/** Configure only while the hopper is stopped; action runs on the hopper task. */
void wifi_service_set_capture_action(wifi_service_capture_action_cb_t action);
/** Borrowed beacon/probe-response IEs; validates the entire list before scheduling. */
void wifi_service_observe_capture_ap(uint8_t channel,
                                     const uint8_t *bssid,
                                     const uint8_t *ies,
                                     size_t length);
/** Mark C5 EAPOL-pair or PMKID observation per BSSID; this does not acknowledge SD persistence. */
void wifi_service_mark_captured(const uint8_t *bssid);
void wifi_service_get_capture_status(wifi_service_capture_status_t *out_status);

void wifi_service_start_channel_hopping(void);

/** Select which band the next channel-hopping session will visit (0/1/2). */
esp_err_t wifi_service_set_analysis_band(uint8_t band);

/**
 * @brief Stop channel hopping.
 */
void wifi_service_stop_channel_hopping(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_SERVICE_H
