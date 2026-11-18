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
/* Legacy nested API. Returns ESP_ERR_NOT_SUPPORTED without RF activity.
 * Reader MFKey transcripts are not nested card-authentication samples. */
#ifndef MF_NESTED_H
#define MF_NESTED_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "mf_classic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result from one nested auth probe.
 */
typedef struct {
  uint32_t nt;     /**< @brief Decrypted target nonce (plaintext). */
  uint32_t nr_enc; /**< @brief Encrypted reader nonce (as sent on wire). */
  uint32_t ar_enc; /**< @brief Encrypted reader response (as sent on wire). */
} mf_nested_sample_t;

hb_nfc_err_t mf_nested_collect_sample(uint8_t target_block,
                                      const uint8_t uid[4],
                                      uint32_t nr_chosen,
                                      mf_nested_sample_t *sample);

#define MF_NESTED_SAMPLE_COUNT 8
#define MF_NESTED_MAX_ATTEMPTS 32

hb_nfc_err_t mf_nested_attack(nfc_iso14443a_data_t *card,
                              uint8_t src_sector,
                              const mf_classic_key_t *src_key,
                              mf_key_type_t src_key_type,
                              uint8_t target_sector,
                              mf_classic_key_t *found_key_out,
                              mf_key_type_t *found_key_type_out);

#ifdef __cplusplus
}
#endif
#endif
