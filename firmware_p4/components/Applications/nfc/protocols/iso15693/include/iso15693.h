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
 * @file iso15693.h
 * @brief ISO 15693 (NFC-V) reader/writer for ST25R3916.
 *
 * Follows the same patterns as mf_classic.h and mf_ultralight.h.
 * Uses a software stream codec and validates ISO15693 CRC on every response.
 *
 * Protocol summary:
 *  - 13.56 MHz
 *  - Single or double subcarrier (single is more common)
 *  - High data rate: 26.48 kbps TX, 26.48 kbps RX
 *  - Low data rate: 1.65 kbps TX, 6.62 kbps RX
 *  - UID: 8 bytes, LSB first on wire
 *  - CRC-16: poly=0x8408, init=0xFFFF, final_xor=0xFFFF
 *  - Addressed mode: includes UID in every command
 *  - Broadcast mode: no UID (all tags respond)
 */
#ifndef ISO15693_H
#define ISO15693_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Stream-mode exchange; returns the decoded frame including validated CRC. */
int iso15693_transceive(const uint8_t *tx,
                        size_t tx_len,
                        bool with_crc,
                        uint8_t *rx,
                        size_t rx_max,
                        size_t rx_min,
                        int timeout_ms);

/* Request flags (byte 0 of every command) */
#define ISO15693_FLAG_SUBCARRIER (1U << 0) /**< 0=single, 1=double        */
#define ISO15693_FLAG_DATA_RATE  (1U << 1) /**< 0=low, 1=high             */
#define ISO15693_FLAG_INVENTORY  (1U << 2) /**< set in INVENTORY requests  */
#define ISO15693_FLAG_PROTO_EXT  (1U << 3) /**< protocol extension        */
#define ISO15693_FLAG_SELECT     (1U << 4) /**< addressed to selected tag  */
#define ISO15693_FLAG_ADDRESS    (1U << 5) /**< UID included in request    */
#define ISO15693_FLAG_OPTION     (1U << 6) /**< option flag                */
/* Response flags (byte 0 of every response) */
#define ISO15693_RESP_ERROR (1U << 0) /**< error flag; rx[1] = code  */
/* Common flag combinations */
/** @brief INVENTORY, high data rate, single slot (0x26). */
#define ISO15693_FLAGS_INVENTORY \
  (ISO15693_FLAG_INVENTORY | ISO15693_FLAG_DATA_RATE | ISO15693_FLAG_ADDRESS)
/** @brief Addressed + high data rate (0x22). */
#define ISO15693_FLAGS_ADDRESSED (ISO15693_FLAG_ADDRESS | ISO15693_FLAG_DATA_RATE)
/** @brief Broadcast + high data rate, no address (0x02). */
#define ISO15693_FLAGS_UNADDRESSED (ISO15693_FLAG_DATA_RATE)
/* Command codes */
#define ISO15693_CMD_INVENTORY            0x01U
#define ISO15693_CMD_STAY_QUIET           0x02U
#define ISO15693_CMD_READ_SINGLE_BLOCK    0x20U
#define ISO15693_CMD_WRITE_SINGLE_BLOCK   0x21U
#define ISO15693_CMD_LOCK_BLOCK           0x22U
#define ISO15693_CMD_READ_MULTIPLE_BLOCKS 0x23U
#define ISO15693_CMD_WRITE_AFI            0x27U
#define ISO15693_CMD_LOCK_AFI             0x28U
#define ISO15693_CMD_WRITE_DSFID          0x29U
#define ISO15693_CMD_LOCK_DSFID           0x2AU
#define ISO15693_CMD_GET_SYSTEM_INFO      0x2BU
#define ISO15693_CMD_GET_MULTI_BLOCK_SEC  0x2CU
/* Error codes (inside response when RESP_ERROR is set) */
#define ISO15693_ERR_NOT_SUPPORTED     0x01U
#define ISO15693_ERR_NOT_RECOGNIZED    0x02U
#define ISO15693_ERR_BLOCK_UNAVAILABLE 0x10U
#define ISO15693_ERR_BLOCK_LOCKED      0x12U
#define ISO15693_ERR_WRITE_FAILED      0x13U
#define ISO15693_ERR_LOCK_FAILED       0x14U
/* Platform limits */
#define ISO15693_MAX_BLOCK_SIZE 32
#define ISO15693_MAX_BLOCKS     256
#define ISO15693_UID_LEN        8

/**
 * @brief ISO 15693 tag descriptor.
 */
typedef struct {
  uint8_t uid[ISO15693_UID_LEN]; /**< UID LSB first (as on wire)             */
  uint8_t dsfid;                 /**< Data Storage Format Identifier         */
  uint8_t afi;                   /**< Application Family Identifier          */
  uint16_t block_count;          /**< Total number of blocks                 */
  uint8_t block_size;            /**< Bytes per block                        */
  uint8_t ic_ref;                /**< IC reference byte                      */
  bool info_valid;               /**< system info was successfully read       */
} iso15693_tag_t;

/* Poller API */

