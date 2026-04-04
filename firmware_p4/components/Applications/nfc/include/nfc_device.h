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
 * @file nfc_device.h
 * @brief NFC Device Card profile save/load via NVS.
 *
 * Provides persistent storage for card dumps (profiles).
 * Each profile stores: UID, ATQA, SAK, type, all blocks, all keys.
 *
 * Storage layout in NVS namespace "nfc_cards":
 *  "count" uint8_t: number of saved profiles
 *  "name_0" string: profile 0 name
 *  "data_0" blob: serialized card data
 *  "name_1" string: profile 1 name
 *  "data_1" blob: serialized card data
 *  ...
 *  "active" uint8_t: currently active profile index
 *
 * Max profiles limited by NVS partition size (typically 5-10 for 4K cards).
 */
#ifndef NFC_DEVICE_H
#define NFC_DEVICE_H

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "mf_classic_emu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NFC_DEVICE_MAX_PROFILES     8
#define NFC_DEVICE_NAME_MAX_LEN     32
#define NFC_DEVICE_NVS_NAMESPACE    "nfc_cards"

typedef struct {
    char    name[NFC_DEVICE_NAME_MAX_LEN];
    uint8_t uid[10];
    uint8_t uid_len;
    uint8_t sak;
    mf_classic_type_t type;
    int     sector_count;
    int     keys_known;    
    bool    complete;      
} nfc_device_profile_info_t;

/**
 * Initialize NVS storage for card profiles.
 * Call once at startup.
 */
hb_nfc_err_t nfc_device_init(void);

/**
 * Save a card profile to NVS.
 * @param name Human-readable name (e.g., "Office Card")
 * @param card Full card dump with keys
 * @return HB_NFC_OK on success
 */
hb_nfc_err_t nfc_device_save(const char* name, const mfc_emu_card_data_t* card);

/**
 * Load a card profile from NVS by index.
 * @param index Profile index (0..count-1)
 * @param card Output: card data
 * @return HB_NFC_OK on success
 */
hb_nfc_err_t nfc_device_load(int index, mfc_emu_card_data_t* card);

/**
 * Load a card profile from NVS by name.
 */
hb_nfc_err_t nfc_device_load_by_name(const char* name, mfc_emu_card_data_t* card);

/**
 * Delete a card profile from NVS.
 */
hb_nfc_err_t nfc_device_delete(int index);

/**
 * Get number of saved profiles.
 */
int nfc_device_get_count(void);

/**
 * Get profile info (lightweight, no block data).
 */
hb_nfc_err_t nfc_device_get_info(int index, nfc_device_profile_info_t* info);

/**
 * List all saved profiles.
 * @param infos Output array (must have space for NFC_DEVICE_MAX_PROFILES entries)
 * @return Number of profiles found
 */
int nfc_device_list(nfc_device_profile_info_t* infos, int max_count);

/**
 * Set/get the active profile index for emulation.
 */
hb_nfc_err_t nfc_device_set_active(int index);
int           nfc_device_get_active(void);

/**
 * Save generic card data to NVS (legacy wrapper).
 */
hb_nfc_err_t nfc_device_save_generic(const char* name, const hb_nfc_card_data_t* card);

/**
 * Load generic card data from NVS (legacy wrapper).
 */
hb_nfc_err_t nfc_device_load_generic(const char* name, hb_nfc_card_data_t* card);

/**
 * Get protocol name string.
 */
const char* nfc_device_protocol_name(hb_nfc_protocol_t proto);

#ifdef __cplusplus
}
#endif

#endif 
