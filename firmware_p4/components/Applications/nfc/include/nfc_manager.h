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

#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "highboy_nfc.h"
#include "highboy_nfc_types.h"
#include "highboy_nfc_compat.h"

/**
 * @brief NFC manager state machine states.
 */
typedef enum {
  NFC_MANAGER_STATE_IDLE = 0,
  NFC_MANAGER_STATE_SCANNING,
  NFC_MANAGER_STATE_READING,
  NFC_MANAGER_STATE_EMULATING,
  NFC_MANAGER_STATE_ERROR,
  NFC_MANAGER_STATE_COUNT
} nfc_manager_state_t;

/**
 * @brief Callback invoked when a card is found.
 *
 * @param card Pointer to the card data.
 * @param ctx  User context.
 */
typedef void (*nfc_manager_card_found_cb_t)(const hb_nfc_card_data_t *card, void *ctx);

/**
 * @brief Start the NFC manager scan task.
 *
 * Hardware must be pre-initialized with highboy_nfc_init().
 *
 * @param cb  Card found callback.
 * @param ctx User context.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if cb is NULL
 *   - HB_NFC_ERR_INTERNAL on task creation failure
 */
hb_nfc_err_t nfc_manager_start(nfc_manager_card_found_cb_t cb, void *ctx);

/**
 * @brief Stop the NFC manager.
 */
void nfc_manager_stop(void);

/**
 * @brief Get current manager state.
 *
 * @return Current state.
 */
nfc_manager_state_t nfc_manager_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_MANAGER_H */
