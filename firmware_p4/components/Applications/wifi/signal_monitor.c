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

#include "signal_monitor.h"

#include "led_control.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"

static const char *TAG = "SIGNAL_MONITOR";

#define SIGNAL_MONITOR_PAYLOAD_SIZE 7
#define SIGNAL_MONITOR_DEFAULT_RSSI (-127)
#define SIGNAL_MONITOR_STATS_INDEX  0xEEEE

static int8_t s_last_rssi = SIGNAL_MONITOR_DEFAULT_RSSI;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

void signal_monitor_start(const uint8_t *bssid, uint8_t channel) {
  ESP_LOGI(TAG, "Signal monitor started");
  uint8_t payload[SIGNAL_MONITOR_PAYLOAD_SIZE];
  memcpy(payload, bssid, 6);
  payload[6] = channel;
  s_session_id = spi_session_start(
      SPI_ID_WIFI_APP_SIGNAL_MON, payload, SIGNAL_MONITOR_PAYLOAD_SIZE, NULL, NULL);
  s_session_id != SPI_SESSION_INVALID_ID ? led_signal_info() : led_signal_error();
}

void signal_monitor_stop(void) {
  ESP_LOGI(TAG, "Signal monitor stopped");
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
}

int8_t signal_monitor_get_rssi(void) {
  spi_header_t resp;
  uint16_t magic_stats = SIGNAL_MONITOR_STATS_INDEX;
  spi_sniffer_stats_t stats;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&magic_stats, 2, &resp, (uint8_t *)&stats, sizeof(stats), 1000) ==
      ESP_OK) {
    s_last_rssi = stats.signal_rssi;
  }
  return s_last_rssi;
}

uint32_t signal_monitor_get_last_seen_ms(void) {
  return 0;
}
