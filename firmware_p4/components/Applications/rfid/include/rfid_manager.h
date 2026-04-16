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

#ifndef RFID_MANAGER_H
#define RFID_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"

#include "ys_rfid2_types.h"
#include "rfid_types.h"

/**
 * @brief Full card event with raw + decoded data.
 */
typedef struct {
  ys_rfid2_event_t driver_event;
  rfid_decoded_data_t decoded;
  bool is_decoded;
} rfid_card_event_t;

/**
 * @brief Callback invoked when a card event occurs.
 *
 * @param event  Pointer to card event. Valid only during callback scope.
 * @param ctx    User context pointer.
 */
typedef void (*rfid_manager_event_cb_t)(const rfid_card_event_t *event, void *ctx);

/**
 * @brief Start the RFID manager.
 *
 * Starts the YS-RFID2 driver and routes events through the protocol registry.
 *
 * @param cb   Event callback. Must not be NULL.
 * @param ctx  User context pointer.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if cb is NULL
 *   - ESP_ERR_INVALID_STATE if already running
 */
esp_err_t rfid_manager_start(rfid_manager_event_cb_t cb, void *ctx);

/**
 * @brief Stop the RFID manager.
 *
 * Stops the YS-RFID2 driver.
 */
void rfid_manager_stop(void);

/**
 * @brief Check if the manager is running.
 */
bool rfid_manager_is_running(void);

/**
 * @brief Get the last card event.
 *
 * @param[out] out_event  Pointer to receive the event.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_NOT_FOUND if no card has been detected yet
 *   - ESP_ERR_INVALID_ARG if out_event is NULL
 */
esp_err_t rfid_manager_get_last_card(rfid_card_event_t *out_event);

#ifdef __cplusplus
}
#endif

#endif // RFID_MANAGER_H
