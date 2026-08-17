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

#include "wifi_sniffer.h"

#include "led_control.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"
#include "storage_stream.h"

static const char *TAG = "WIFI_SNIFFER";

#define WIFI_SNIFFER_MIN_FRAME_LEN 3

static spi_sniffer_stats_t s_cached_stats;
static wifi_sniffer_cb_t s_stream_cb = NULL;
static storage_stream_t s_capture_stream = NULL;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

bool wifi_sniffer_stop_capture(void);

static void session_stream_cb(const uint8_t *payload, uint8_t len) {
  if (payload == NULL || len < WIFI_SNIFFER_MIN_FRAME_LEN)
    return;
  const spi_wifi_sniffer_frame_t *frame = (const spi_wifi_sniffer_frame_t *)payload;
  uint16_t data_len = frame->len;
  if (data_len > (len - WIFI_SNIFFER_MIN_FRAME_LEN))
    data_len = (len - WIFI_SNIFFER_MIN_FRAME_LEN);

  if (s_capture_stream != NULL) {
    storage_stream_write(s_capture_stream, frame->data, data_len);
  }

  if (s_stream_cb != NULL) {
    s_stream_cb(frame->data, data_len, frame->rssi, frame->channel);
  }
}

static void session_lost_cb(uint32_t session_id, spi_id_t op_id) {
  (void)op_id;
  if (session_id == s_session_id) {
    s_session_id = SPI_SESSION_INVALID_ID;
    wifi_sniffer_stop_capture();
    s_stream_cb = NULL;
  }
}

static void update_stats(void) {
  spi_header_t resp;
  uint16_t magic_stats = SPI_DATA_INDEX_STATS;
  spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                          (uint8_t *)&magic_stats,
                          2,
                          &resp,
                          (uint8_t *)&s_cached_stats,
                          sizeof(s_cached_stats),
                          1000);
}

static bool
start_internal(wifi_sniffer_type_t type, uint8_t channel, bool monitor_mode, wifi_sniffer_cb_t cb) {
  uint8_t payload[3];
  payload[0] = (uint8_t)type;
  payload[1] = channel;
  payload[2] = monitor_mode ? 1 : 0;
  memset(&s_cached_stats, 0, sizeof(s_cached_stats));
  s_stream_cb = cb;
  s_session_id = spi_session_start(
      SPI_ID_WIFI_APP_SNIFFER, payload, sizeof(payload), session_stream_cb, session_lost_cb);
  if (s_session_id == SPI_SESSION_INVALID_ID) {
    s_stream_cb = NULL;
    led_signal_error();
    return false;
  }
  led_signal_info(); // sniffer session up
  return true;
}

bool wifi_sniffer_start(wifi_sniffer_type_t type, uint8_t channel) {
  return start_internal(type, channel, false, NULL);
}

bool wifi_sniffer_start_stream(wifi_sniffer_type_t type, uint8_t channel, wifi_sniffer_cb_t cb) {
  return start_internal(type, channel, false, cb);
}

bool wifi_sniffer_start_monitor(uint8_t channel) {
  return start_internal(WIFI_SNIFFER_TYPE_RAW, channel, true, NULL);
}

void wifi_sniffer_stop(void) {
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
  wifi_sniffer_stop_capture();
  s_stream_cb = NULL;
}

uint32_t wifi_sniffer_get_packet_count(void) {
  update_stats();
  return s_cached_stats.packets;
}

uint32_t wifi_sniffer_get_deauth_count(void) {
  return s_cached_stats.deauths;
}

uint32_t wifi_sniffer_get_buffer_usage(void) {
  return s_cached_stats.buffer_usage;
}

bool wifi_sniffer_handshake_captured(void) {
  return s_cached_stats.handshake_captured;
}

bool wifi_sniffer_pmkid_captured(void) {
  return s_cached_stats.pmkid_captured;
}

bool wifi_sniffer_start_capture(const char *path) {
  if (path == NULL)
    return false;
  if (s_capture_stream != NULL) {
    storage_stream_close(s_capture_stream);
    s_capture_stream = NULL;
  }
  s_capture_stream = storage_stream_open(path, "wb");
  if (s_capture_stream == NULL) {
    ESP_LOGE(TAG, "Failed to open capture file: %s", path);
    return false;
  }
  ESP_LOGI(TAG, "Capture started: %s", path);
  return true;
}

bool wifi_sniffer_stop_capture(void) {
  if (s_capture_stream == NULL)
    return false;
  storage_stream_flush(s_capture_stream);
  storage_stream_close(s_capture_stream);
  s_capture_stream = NULL;
  ESP_LOGI(TAG, "Capture stopped");
  return true;
}

size_t wifi_sniffer_get_capture_size(void) {
  return storage_stream_bytes_written(s_capture_stream);
}

void wifi_sniffer_free_buffer(void) {
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_FREE_BUFFER, NULL, 0, NULL, NULL, 0, 2000);
  s_cached_stats.buffer_usage = 0;
}

void wifi_sniffer_set_snaplen(uint16_t len) {
  uint8_t payload[2];
  memcpy(payload, &len, sizeof(len));
  spi_bridge_send_command(
      SPI_ID_WIFI_SNIFFER_SET_SNAPLEN, payload, sizeof(payload), NULL, NULL, 0, 2000);
}

void wifi_sniffer_set_verbose(bool is_verbose) {
  uint8_t payload = is_verbose ? 1 : 0;
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_SET_VERBOSE, &payload, 1, NULL, NULL, 0, 2000);
}

void wifi_sniffer_clear_pmkid(void) {
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_CLEAR_PMKID, NULL, 0, NULL, NULL, 0, 2000);
  s_cached_stats.pmkid_captured = false;
}

void wifi_sniffer_get_pmkid_bssid(uint8_t out_bssid[6]) {
  if (out_bssid == NULL)
    return;
  spi_header_t resp;
  uint8_t payload[6] = {0};
  if (spi_bridge_send_command(
          SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID, NULL, 0, &resp, payload, sizeof(payload), 1000) ==
      ESP_OK) {
    memcpy(out_bssid, payload, 6);
  } else {
    memset(out_bssid, 0, 6);
  }
}

void wifi_sniffer_clear_handshake(void) {
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE, NULL, 0, NULL, NULL, 0, 2000);
  s_cached_stats.handshake_captured = false;
}

void wifi_sniffer_get_handshake_bssid(uint8_t out_bssid[6]) {
  if (out_bssid == NULL)
    return;
  spi_header_t resp;
  uint8_t payload[6] = {0};
  if (spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID,
                              NULL,
                              0,
                              &resp,
                              payload,
                              sizeof(payload),
                              1000) == ESP_OK) {
    memcpy(out_bssid, payload, 6);
  } else {
    memset(out_bssid, 0, 6);
  }
}
