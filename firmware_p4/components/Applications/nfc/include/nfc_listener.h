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

#ifndef NFC_LISTENER_H
#define NFC_LISTENER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_types.h"
#include "mf_classic_emu.h"

/**
 * @brief Start card emulation from generic card data.
 *
 * @param card Card data to emulate.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if card is NULL
 *   - HB_NFC_ERR_INTERNAL on hardware failure
 */
hb_nfc_err_t nfc_listener_start(const hb_nfc_card_data_t *card);

/**
 * @brief Start emulation from a pre-loaded dump (blocks + keys already filled).
 *
 * Use this after mf_classic_read_full() has populated the mfc_emu_card_data_t.
 *
 * @param emu Emulation card data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if emu is NULL
 *   - HB_NFC_ERR_INTERNAL on hardware failure
 */
hb_nfc_err_t nfc_listener_start_emu(const mfc_emu_card_data_t *emu);

/**
 * @brief Stop card emulation.
 */
void nfc_listener_stop(void);

/** Firmware retains the antenna while startup or cleanup is pending. */
bool nfc_listener_is_active(void);
bool nfc_listener_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_LISTENER_H */
