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

#ifndef BLUETOOTH_SERVICE_H
#define BLUETOOTH_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

struct ble_gap_event;

#define BLE_SCAN_LIST_SIZE 50

/**
 * @brief BLE scan result entry.
 */
typedef struct {
  char name[32];
  char uuids[128];
  uint8_t addr[6];
  uint8_t addr_type;
  int rssi;
} bluetooth_service_scan_result_t;

/**
 * @brief Callback for BLE sniffer raw advertisement frames.
 *
 * @param addr       Advertiser address (6 bytes).
 * @param addr_type  Address type.
 * @param rssi       Received signal strength.
 * @param data       Raw advertisement data.
 * @param len        Length of advertisement data.
 */
typedef void (*bluetooth_service_sniffer_cb_t)(
    const uint8_t *addr, uint8_t addr_type, int rssi, const uint8_t *data, uint16_t len);

/**
 * @brief Callback for BLE RSSI tracker updates.
 *
 * @param rssi  Received signal strength of tracked device.
 */
typedef void (*bluetooth_service_tracker_cb_t)(int rssi);

/**
 * @brief Initialize the BLE subsystem.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_init(void);

/**
 * @brief Deinitialize the BLE subsystem and release resources.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_deinit(void);

/**
 * @brief Start the BLE host task.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_start(void);

/**
 * @brief Stop the BLE host task.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_stop(void);

/**
 * @brief Check if the BLE subsystem is initialized.
 *
 * @return true if initialized, false otherwise.
 */
bool bluetooth_service_is_initialized(void);

/**
 * @brief Check if the BLE host task is running.
 *
 * @return true if running, false otherwise.
 */
bool bluetooth_service_is_running(void);

/**
 * @brief Cheap, cached "BLE host running" state.
 *
 * Reads a flag refreshed by the background C5 status poll, so callers on the
 * LVGL thread (e.g. the header indicator) never block on an SPI round-trip.
 *
 * @return true if the last poll saw the BLE host running.
 */
bool bluetooth_service_is_running_cached(void);

/**
 * @brief Update the cached "BLE host running" state.
 *
 * Called by the C5 status poll; not intended for UI code.
 *
 * @param running  Latest running state from the C5 system status.
 */
void bluetooth_service_set_running_cached(bool running);

/**
 * @brief Terminate all active BLE connections.
 */
void bluetooth_service_disconnect_all(void);

/**
 * @brief Get the number of active BLE connections.
 *
 * @return Connection count.
 */
int bluetooth_service_get_connected_count(void);

/**
 * @brief Get the current BLE MAC address.
 *
 * @param[out] out_mac  Buffer for 6-byte MAC address.
 */
void bluetooth_service_get_mac(uint8_t *out_mac);

/**
 * @brief Set a random static BLE MAC address.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_set_random_mac(void);

/**
 * @brief Initiate a BLE connection to a remote device.
 *
 * @param addr       Target device address (6 bytes).
 * @param addr_type  Target address type.
 * @param cb         GAP event callback for the connection.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_connect(const uint8_t *addr,
                                    uint8_t addr_type,
                                    int (*cb)(struct ble_gap_event *event, void *arg));

/**
 * @brief Start passive BLE sniffer.
 *
 * @param cb  Callback invoked for each received advertisement.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_start_sniffer(bluetooth_service_sniffer_cb_t cb);

/**
 * @brief Stop the BLE sniffer.
 */
void bluetooth_service_stop_sniffer(void);

/**
 * @brief Start RSSI tracking for a specific BLE device.
 *
 * @param addr  Target device address (6 bytes).
 * @param cb    Callback invoked with RSSI updates.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_start_tracker(const uint8_t *addr, bluetooth_service_tracker_cb_t cb);

/**
 * @brief Stop the RSSI tracker.
 */
void bluetooth_service_stop_tracker(void);

/**
 * @brief Start BLE advertising.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_start_advertising(void);

/**
 * @brief Stop BLE advertising.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_stop_advertising(void);

/**
 * @brief Get the current own address type.
 *
 * @return BLE own address type value.
 */
uint8_t bluetooth_service_get_own_addr_type(void);

/**
 * @brief Set BLE TX power to maximum level.
 *
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_set_max_power(void);

/**
 * @brief Save BLE announce configuration to persistent storage.
 *
 * @param name      Device name. Must not be NULL.
 * @param max_conn  Maximum simultaneous connections.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_save_announce_config(const char *name, uint8_t max_conn);

/**
 * @brief Load the BLE spam name list from persistent storage.
 *
 * @param[out] out_list   Pointer to receive allocated string array.
 * @param[out] out_count  Number of entries loaded.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_load_spam_list(char ***out_list, size_t *out_count);

/**
 * @brief Save the BLE spam name list to persistent storage.
 *
 * @param list   Array of name strings.
 * @param count  Number of entries.
 * @return ESP_OK on success, or an error code.
 */
esp_err_t bluetooth_service_save_spam_list(const char *const *list, size_t count);

/**
 * @brief Free a spam list previously loaded with bluetooth_service_load_spam_list.
 *
 * @param list   String array to free.
 * @param count  Number of entries.
 */
void bluetooth_service_free_spam_list(char **list, size_t count);

/**
 * @brief Perform a BLE scan for the given duration.
 *
 * @param duration_ms  Scan duration in milliseconds.
 */
void bluetooth_service_scan(uint32_t duration_ms);

/**
 * @brief Get the number of devices found in the last scan.
 *
 * @return Device count.
 */
uint16_t bluetooth_service_get_scan_count(void);

/**
 * @brief Get a scan result by index.
 *
 * @param index  Index into the scan results.
 * @return Pointer to the result, or NULL if index is out of range.
 */
bluetooth_service_scan_result_t *bluetooth_service_get_scan_result(uint16_t index);

#ifdef __cplusplus
}
#endif

#endif // BLUETOOTH_SERVICE_H
