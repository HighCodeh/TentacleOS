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
 * @file felica.h
 * @brief FeliCa (NFC-F / ISO 18092) reader/writer for ST25R3916.
 *
 * Protocol summary:
 *  - 13.56 MHz, 212 or 424 kbps
 *  - IDm: 8-byte manufacturer ID (used as tag address)
 *  - PMm: 8-byte manufacturer parameters
 *  - System codes: 0x88B4 (common), 0x0003 (NDEF), 0x8008 (Suica/transit)
 *  - Commands: SENSF_REQ/RES (poll), Read/Write Without Encryption
 */
#ifndef FELICA_H
#define FELICA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */
#define FELICA_IDM_LEN    8
#define FELICA_PMM_LEN    8
#define FELICA_BLOCK_SIZE 16
#define FELICA_MAX_BLOCKS 32

#define FELICA_CMD_SENSF_REQ    0x04U
#define FELICA_CMD_SENSF_RES    0x05U
#define FELICA_CMD_READ_WO_ENC  0x06U
#define FELICA_CMD_READ_RESP    0x07U
#define FELICA_CMD_WRITE_WO_ENC 0x08U
#define FELICA_CMD_WRITE_RESP   0x09U

#define FELICA_SC_COMMON 0x88B4U /**< wildcard system code             */
#define FELICA_SC_NDEF   0x0003U /**< NDEF system code                 */

#define FELICA_RCF_NONE        0x00U /**< no system code in response       */
#define FELICA_RCF_SYSTEM_CODE 0x01U /**< include system code in response  */

/* RF Configuration */
#define FELICA_AM_MOD_PERCENT 10 /**< AM modulation %, ST25R3916 spec */

/* SENSF_REQ Protocol (Polling) */
#define FELICA_SENSF_REQ_LENGTH      0x07U /**< Total packet byte count */
#define FELICA_SENSF_TIME_SLOT_MASK  0x0FU /**< 4-bit time slot field mask */
#define FELICA_SENSF_RES_MIN_LEN     17    /**< Minimum valid response length */
#define FELICA_SENSF_RES_WITH_RD_LEN 20    /**< Min length to include RD field */
#define FELICA_SENSF_TIMEOUT_MS      20    /**< RF transceive timeout */
#define FELICA_SENSF_RES_IDM_OFFSET  2     /**< IDm position in SENSF_RES */
#define FELICA_SENSF_RES_PMM_OFFSET  10    /**< PMm position in SENSF_RES */
#define FELICA_SENSF_RES_RD0_OFFSET  18    /**< RD[0] position in SENSF_RES */
#define FELICA_SENSF_RES_RD1_OFFSET  19    /**< RD[1] position in SENSF_RES */
#define FELICA_SENSF_RES_BUFFER_SIZE 32    /**< RX buffer for SENSF_RES */

/* Read Without Encryption Protocol */
#define FELICA_READ_MAX_BLOCKS      4     /**< Max blocks per READ command */
#define FELICA_READ_CMD_BUFFER_SIZE 32    /**< TX buffer for READ command */
#define FELICA_READ_RES_BUFFER_SIZE 256   /**< RX buffer for READ response */
#define FELICA_READ_TIMEOUT_MS      30    /**< RF transceive timeout */
#define FELICA_READ_RES_MIN_LEN     12    /**< Minimum valid response length */
#define FELICA_READ_RES_IDM_OFFSET  2     /**< IDm position in READ_RESP */
#define FELICA_READ_RES_STATUS1_POS 10    /**< Status1 position in READ_RESP */
#define FELICA_READ_RES_STATUS2_POS 11    /**< Status2 position in READ_RESP */
#define FELICA_READ_RES_COUNT_POS   12    /**< Block count position in READ_RESP */
#define FELICA_READ_RES_DATA_POS    13    /**< Block data start position in READ_RESP */
#define FELICA_BLOCK_DESC_BYTE      0x80U /**< Block access descriptor byte */
#define FELICA_SERVICE_COUNT_OFFSET 10    /**< Service code count offset in cmd */
#define FELICA_CMD_SERVICES_BASE    11    /**< Services data start offset */
#define FELICA_SC_LIST_ENTRY_SIZE   2     /**< Each service code is 2 bytes */

/* Write Without Encryption Protocol */
#define FELICA_WRITE_CMD_BUFFER_SIZE 64 /**< TX buffer for WRITE command */
#define FELICA_WRITE_RES_BUFFER_SIZE 32 /**< RX buffer for WRITE response */
#define FELICA_WRITE_TIMEOUT_MS      50 /**< RF transceive timeout (longer) */
#define FELICA_WRITE_RES_MIN_LEN     11 /**< Minimum valid response length */
#define FELICA_WRITE_RES_IDM_OFFSET  2  /**< IDm position in WRITE_RESP */
#define FELICA_WRITE_RES_STATUS1_POS 10 /**< Status1 position in WRITE_RESP */
#define FELICA_WRITE_RES_STATUS2_POS 11 /**< Status2 position in WRITE_RESP */
#define FELICA_FRAME_MIN_HDR_LEN     2  /**< Minimum frame header (len + cmd) */

