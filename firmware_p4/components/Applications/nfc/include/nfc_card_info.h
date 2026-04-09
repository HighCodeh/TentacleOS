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
/**
 * @file nfc_card_info.h
 * @brief Card identification helpers (manufacturer + type).
 */
#ifndef NFC_CARD_INFO_H
#define NFC_CARD_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Card type identification result.
 */
typedef struct {
  const char *name;
  const char *full_name;
  bool is_mf_classic;
  bool is_mf_ultralight;
  bool is_iso_dep;
} nfc_card_type_info_t;

/**
 * @brief Get manufacturer name from first UID byte.
 *
 * @param uid0 First byte of the UID.
 * @return Manufacturer name string, or NULL if unknown.
 */
const char *nfc_card_info_get_manufacturer(uint8_t uid0);

/**
 * @brief Identify card type from SAK and ATQA.
 *
 * @param sak  SAK byte.
 * @param atqa ATQA bytes (2 bytes).
 * @return Card type information.
 */
nfc_card_type_info_t nfc_card_info_identify(uint8_t sak, const uint8_t atqa[2]);

#ifdef __cplusplus
}
#endif

#endif /* NFC_CARD_INFO_H */
