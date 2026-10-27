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

typedef enum {
  NFC_MANAGER_TECH_A = 0,
  NFC_MANAGER_TECH_B,
  NFC_MANAGER_TECH_F,
  NFC_MANAGER_TECH_V,
  NFC_MANAGER_TECH_COUNT,
} nfc_manager_tech_t;

// Caller initializes hardware, owns the antenna and turns the field off after this probe.
esp_err_t nfc_manager_poll(nfc_manager_tech_t tech, hb_nfc_card_data_t *out_card);

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
