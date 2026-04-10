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

#ifndef NFC_STORE_H
#define NFC_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#define NFC_STORE_MAX_ENTRIES 16
#define NFC_STORE_NAME_MAX    32
#define NFC_STORE_PAYLOAD_MAX 2048

/**
 * @brief Card parameters for nfc_store_save().
 */
typedef struct {
  const char *name;           /**< @brief Entry name. */
  hb_nfc_protocol_t protocol; /**< @brief NFC protocol type. */
  const uint8_t *uid;         /**< @brief Card UID bytes. */
  uint8_t uid_len;            /**< @brief UID length in bytes. */
  const uint8_t atqa[2];      /**< @brief ATQA bytes. */
  uint8_t sak;                /**< @brief SAK byte. */
  const uint8_t *payload;     /**< @brief Protocol-specific payload. */
  size_t payload_len;         /**< @brief Payload length in bytes. */
} nfc_store_card_t;

/**
 * @brief Input parameters for nfc_store_pack_mful().
 */
typedef struct {
  const uint8_t *version; /**< @brief Version bytes (8 bytes). */
  const uint8_t *pages;   /**< @brief Page data. */
  uint16_t page_count;    /**< @brief Number of pages. */
} nfc_mful_pack_t;

/**
 * @brief Output parameters for nfc_store_unpack_mful().
 */
typedef struct {
  uint8_t *version;     /**< @brief Version bytes output (8 bytes). */
  uint8_t *pages;       /**< @brief Page data output buffer. */
  uint16_t *page_count; /**< @brief Number of pages unpacked. */
  uint16_t pages_max;   /**< @brief Maximum pages output buffer size. */
} nfc_mful_unpack_t;

/**
 * @brief Full store entry with payload data.
 */
typedef struct {
  char name[NFC_STORE_NAME_MAX];          /**< @brief Entry name. */
  hb_nfc_protocol_t protocol;             /**< @brief NFC protocol type. */
  uint8_t uid[10];                        /**< @brief Card UID bytes. */
  uint8_t uid_len;                        /**< @brief UID length in bytes. */
  uint8_t atqa[2];                        /**< @brief ATQA bytes. */
  uint8_t sak;                            /**< @brief SAK byte. */
  size_t payload_len;                     /**< @brief Payload length in bytes. */
  uint8_t payload[NFC_STORE_PAYLOAD_MAX]; /**< @brief Protocol-specific payload. */
} nfc_store_entry_t;

/**
 * @brief Light info struct (no payload, for listing).
 */
typedef struct {
  char name[NFC_STORE_NAME_MAX]; /**< @brief Entry name. */
  hb_nfc_protocol_t protocol;    /**< @brief NFC protocol type. */
  uint8_t uid[10];               /**< @brief Card UID bytes. */
  uint8_t uid_len;               /**< @brief UID length in bytes. */
} nfc_store_info_t;

/**
 * @brief Initialize NFC store.
 */
void nfc_store_init(void);

/**
 * @brief Get number of stored entries.
 *
 * @return Entry count.
 */
int nfc_store_count(void);

/**
 * @brief Save an NFC card entry to storage.
 *
 * @param card  Card parameters to save.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if card, name, or uid is NULL
 *   - HB_NFC_ERR_NO_MEM if storage is full
 *   - HB_NFC_ERR_INTERNAL on NVS write failure
 */
hb_nfc_err_t nfc_store_save(const nfc_store_card_t *card);

/**
 * @brief Load a full entry by index.
 *
 * @param      index Entry index.
 * @param[out] out   Loaded entry data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if out is NULL or index is out of range
 *   - HB_NFC_ERR_NOT_FOUND if entry does not exist
 */
hb_nfc_err_t nfc_store_load(int index, nfc_store_entry_t *out);

/**
 * @brief Get lightweight info for an entry.
 *
 * @param      index Entry index.
 * @param[out] out   Lightweight entry info.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if out is NULL or index is out of range
 *   - HB_NFC_ERR_NOT_FOUND if entry does not exist
 */
hb_nfc_err_t nfc_store_get_info(int index, nfc_store_info_t *out);

/**
 * @brief List all stored entry infos.
 *
 * @param[out] infos Output array.
 * @param      max   Maximum entries to fill.
 * @return Number of entries listed.
 */
int nfc_store_list(nfc_store_info_t *infos, int max);

/**
 * @brief Delete an entry by index.
 *
 * @param index Entry index.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if index is out of range
 *   - HB_NFC_ERR_NOT_FOUND if entry does not exist
 */
hb_nfc_err_t nfc_store_delete(int index);

/**
 * @brief Find an entry by UID.
 *
 * @param      uid       UID bytes.
 * @param      uid_len   UID length.
 * @param[out] index_out Matching entry index.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if uid or index_out is NULL
 *   - HB_NFC_ERR_NOT_FOUND if no matching entry exists
 */
hb_nfc_err_t nfc_store_find_by_uid(const uint8_t *uid, uint8_t uid_len, int *index_out);

/**
 * @brief Build NTAG/Ultralight payload from version + pages.
 *
 * @param      in      Input pack parameters (version, pages, page_count).
 * @param[out] out     Output buffer for packed payload.
 * @param      out_max Output buffer size.
 * @return Packed payload size in bytes, or 0 on failure.
 */
size_t nfc_store_pack_mful(const nfc_mful_pack_t *in, uint8_t *out, size_t out_max);

/**
 * @brief Unpack NTAG/Ultralight payload.
 *
 * @param      payload     Packed payload.
 * @param      payload_len Payload length.
 * @param[out] out         Output parameters (version, pages, page_count, pages_max).
 * @return
 *   - true on success
 *   - false if payload is malformed or buffer too small
 */
bool nfc_store_unpack_mful(const uint8_t *payload, size_t payload_len, nfc_mful_unpack_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NFC_STORE_H */
