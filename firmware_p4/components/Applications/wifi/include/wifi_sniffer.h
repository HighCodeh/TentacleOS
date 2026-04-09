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

#ifndef WIFI_SNIFFER_H
#define WIFI_SNIFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Sniffer capture filter type.
 */
typedef enum {
  WIFI_SNIFFER_TYPE_BEACON = 0,
  WIFI_SNIFFER_TYPE_PROBE,
  WIFI_SNIFFER_TYPE_EAPOL,
  WIFI_SNIFFER_TYPE_PMKID,
  WIFI_SNIFFER_TYPE_RAW,
  WIFI_SNIFFER_TYPE_COUNT
} wifi_sniffer_type_t;

/**
 * @brief Callback invoked for each sniffed frame in stream mode.
 *
 * @param frame    Pointer to raw frame data.
 * @param len      Length of frame data.
 * @param rssi     Signal strength in dBm.
 * @param channel  Channel the frame was captured on.
 */
typedef void (*wifi_sniffer_cb_t)(const uint8_t *frame, uint16_t len, int8_t rssi, uint8_t channel);

/**
 * @brief Start the sniffer with buffered capture.
 *
 * @param type     Frame filter type.
 * @param channel  Channel to sniff (0 for hopping).
 * @return true on success, false on failure.
 */
bool wifi_sniffer_start(wifi_sniffer_type_t type, uint8_t channel);

/**
 * @brief Start the sniffer in streaming mode with a callback.
 *
 * @param type     Frame filter type.
 * @param channel  Channel to sniff (0 for hopping).
 * @param cb       Callback for each frame, or NULL for buffer-only mode.
 * @return true on success, false on failure.
 */
bool wifi_sniffer_start_stream(wifi_sniffer_type_t type, uint8_t channel, wifi_sniffer_cb_t cb);

/**
 * @brief Stop the sniffer and release stream resources.
 */
void wifi_sniffer_stop(void);

/**
 * @brief Start writing captured frames to a file.
 *
 * @param path  File path for the capture output.
 * @return true on success, false on failure.
 */
bool wifi_sniffer_start_capture(const char *path);

/**
 * @brief Stop writing captured frames to file.
 *
 * @return true if a capture was active and stopped, false otherwise.
 */
bool wifi_sniffer_stop_capture(void);

/**
 * @brief Get the size of the current capture file in bytes.
 *
 * @return Bytes written to the capture file.
 */
size_t wifi_sniffer_get_capture_size(void);

/**
 * @brief Free the C5 sniffer packet buffer over SPI.
 */
void wifi_sniffer_free_buffer(void);

/**
 * @brief Get the total number of packets captured.
 *
 * @return Packet count.
 */
uint32_t wifi_sniffer_get_packet_count(void);

/**
 * @brief Get the number of deauth frames observed.
 *
 * @return Deauth frame count.
 */
uint32_t wifi_sniffer_get_deauth_count(void);

/**
 * @brief Get the current C5 buffer usage percentage.
 *
 * @return Buffer usage value.
 */
uint32_t wifi_sniffer_get_buffer_usage(void);

/**
 * @brief Set the snap length for packet capture.
 *
 * @param len  Maximum bytes to capture per frame.
 */
void wifi_sniffer_set_snaplen(uint16_t len);

/**
 * @brief Enable or disable verbose output.
 *
 * @param is_verbose  true to enable, false to disable.
 */
void wifi_sniffer_set_verbose(bool is_verbose);

/**
 * @brief Check if a PMKID has been captured.
 *
 * @return true if captured, false otherwise.
 */
bool wifi_sniffer_pmkid_captured(void);

/**
 * @brief Clear the captured PMKID state.
 */
void wifi_sniffer_clear_pmkid(void);

/**
 * @brief Get the BSSID associated with the captured PMKID.
 *
 * @param[out] out_bssid  6-byte buffer for the BSSID.
 */
void wifi_sniffer_get_pmkid_bssid(uint8_t out_bssid[6]);

/**
 * @brief Check if a WPA handshake has been captured.
 *
 * @return true if captured, false otherwise.
 */
bool wifi_sniffer_handshake_captured(void);

/**
 * @brief Clear the captured handshake state.
 */
void wifi_sniffer_clear_handshake(void);

/**
 * @brief Get the BSSID associated with the captured handshake.
 *
 * @param[out] out_bssid  6-byte buffer for the BSSID.
 */
void wifi_sniffer_get_handshake_bssid(uint8_t out_bssid[6]);

#ifdef __cplusplus
}
#endif

#endif // WIFI_SNIFFER_H
