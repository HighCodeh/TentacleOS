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

#include "spi_protocol.h"

/**
 * @brief Packet filter type for the Wi-Fi sniffer.
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
 * @brief Start the Wi-Fi sniffer with PCAP buffering.
 *
 * @param type     Packet filter type.
 * @param channel  Wi-Fi channel (0 for channel hopping).
 * @return true on success, false on failure.
 */
bool wifi_sniffer_start(wifi_sniffer_type_t type, uint8_t channel);

/**
 * @brief Stop the Wi-Fi sniffer.
 */
void wifi_sniffer_stop(void);

/**
 * @brief Enable monitor mode: when the local PCAP buffer fills up, the
 * buffer recycles instead of dropping packets, and the packet counter
 * keeps incrementing forever. Used by the Packet Monitor screen which
 * only needs the counter, not the captured PCAP.
 */
void wifi_sniffer_set_monitor_mode(bool enabled);

/**
 * @brief Bind a session ID to the running sniffer (called by dispatcher
 * after session_manager_start). Streams will be emitted via the session.
 */
void wifi_sniffer_bind_session(uint32_t session_id);

/**
 * @brief Kill callback: stops the sniffer when its session watchdog fires.
 *  Compatible with session_kill_cb_t.
 */
void wifi_sniffer_session_killed(spi_id_t op_id);

/**
 * @brief Save the PCAP buffer to internal flash.
 *
 * @param filename  Output filename.
 * @return true on success, false on failure.
 */
bool wifi_sniffer_save_to_internal_flash(const char *filename);

/**
 * @brief Save the PCAP buffer to SD card.
 *
 * @param filename  Output filename.
 * @return true on success, false on failure.
 */
bool wifi_sniffer_save_to_sd_card(const char *filename);

/**
 * @brief Free the PCAP capture buffer.
 */
void wifi_sniffer_free_buffer(void);

/**
 * @brief Get the total number of captured packets.
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
 * @brief Get the current PCAP buffer usage in bytes.
 *
 * @return Number of bytes used.
 */
uint32_t wifi_sniffer_get_buffer_usage(void);

/**
 * @brief Fill the extended monitor fields (per-type/per-band tallies, unique
 *        APs, last channel) of a sniffer stats payload.
 *
 * @param out Stats struct whose extended tail is populated. The base fields
 *            (packets/deauths/...) are left untouched.
 */
void wifi_sniffer_fill_ext_stats(spi_sniffer_stats_t *out);

/**
 * @brief Set the maximum snapshot length for captured packets.
 *
 * @param len  Maximum bytes to capture per packet.
 */
void wifi_sniffer_set_snaplen(uint16_t len);

/**
 * @brief Enable or disable verbose packet logging.
 *
 * @param is_verbose  true to enable, false to disable.
 */
void wifi_sniffer_set_verbose(bool is_verbose);

/**
 * @brief Start the sniffer with streaming directly to SD card.
 *
 * @param type      Packet filter type.
 * @param channel   Wi-Fi channel (0 for channel hopping).
 * @param filename  Output filename on SD card.
 * @return true on success, false on failure.
 */
bool wifi_sniffer_start_stream_sd(wifi_sniffer_type_t type, uint8_t channel, const char *filename);

/**
 * @brief Check if a PMKID has been captured.
 *
 * @return true if PMKID is available, false otherwise.
 */
bool wifi_sniffer_pmkid_captured(void);

/**
 * @brief Clear the captured PMKID state.
 */
void wifi_sniffer_clear_pmkid(void);

/**
 * @brief Get the BSSID associated with the captured PMKID.
 *
 * @param[out] out_bssid  Buffer to store the BSSID (6 bytes).
 */
void wifi_sniffer_get_pmkid_bssid(uint8_t out_bssid[6]);

/**
 * @brief Check if a WPA handshake has been captured.
 *
 * @return true if handshake is available, false otherwise.
 */
bool wifi_sniffer_handshake_captured(void);

/**
 * @brief Clear the captured handshake state.
 */
void wifi_sniffer_clear_handshake(void);

/**
 * @brief Get the BSSID associated with the captured handshake.
 *
 * @param[out] out_bssid  Buffer to store the BSSID (6 bytes).
 */
void wifi_sniffer_get_handshake_bssid(uint8_t out_bssid[6]);

#ifdef __cplusplus
}
#endif

#endif // WIFI_SNIFFER_H
