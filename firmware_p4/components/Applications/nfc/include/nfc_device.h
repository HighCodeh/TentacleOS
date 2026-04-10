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

#ifndef NFC_DEVICE_H
#define NFC_DEVICE_H

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "mf_classic_emu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_DEVICE_MAX_PROFILES  8
#define NFC_DEVICE_NAME_MAX_LEN  32
#define NFC_DEVICE_NVS_NAMESPACE "nfc_cards"

/**
 * @brief Lightweight profile info (no block data).
 */
typedef struct {
  char name[NFC_DEVICE_NAME_MAX_LEN]; /**< @brief Human-readable profile name. */
  uint8_t uid[10];                    /**< @brief Card UID bytes. */
  uint8_t uid_len;                    /**< @brief UID length in bytes. */
  uint8_t sak;                        /**< @brief SAK byte. */
  mf_classic_type_t type;             /**< @brief MIFARE Classic card type. */
  int sector_count;                   /**< @brief Number of sectors on card. */
  int keys_known;                     /**< @brief Number of known keys. */
  bool is_complete;                   /**< @brief true if all keys are known. */
} nfc_device_profile_info_t;

/**
 * @brief Initialize NVS storage for card profiles.
 *
 * Call once at startup.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INTERNAL on NVS initialization failure
 */
hb_nfc_err_t nfc_device_init(void);

/**
 * @brief Save a card profile to NVS.
 *
 * @param name Human-readable name (e.g., "Office Card").
 * @param card Full card dump with keys.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if name or card is NULL
 *   - HB_NFC_ERR_NO_MEM if storage is full
 *   - HB_NFC_ERR_INTERNAL on NVS write failure
 */
hb_nfc_err_t nfc_device_save(const char *name, const mfc_emu_card_data_t *card);

/**
 * @brief Load a card profile from NVS by index.
 *
 * @param      index Profile index (0..count-1).
 * @param[out] card  Loaded card data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if card is NULL or index is out of range
 *   - HB_NFC_ERR_NOT_FOUND if profile does not exist
 */
hb_nfc_err_t nfc_device_load(int index, mfc_emu_card_data_t *card);

/**
 * @brief Load a card profile from NVS by name.
 *
 * @param      name Profile name.
 * @param[out] card Loaded card data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if name or card is NULL
 *   - HB_NFC_ERR_NOT_FOUND if profile does not exist
 */
hb_nfc_err_t nfc_device_load_by_name(const char *name, mfc_emu_card_data_t *card);

/**
 * @brief Delete a card profile from NVS.
 *
 * @param index Profile index.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if index is out of range
 *   - HB_NFC_ERR_NOT_FOUND if profile does not exist
 */
hb_nfc_err_t nfc_device_delete(int index);

/**
 * @brief Get number of saved profiles.
 *
 * @return Number of saved profiles.
 */
int nfc_device_get_count(void);

/**
 * @brief Get profile info (lightweight, no block data).
 *
 * @param      index Profile index.
 * @param[out] info  Profile info without block data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if info is NULL or index is out of range
 *   - HB_NFC_ERR_NOT_FOUND if profile does not exist
 */
hb_nfc_err_t nfc_device_get_info(int index, nfc_device_profile_info_t *info);

/**
 * @brief List all saved profiles.
 *
 * @param[out] infos     Output array (must have space for NFC_DEVICE_MAX_PROFILES entries).
 * @param      max_count Maximum entries to fill.
 * @return Number of profiles found.
 */
int nfc_device_list(nfc_device_profile_info_t *infos, int max_count);

/**
 * @brief Set the active profile index for emulation.
 *
 * @param index Profile index.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if index is out of range
 */
hb_nfc_err_t nfc_device_set_active(int index);

/**
 * @brief Get the active profile index for emulation.
 *
 * @return Active profile index, or -1 if none.
 */
int nfc_device_get_active(void);

/**
 * @brief Save generic card data to NVS (legacy wrapper).
 *
 * @param name Profile name.
 * @param card Card data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if name or card is NULL
 *   - HB_NFC_ERR_NO_MEM if storage is full
 */
hb_nfc_err_t nfc_device_save_generic(const char *name, const hb_nfc_card_data_t *card);

/**
 * @brief Load generic card data from NVS (legacy wrapper).
 *
 * @param      name Profile name.
 * @param[out] card Loaded card data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if name or card is NULL
 *   - HB_NFC_ERR_NOT_FOUND if profile does not exist
 */
hb_nfc_err_t nfc_device_load_generic(const char *name, hb_nfc_card_data_t *card);

/**
 * @brief Get protocol name string.
 *
 * @param proto Protocol type.
 * @return Protocol name string.
 */
const char *nfc_device_protocol_name(hb_nfc_protocol_t proto);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DEVICE_H */
