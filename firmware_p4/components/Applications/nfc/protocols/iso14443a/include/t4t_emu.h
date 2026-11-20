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
/**
 * @file t4t_emu.h
 * @brief ISO14443-4 / T4T emulation (ISO-DEP target).
 */
#ifndef T4T_EMU_H
#define T4T_EMU_H

#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "mf_plus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define T4T_EMU_UID_CAPACITY 7U
#define T4T_EMU_ATS_CAPACITY 32U

typedef struct {
  uint8_t uid[T4T_EMU_UID_CAPACITY];
  uint8_t uid_len;
  uint8_t atqa[2];
  uint8_t sak;
  uint8_t ats[T4T_EMU_ATS_CAPACITY];
  uint8_t ats_len;
} t4t_emu_identity_t;

/* ISO-DEP identity only; application commands are not emulated. */
struct nfc_t4t_listener;
hb_nfc_err_t t4t_emu_init_ndef(struct nfc_t4t_listener *listener);
hb_nfc_err_t t4t_emu_init_identity(const t4t_emu_identity_t *identity);

/* Caller owns the listener through stop; initialize only with the RF worker idle. */
hb_nfc_err_t t4t_emu_init_plus(const t4t_emu_identity_t *identity, mfp_listener_t *listener);

/**
 * @brief Initialize default NDEF and CC data for emulation.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_MEM on allocation failure
 */
hb_nfc_err_t t4t_emu_init_default(void);

/**
 * @brief Configure ST25R3916 as NFC-A target for ISO-DEP.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on hardware failure
 */
hb_nfc_err_t t4t_emu_configure_target(void);

/**
 * @brief Start emulation and enter SLEEP state.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on hardware failure
 */
hb_nfc_err_t t4t_emu_start(void);

/**
 * @brief Stop emulation.
 */
void t4t_emu_stop(void);

/**
 * @brief Run one emulation step (call in tight loop).
 */
void t4t_emu_run_step(void);
bool t4t_emu_has_error(void);

#ifdef __cplusplus
}
#endif

#endif /* T4T_EMU_H */