/* Dump Formatting */
#define FELICA_IDM_STR_BUFFER_SIZE    (2 * FELICA_IDM_LEN + 1) /**< IDm hex string buffer */
#define FELICA_HEX_DUMP_BUFFER_SIZE   (3 * FELICA_BLOCK_SIZE)  /**< Hex dump line buffer */
#define FELICA_ASCII_DUMP_BUFFER_SIZE (FELICA_BLOCK_SIZE + 1)  /**< ASCII dump line buffer */

#define ASCII_PRINTABLE_START 0x20 /**< ' ' - first printable ASCII */
#define ASCII_PRINTABLE_END   0x7E /**< '~' - last printable ASCII */

/* Service Codes */
#define FELICA_SERVICE_COMMON_RO 0x000BU /**< Common Area Read-Only */
#define FELICA_SERVICE_COMMON_RW 0x0009U /**< Common Area Read-Write */
#define FELICA_SERVICE_NDEF      0x100BU /**< NDEF service */

/**
 * @brief FeliCa tag descriptor from SENSF_RES.
 */
typedef struct {
  uint8_t idm[FELICA_IDM_LEN]; /**< Manufacturer ID (8 bytes)           */
  uint8_t pmm[FELICA_PMM_LEN]; /**< Manufacturer Parameters (8 bytes)   */
  uint8_t rd[2];               /**< Request Data / System Code          */
  bool rd_valid;               /**< rd was included in SENSF_RES        */
} felica_tag_t;

/* Poller API */

/**
 * @brief Configure ST25R3916 for FeliCa polling (212 kbps).
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on communication failure
 */
hb_nfc_err_t felica_poller_init(void);

/**
 * @brief Send SENSF_REQ and collect the first responding tag.
 *
 * @param system_code  System code filter (FELICA_SC_COMMON = all tags).
 * @param[out] tag     Populated with IDm, PMm, RD on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_CARD if no tag responds
 */
hb_nfc_err_t felica_sensf_req(uint16_t system_code, felica_tag_t *tag);

/**
 * @brief Send SENSF_REQ with time slots (best-effort).
 *
 * @param system_code  System code filter.
 * @param time_slots   0-15 (0 = 1 slot, 15 = 16 slots).
 * @param[out] tag     Populated on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_CARD if no tag responds
 */
hb_nfc_err_t felica_sensf_req_slots(uint16_t system_code, uint8_t time_slots, felica_tag_t *tag);

/**
 * @brief Poll multiple tags by repeating SENSF_REQ (best-effort).
 *
 * @param system_code  System code filter.
 * @param time_slots   Number of time slots per poll.
 * @param[out] out     Output array of unique tags (by IDm).
 * @param max_tags     Maximum entries in output array.
 * @return Number of unique tags found.
 */
int felica_polling(uint16_t system_code, uint8_t time_slots, felica_tag_t *out, size_t max_tags);

/**
 * @brief Read blocks using Read Without Encryption (command 0x06).
 *
 * @param tag           Tag with valid IDm.
 * @param service_code  Service code (e.g. 0x000B for read-only, 0x0009 for R/W).
 * @param first_block   Starting block number (0-based).
 * @param count         Number of blocks to read (max 4 per command).
 * @param[out] out      Output buffer (count x 16 bytes).
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t felica_read_blocks(const felica_tag_t *tag,
                                uint16_t service_code,
                                uint8_t first_block,
                                uint8_t count,
                                uint8_t *out);

/**
 * @brief Write blocks using Write Without Encryption (command 0x08).
 *
 * @param tag           Tag with valid IDm.
 * @param service_code  Service code (e.g. 0x0009 for R/W).
 * @param first_block   Starting block number (0-based).
 * @param count         Number of blocks to write (max 1 per command).
 * @param data          Data to write (count x 16 bytes).
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t felica_write_blocks(const felica_tag_t *tag,
                                 uint16_t service_code,
                                 uint8_t first_block,
                                 uint8_t count,
                                 const uint8_t *data);

/**
 * @brief Full tag dump: SENSF_REQ + read blocks.
 *
 * Tries common system code, reads up to 32 blocks.
 */
void felica_dump_card(void);

#ifdef __cplusplus
}
#endif

#endif /* FELICA_H */
