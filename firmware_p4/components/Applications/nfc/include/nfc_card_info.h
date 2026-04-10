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

#ifndef NFC_CARD_INFO_H
#define NFC_CARD_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** @name SAK (Select Acknowledge) byte values for ISO 14443-3A cards. */
/** @{ */
#define NFC_SAK_ULTRALIGHT     0x00 /**< MIFARE Ultralight / NTAG */
#define NFC_SAK_CLASSIC_1K_TNP 0x01 /**< TNP3XXX (Classic 1K protocol) */
#define NFC_SAK_CLASSIC_1K     0x08 /**< MIFARE Classic 1K */
#define NFC_SAK_CLASSIC_MINI   0x09 /**< MIFARE Classic Mini 0.3K */
#define NFC_SAK_PLUS_2K_SL2    0x10 /**< MIFARE Plus 2K (SL2) */
#define NFC_SAK_PLUS_4K_SL2    0x11 /**< MIFARE Plus 4K (SL2) */
#define NFC_SAK_CLASSIC_4K     0x18 /**< MIFARE Classic 4K */
#define NFC_SAK_ISO_DEP        0x20 /**< DESFire / Plus SL3 / ISO-DEP capable */
#define NFC_SAK_CLASSIC_1K_EV1 0x28 /**< MIFARE Classic 1K (emulated / EV1) */
#define NFC_SAK_CLASSIC_4K_EV1 0x38 /**< MIFARE Classic 4K (emulated / EV1) */
#define NFC_SAK_CL3_CASCADE    0x60 /**< ISO 14443-4 (CL3 cascade) */
#define NFC_SAK_CLASSIC_1K_INF 0x88 /**< MIFARE Classic 1K (Infineon) */
#define NFC_SAK_ISO_DEP_BIT    0x20 /**< Bit flag indicating ISO-DEP support */
/** @} */

/** @name ISO 14443-3B REQB default parameters. */
/** @{ */
#define NFC_REQB_AFI_ALL       0x00 /**< AFI: match all application families */
#define NFC_REQB_PARAM_DEFAULT 0x00 /**< Default REQB parameter byte */
/** @} */

/**
 * @brief Card type identification result.
 */
typedef struct {
  const char *name;      /**< @brief Short card type name. */
  const char *full_name; /**< @brief Full card type name. */
  bool is_mf_classic;    /**< @brief true if MIFARE Classic. */
  bool is_mf_ultralight; /**< @brief true if MIFARE Ultralight. */
  bool is_iso_dep;       /**< @brief true if ISO-DEP capable. */
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
