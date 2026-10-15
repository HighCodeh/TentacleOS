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
 * @file mf_key_dict.h
 * @brief MIFARE Classic key dictionary (loaded from .dic files on SD/flash).
 *
 * Default and family dictionaries ship on the firmware flash partition and
 * are copied to the SD card on first boot. The user can edit them or add
 * extra dictionaries. Keys discovered at runtime are appended to
 * `mf_classic_user.dic` on the SD card.
 */
#ifndef MF_KEY_DICT_H
#define MF_KEY_DICT_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of keys held in the in-memory dictionary. */
#define MF_KEY_DICT_MAX_KEYS 384

/**
 * @brief Initialize the key dictionary (load default + user .dic files).
 *
 * Seeds from the firmware's built-in dictionaries, then merges all matching
 * dictionaries from
 * the SD card. This keeps firmware updates and user keys.
 * Also migrates the legacy NVS namespace
 * ("nfc_dict") into the user file the first time it runs after upgrade.
 */
void mf_key_dict_init(void);

/**
 * @brief Get total number of keys currently loaded.
 *
 * @return Total key count.
 */
int mf_key_dict_count(void);

/**
 * @brief Get a key by index.
 *
 * @param idx          Key index (0-based).
 * @param[out] key_out 6-byte key.
 */
void mf_key_dict_get(int idx, uint8_t key_out[6]);

/**
 * @brief Check if a key exists in the dictionary.
 *
 * @param key  6-byte key to check.
 * @return true if the key is already in the dictionary.
 */
bool mf_key_dict_contains(const uint8_t key[6]);

/**
 * @brief Add a key to the dictionary and persist it to mf_classic_user.dic.
 *
 * If the key is already present (built-in or user), this is a no-op and
 * returns HB_NFC_OK.
 *
 * @param key  6-byte key to add.
 * @return
 *   - HB_NFC_OK on success or duplicate
 *   - Error code if dictionary is full or persistence failed
 */
hb_nfc_err_t mf_key_dict_add(const uint8_t key[6]);

#ifdef __cplusplus
}
#endif
#endif
