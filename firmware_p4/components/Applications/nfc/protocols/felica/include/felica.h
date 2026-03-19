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

/* Constants */
#define FELICA_IDM_LEN          8
#define FELICA_PMM_LEN          8
#define FELICA_BLOCK_SIZE       16
#define FELICA_MAX_BLOCKS       32

#define FELICA_CMD_SENSF_REQ    0x04U
#define FELICA_CMD_SENSF_RES    0x05U
#define FELICA_CMD_READ_WO_ENC  0x06U
#define FELICA_CMD_READ_RESP    0x07U
#define FELICA_CMD_WRITE_WO_ENC 0x08U
#define FELICA_CMD_WRITE_RESP   0x09U

#define FELICA_SC_COMMON        0x88B4U  /**< wildcard system code             */
#define FELICA_SC_NDEF          0x0003U  /**< NDEF system code                 */

#define FELICA_RCF_NONE         0x00U    /**< no system code in response       */
#define FELICA_RCF_SYSTEM_CODE  0x01U    /**< include system code in response  */

/* Constants */
typedef struct {
    uint8_t idm[FELICA_IDM_LEN];  /**< Manufacturer ID (8 bytes)           */
    uint8_t pmm[FELICA_PMM_LEN];  /**< Manufacturer Parameters (8 bytes)   */
    uint8_t rd[2];                 /**< Request Data / System Code          */
    bool    rd_valid;              /**< rd was included in SENSF_RES        */
} felica_tag_t;

/* Poller API */

/**
 * @brief Configure ST25R3916 for FeliCa polling (212 kbps).
 */
hb_nfc_err_t felica_poller_init(void);

/**
 * @brief Send SENSF_REQ and collect the first responding tag.
 *
 * @param system_code  System code filter (FELICA_SC_COMMON = all tags).
 * @param tag          Output: populated with IDm, PMm, RD.
 */
hb_nfc_err_t felica_sensf_req(uint16_t system_code, felica_tag_t* tag);

/**
 * @brief Send SENSF_REQ with time slots (best-effort).
 *
 * @param time_slots 0-15 (0 = 1 slot, 15 = 16 slots).
 */
hb_nfc_err_t felica_sensf_req_slots(uint16_t system_code, uint8_t time_slots, felica_tag_t* tag);

/**
 * @brief Poll multiple tags by repeating SENSF_REQ (best-effort).
 *
 * Returns number of unique tags found (by IDm).
 */
int felica_polling(uint16_t system_code, uint8_t time_slots,
                     felica_tag_t* out, size_t max_tags);

/**
 * @brief Read blocks using Read Without Encryption (command 0x06).
 *
 * @param tag           Tag with valid IDm.
 * @param service_code  Service code (e.g. 0x000B for read-only, 0x0009 for R/W).
 * @param first_block   Starting block number (0-based).
 * @param count         Number of blocks to read (max 4 per command).
 * @param out           Output buffer (count x 16 bytes).
 */
hb_nfc_err_t felica_read_blocks(const felica_tag_t* tag,
                                  uint16_t            service_code,
                                  uint8_t             first_block,
                                  uint8_t             count,
                                  uint8_t*            out);

/**
 * @brief Write blocks using Write Without Encryption (command 0x08).
 *
 * @param tag           Tag with valid IDm.
 * @param service_code  Service code (e.g. 0x0009 for R/W).
 * @param first_block   Starting block number (0-based).
 * @param count         Number of blocks to write (max 1 per command).
 * @param data          Data to write (count x 16 bytes).
 */
hb_nfc_err_t felica_write_blocks(const felica_tag_t* tag,
                                   uint16_t            service_code,
                                   uint8_t             first_block,
                                   uint8_t             count,
                                   const uint8_t*      data);

/**
 * @brief Full tag dump: SENSF_REQ + read blocks.
 *
 * Tries common system code, reads up to 32 blocks.
 */
void felica_dump_card(void);

#endif /* FELICA_H */
