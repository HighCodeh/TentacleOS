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
#include "wifi_service.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "SIGNAL_MONITOR";

static uint8_t target_bssid[6] = {0};
static int8_t last_rssi = -127;
static int64_t last_seen_time = 0;
static bool is_running = false;

// Basic 802.11 MAC Header to find Source Address (Addr2)
typedef struct {
  uint16_t frame_control;
  uint16_t duration;
  uint8_t addr1[6]; // Receiver / Destination
  uint8_t addr2[6]; // Transmitter / Source (This is usually the BSSID for APs)
  uint8_t addr3[6];
  uint16_t seq_ctrl;
} __attribute__((packed)) wifi_mac_header_t;

static void monitor_callback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (!is_running) return;

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_mac_header_t *mac_header = (const wifi_mac_header_t *)ppkt->payload;

  // We check if the packet was sent BY the target (Addr2)
  // This gives us the signal strength of the Target -> Us
  if (memcmp(mac_header->addr2, target_bssid, 6) == 0) {
    // Update RSSI (Simple smoothing could be added here if needed)
    last_rssi = ppkt->rx_ctrl.rssi;
    last_seen_time = esp_timer_get_time();
  }
}

void signal_monitor_start(const uint8_t *bssid, uint8_t channel) {
  if (is_running) {
    signal_monitor_stop();
  }

  if (bssid == NULL) return;

  memcpy(target_bssid, bssid, 6);
  last_rssi = -127;
  last_seen_time = 0;
  is_running = true;

  ESP_LOGI(TAG, "Starting Signal Monitor for Target: %02x:%02x:%02x:%02x:%02x:%02x on Ch %d",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  wifi_promiscuous_filter_t filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
  };

  // 3. Start via Service
  wifi_service_promiscuous_start(monitor_callback, &filter);
}

void signal_monitor_stop(void) {
  if (!is_running) return;

  is_running = false;
  wifi_service_promiscuous_stop();

  memset(target_bssid, 0, 6);
  ESP_LOGI(TAG, "Signal Monitor Stopped");
}

int8_t signal_monitor_get_rssi(void) {
  return last_rssi;
}

uint32_t signal_monitor_get_last_seen_ms(void) {
  if (last_seen_time == 0) return UINT32_MAX;

  int64_t now = esp_timer_get_time();
  return (uint32_t)((now - last_seen_time) / 1000);
}
