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

#include "mf_nested.h"
#include <string.h>

/* Legacy API retained for callers. The former implementation fabricated reader
 * responses from the source key, which cannot recover the target key. Real
 * nested/static-nested recovery requires a separate, validated implementation. */
hb_nfc_err_t mf_nested_collect_sample(uint8_t target_block,
                                      const uint8_t uid[4],
                                      uint32_t nr_chosen,
                                      mf_nested_sample_t *sample) {
  (void)target_block;
  (void)uid;
  (void)nr_chosen;
  if (sample == NULL)
    return HB_NFC_ERR_PARAM;
  memset(sample, 0, sizeof(*sample));
  return ESP_ERR_NOT_SUPPORTED;
}

hb_nfc_err_t mf_nested_attack(nfc_iso14443a_data_t *card,
                              uint8_t src_sector,
                              const mf_classic_key_t *src_key,
                              mf_key_type_t src_key_type,
                              uint8_t target_sector,
                              mf_classic_key_t *found_key_out,
                              mf_key_type_t *found_key_type_out) {
  (void)card;
  (void)src_sector;
  (void)src_key;
  (void)src_key_type;
  (void)target_sector;
  if (found_key_out == NULL || found_key_type_out == NULL)
    return HB_NFC_ERR_PARAM;
  memset(found_key_out, 0, sizeof(*found_key_out));
  *found_key_type_out = MF_KEY_A;
  return ESP_ERR_NOT_SUPPORTED;
}
