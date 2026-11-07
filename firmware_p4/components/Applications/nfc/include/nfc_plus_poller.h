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

#ifndef NFC_PLUS_POLLER_H
#define NFC_PLUS_POLLER_H

#include "nfc_plus_file.h"
#include "mf_plus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*nfc_plus_poller_cancel_cb_t)(void);
/* Caller owns RF. Non-SL3/unknown geometry returns identity only; level 255 is unknown. */
hb_nfc_err_t nfc_plus_poller_read(const nfc_iso14443a_data_t *identity,
                                  const uint8_t *keys,
                                  size_t key_count,
                                  nfc_plus_poller_cancel_cb_t cancel,
                                  nfc_plus_file_t *out_card,
                                  uint8_t *out_security_level);
/* Only recovered ordinary blocks, never UID/trailers/keys/config; no write retry. */
hb_nfc_err_t nfc_plus_poller_write(const nfc_iso14443a_data_t *identity,
                                   const nfc_plus_file_t *card,
                                   bool is_encrypted,
                                   nfc_plus_poller_cancel_cb_t cancel,
                                   uint16_t *out_written);

#ifdef __cplusplus
}
#endif
#endif
