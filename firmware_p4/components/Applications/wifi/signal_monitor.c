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

#include "signal_monitor.h"

#include <string.h>

#include "spi_bridge.h"

#define SIGNAL_MONITOR_PAYLOAD_SIZE 7
#define SIGNAL_MONITOR_DEFAULT_RSSI (-127)
#define SIGNAL_MONITOR_STATS_INDEX  0xEEEE

static int8_t s_last_rssi = SIGNAL_MONITOR_DEFAULT_RSSI;

void signal_monitor_start(const uint8_t *bssid, uint8_t channel) {
  uint8_t payload[SIGNAL_MONITOR_PAYLOAD_SIZE];
  memcpy(payload, bssid, 6);
  payload[6] = channel;
  spi_bridge_send_command(
      SPI_ID_WIFI_APP_SIGNAL_MON, payload, SIGNAL_MONITOR_PAYLOAD_SIZE, NULL, NULL, 2000);
}

void signal_monitor_stop(void) {
  spi_bridge_send_command(SPI_ID_WIFI_APP_ATTACK_STOP, NULL, 0, NULL, NULL, 2000);
}

int8_t signal_monitor_get_rssi(void) {
  spi_header_t resp;
  uint16_t magic_stats = SIGNAL_MONITOR_STATS_INDEX;
  spi_sniffer_stats_t stats;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&magic_stats, 2, &resp, (uint8_t *)&stats, 1000) ==
      ESP_OK) {
    s_last_rssi = stats.signal_rssi;
  }
  return s_last_rssi;
}

uint32_t signal_monitor_get_last_seen_ms(void) {
  return 0;
}
