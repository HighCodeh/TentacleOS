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

typedef enum {
  WIFI_ANALYSIS_BAND_DUAL = 0,
  WIFI_ANALYSIS_BAND_2G = 1,
  WIFI_ANALYSIS_BAND_5G = 2,
} wifi_analysis_band_t;

#define WIFI_SPECTRUM_BIN_COUNT 39

typedef struct {
  uint32_t packets;
  uint32_t deauths;
  uint32_t buffer_usage;
  uint32_t beacons;
  uint32_t probe_reqs;
  uint32_t probe_resps;
  uint32_t data_frames;
  uint32_t ctrl_frames;
  uint32_t mgmt_frames;
  uint32_t pkts_2ghz;
  uint32_t pkts_5ghz;
  uint32_t unique_aps;
  uint8_t channel;
  int8_t last_rssi;
  uint32_t channel_packets[WIFI_SPECTRUM_BIN_COUNT];
  int8_t channel_peak_rssi[WIFI_SPECTRUM_BIN_COUNT];
  bool handshake_captured;
  bool pmkid_captured;
} wifi_sniffer_stats_t;

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

/** Start a capture constrained to dual, 2.4 GHz, or 5 GHz hopping. */
bool wifi_sniffer_start_band(wifi_sniffer_type_t type, uint8_t channel, wifi_analysis_band_t band);

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
 * @brief Start the sniffer in Packet Monitor mode (counter-only).
 *
 * Same as `wifi_sniffer_start(WIFI_SNIFFER_TYPE_RAW, channel)` but tells
 * the C5 to recycle the PCAP buffer when full (instead of dropping new
 * packets). Use this for the Packet Monitor screen which only reads
 * counters and never saves the PCAP.
 *
 * @param channel  Wi-Fi channel (0 for hopping).
 */
bool wifi_sniffer_start_monitor(uint8_t channel);

/** Start a counter-only monitor constrained to a selected band. */
bool wifi_sniffer_start_monitor_band(uint8_t channel, wifi_analysis_band_t band);

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

/** Full path of the active/latest PCAP, or an empty string for counter-only mode. */
const char *wifi_sniffer_get_capture_path(void);

/** Fetch one coherent statistics snapshot from the C5. */
bool wifi_sniffer_get_stats(wifi_sniffer_stats_t *out);

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
