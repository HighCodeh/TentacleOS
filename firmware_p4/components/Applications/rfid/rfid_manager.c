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

#include "rfid_manager.h"

#include <string.h>

#include "esp_log.h"

#include "ys_rfid2.h"
#include "rfid_protocol_registry.h"

static const char *TAG = "RFID_MANAGER";

static struct {
  rfid_manager_event_cb_t cb;
  void *ctx;
  bool is_running;
  rfid_card_event_t last_event;
  bool has_last_event;
} s_mgr = {0};

static void driver_event_handler(const ys_rfid2_event_t *event, void *ctx);

esp_err_t rfid_manager_start(rfid_manager_event_cb_t cb, void *ctx) {
  if (cb == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_mgr.is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  rfid_protocol_registry_init();

  s_mgr.cb = cb;
  s_mgr.ctx = ctx;
  s_mgr.has_last_event = false;

  esp_err_t ret = ys_rfid2_start(driver_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start driver: %s", esp_err_to_name(ret));
    return ret;
  }

  s_mgr.is_running = true;
  ESP_LOGI(TAG, "Started");
  return ESP_OK;
}

void rfid_manager_stop(void) {
  if (!s_mgr.is_running) {
    return;
  }

  ys_rfid2_stop();

  s_mgr.is_running = false;
  s_mgr.cb = NULL;
  s_mgr.ctx = NULL;

  ESP_LOGI(TAG, "Stopped");
}

bool rfid_manager_is_running(void) {
  return s_mgr.is_running;
}

esp_err_t rfid_manager_get_last_card(rfid_card_event_t *out_event) {
  if (out_event == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_mgr.has_last_event) {
    return ESP_ERR_NOT_FOUND;
  }

  *out_event = s_mgr.last_event;
  return ESP_OK;
}

static void driver_event_handler(const ys_rfid2_event_t *event, void *ctx) {
  (void)ctx;

  rfid_card_event_t card_event = {
      .driver_event = *event,
      .decoded = {0},
      .is_decoded = false,
  };

  if (event->type == YS_RFID2_EVENT_CARD_DETECTED) {
    card_event.is_decoded = rfid_protocol_registry_decode_all(&event->raw, &card_event.decoded);

    if (card_event.is_decoded) {
      ESP_LOGI(TAG,
               "Card: %s — FC:%u ID:%u",
               card_event.decoded.protocol_name,
               (unsigned)card_event.decoded.facility_code,
               (unsigned)card_event.decoded.card_number);
    } else {
      ESP_LOGI(TAG, "Card: unknown — raw: %s", event->raw.id_str);
    }

    s_mgr.last_event = card_event;
    s_mgr.has_last_event = true;
  }

  if (s_mgr.cb != NULL) {
    s_mgr.cb(&card_event, s_mgr.ctx);
  }
}