/**
 * @brief Configure ST25R3916 for ISO 15693 polling.
 *
 * Must be called after st25r3916_core_init() + st25r3916_core_field_on().
 * Sets subcarrier stream mode (0x70), high data rate, single subcarrier.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on communication failure
 */
hb_nfc_err_t iso15693_poller_init(void);

/**
 * @brief Send INVENTORY and collect the first responding tag.
 *
 * Uses broadcast mode (no mask). Populates tag->uid and tag->dsfid.
 *
 * @param[out] tag  Populated with UID and DSFID on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_CARD if no response within timeout
 */
hb_nfc_err_t iso15693_inventory(iso15693_tag_t *tag);

/**
 * @brief Read GET_SYSTEM_INFO for an addressed tag.
 *
 * Fills tag->block_count, tag->block_size, tag->afi, tag->dsfid.
 * tag->uid must be valid (from iso15693_inventory).
 *
 * @param tag  Tag with valid UID; additional fields populated on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_get_system_info(iso15693_tag_t *tag);

/**
 * @brief Inventory multiple tags using STAY_QUIET (best-effort).
 *
 * Each discovered tag is silenced (STAY_QUIET) and the inventory
 * is repeated until no response.
 *
 * @param[out] out     Output array of discovered tags.
 * @param max_tags     Maximum entries in output array.
 * @return Number of tags found (up to max_tags).
 */
int iso15693_inventory_all(iso15693_tag_t *out, size_t max_tags);

/**
 * @brief Read a single block (addressed mode).
 *
 * @param tag       Tag with valid UID.
 * @param block     Block number (0-based).
 * @param[out] data      Output buffer.
 * @param data_max  Output buffer capacity.
 * @param[out] data_len  Set to number of bytes written on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_read_single_block(
    const iso15693_tag_t *tag, uint8_t block, uint8_t *data, size_t data_max, size_t *data_len);

/**
 * @brief Write a single block (addressed mode).
 *
 * @param tag       Tag with valid UID.
 * @param block     Block number (0-based).
 * @param data      Data to write (must match block_size).
 * @param data_len  Number of bytes to write.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_write_single_block(const iso15693_tag_t *tag,
                                         uint8_t block,
                                         const uint8_t *data,
                                         size_t data_len);

/**
 * @brief Lock a single block (addressed mode).
 *
 * @param tag    Tag with valid UID.
 * @param block  Block number to lock.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_lock_block(const iso15693_tag_t *tag, uint8_t block);

/**
 * @brief Read multiple consecutive blocks (addressed mode).
 *
 * @param tag          Tag with valid UID.
 * @param first_block  Starting block number.
 * @param count        Number of blocks to read.
 * @param[out] out_buf Output buffer (count * block_size bytes needed).
 * @param out_max      Output buffer capacity.
 * @param[out] out_len Set to bytes written on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_read_multiple_blocks(const iso15693_tag_t *tag,
                                           uint8_t first_block,
                                           uint8_t count,
                                           uint8_t *out_buf,
                                           size_t out_max,
                                           size_t *out_len);

/**
 * @brief Write AFI (addressed mode).
 *
 * @param tag  Tag with valid UID.
 * @param afi  Application Family Identifier value to write.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_write_afi(const iso15693_tag_t *tag, uint8_t afi);

/**
 * @brief Lock AFI (addressed mode).
 *
 * @param tag  Tag with valid UID.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_lock_afi(const iso15693_tag_t *tag);

/**
 * @brief Write DSFID (addressed mode).
 *
 * @param tag    Tag with valid UID.
 * @param dsfid  Data Storage Format Identifier value to write.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_write_dsfid(const iso15693_tag_t *tag, uint8_t dsfid);

/**
 * @brief Lock DSFID (addressed mode).
 *
 * @param tag  Tag with valid UID.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_lock_dsfid(const iso15693_tag_t *tag);

/**
 * @brief Get multiple block security status (addressed mode).
 *
 * @param tag          Tag with valid UID.
 * @param first_block  Starting block number.
 * @param count        Number of blocks to query.
 * @param[out] out_buf Output buffer (count bytes).
 * @param out_max      Output buffer capacity.
 * @param[out] out_len Set to bytes written on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso15693_get_multi_block_sec(const iso15693_tag_t *tag,
                                          uint8_t first_block,
                                          uint8_t count,
                                          uint8_t *out_buf,
                                          size_t out_max,
                                          size_t *out_len);

/**
 * @brief Full tag dump: inventory -> system info -> all blocks.
 *
 * Prints everything via ESP_LOGI. No output parameters.
 */
void iso15693_dump_card(void);

/* Utility */

/**
 * @brief Compute ISO 15693 CRC-16 (poly=0x8408, init=0xFFFF, ~result).
 *
 * @param data  Input data buffer.
 * @param len   Number of bytes.
 * @return Computed CRC-16 value.
 */
uint16_t iso15693_crc16(const uint8_t *data, size_t len);

/**
 * @brief Verify CRC appended to a response buffer (last 2 bytes).
 *
 * @param data  Response buffer including trailing CRC.
 * @param len   Total length including CRC bytes.
 * @return true if CRC matches, false otherwise.
 */
bool iso15693_check_crc(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ISO15693_H */
