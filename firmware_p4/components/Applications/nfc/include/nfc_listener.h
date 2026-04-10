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

#ifndef NFC_LISTENER_H
#define NFC_LISTENER_H

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* NFC_LISTENER_H */
