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


#include "deauther_detector.h"
#include "wifi_service.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "DEAUTH_DETECTOR";

static uint32_t *deauth_counter = NULL;

// WiFi 802.11 Frame Control
typedef struct {
  uint16_t protocol:2;
  uint16_t type:2;
  uint16_t subtype:4;
  uint16_t to_ds:1;
  uint16_t from_ds:1;
  uint16_t more_frag:1;
  uint16_t retry:1;
  uint16_t pwr_mgt:1;
  uint16_t more_data:1;
  uint16_t protected_frame:1;
  uint16_t order:1;
} __attribute__((packed)) wifi_frame_control_t;

static void wifi_sniffer_packet_handler(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) {
    return;
  }

  const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buf;
  const wifi_frame_control_t *frame_control = (const wifi_frame_control_t *)ppkt->payload;

  // Type 0 (Management) and Subtype 12 (0xC - Deauthentication)
  if (frame_control->type == 0 && frame_control->subtype == 0xC) {
    if (deauth_counter) {
      (*deauth_counter)++;
      // Optional: Log every N packets to avoid flooding
      if ((*deauth_counter) % 10 == 0) {
        ESP_LOGW(TAG, "Deauth detected! Total: %lu", *deauth_counter);
      }
    }
  }
}

void deauther_detector_start(void) {
  if (deauth_counter == NULL) {
    deauth_counter = (uint32_t *)heap_caps_malloc(sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    if (deauth_counter == NULL) {
      ESP_LOGE(TAG, "Failed to allocate deauth counter in PSRAM!");
      return;
    }
    *deauth_counter = 0;
  }

  ESP_LOGI(TAG, "Starting Deauth Detector...");

  wifi_promiscuous_filter_t filter = {
    .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
  };

  wifi_service_promiscuous_start(wifi_sniffer_packet_handler, &filter);

  wifi_service_start_channel_hopping();
}

void deauther_detector_stop(void) {
  wifi_service_stop_channel_hopping();

  wifi_service_promiscuous_stop();

  if (deauth_counter) {
    heap_caps_free(deauth_counter);
    deauth_counter = NULL;
  }

  ESP_LOGI(TAG, "Deauth Detector Stopped.");
}

uint32_t deauther_detector_get_count(void) {
  if (deauth_counter) {
    return *deauth_counter;
  }
  return 0;
}
