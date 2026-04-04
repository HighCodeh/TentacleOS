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

/**
 * BLE Spam Indices:
 * 0: AppleJuice (Airpods, Beats, etc)
 * 1: SourApple (AppleTV Setup, Phone Setup, etc)
 * 2: SwiftPair (Microsoft Windows popups)
 * 3: Samsung (Samsung Watch/Galaxy popups)
 * 4: Android (Google Fast Pair)
 * 5: Tutti-Frutti (Cycles through all above)
 */

#include "canned_spam.h"
#include "bluetooth_service.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "CANNED_SPAM";

typedef enum {
  CAT_APPLE_JUICE = 0,
  CAT_SOUR_APPLE,
  CAT_SWIFT_PAIR,
  CAT_SAMSUNG,
  CAT_ANDROID,
  CAT_TUTTI_FRUTTI,
  CAT_COUNT
} SpamCategory;

static const SpamType category_info[] = {
  {"AppleJuice"},
  {"SourApple"},
  {"SwiftPair"},
  {"Samsung"},
  {"Android"},
  {"Tutti-Frutti"}
};

static bool spam_running = false;
static int current_category = -1;

int spam_get_attack_count(void) {
  return CAT_COUNT;
}

const SpamType* spam_get_attack_type(int index) {
  if (index < 0 || index >= CAT_COUNT) {
    return NULL;
  }
  return &category_info[index];
}

esp_err_t spam_start(int attack_index) {
  if (spam_running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service is not running");
    return ESP_FAIL;
  }

  if (attack_index < 0 || attack_index >= CAT_COUNT) {
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t payload = (uint8_t)attack_index;
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_APP_SPAM,
      &payload, sizeof(payload),
      &resp_hdr, resp_buf, 5000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGE(TAG, "Failed to start spam on C5");
    return ESP_FAIL;
  }

  current_category = attack_index;
  spam_running = true;

  ESP_LOGI(TAG, "Spam started for category: %s", category_info[attack_index].name);
  return ESP_OK;
}

esp_err_t spam_stop(void) {
  if (!spam_running) {
    return ESP_ERR_INVALID_STATE;
  }

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_APP_STOP,
      NULL, 0,
      &resp_hdr, resp_buf, 2000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGW(TAG, "Failed to stop spam on C5");
  }

  spam_running = false;
  ESP_LOGI(TAG, "Spam stopped");
  return ESP_OK;
}
