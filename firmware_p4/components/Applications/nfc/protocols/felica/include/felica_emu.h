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
 * @file felica_emu.h
 * @brief FeliCa (NFC-F) tag emulation for ST25R3916.
 */
#ifndef FELICA_EMU_H
#define FELICA_EMU_H

#include <stdint.h>
#include <stdbool.h>
#include "felica.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FELICA_EMU_MAX_BLOCKS 32
#define FELICA_EMU_MEM_SIZE   (FELICA_EMU_MAX_BLOCKS * FELICA_BLOCK_SIZE)

/* Frame parsing offsets - shared with protocol */
#define FELICA_EMU_FRAME_IDM_OFFSET    2  /**< IDm starts at byte 2 */
#define FELICA_EMU_READ_MIN_FRAME_LEN  16 /**< Minimum READ frame size */
#define FELICA_EMU_WRITE_MIN_FRAME_LEN 32 /**< Minimum WRITE frame size */
#define FELICA_EMU_FRAME_HDR_LEN       2  /**< Frame header (len + cmd) */

/* PT Memory structure offsets */
#define FELICA_EMU_PT_SC_OFFSET   0  /**< System code in PT mem */
#define FELICA_EMU_PT_DESC_OFFSET 2  /**< Descriptor byte in PT mem */
#define FELICA_EMU_PT_IDM_OFFSET  3  /**< IDm starts at PT[3] */
#define FELICA_EMU_PT_PMM_OFFSET  11 /**< PMm starts at PT[11] */

/* Frame parsing */
#define FELICA_EMU_SC_COUNT_OFFSET      10 /**< Service code count byte */
#define FELICA_EMU_SC_BLOCK_BASE_OFFSET 11 /**< Service codes start here */
#define FELICA_EMU_SC_ENTRY_SIZE        2  /**< 2 bytes per service code */

/* Response frame building */
#define FELICA_EMU_READ_RES_HDR_SIZE 4   /**< Response header (len+cmd+idm_start) */
#define FELICA_EMU_WRITE_RES_SIZE    12U /**< Fixed write response size */
#define FELICA_EMU_RES_IDM_OFFSET    2   /**< IDm in response frame */
#define FELICA_EMU_RES_STATUS1_POS   10  /**< Status1 in response */
#define FELICA_EMU_RES_STATUS2_POS   11  /**< Status2 in response */

/* Register/Hardware constants */
#define FELICA_EMU_BYTE_SHIFT_HI   8     /**< High byte extraction shift */
#define FELICA_EMU_BYTE_MASK_LOW   0xFFU /**< Low byte extraction mask */
#define FELICA_EMU_BYTE_MASK_SC_LO 0xFFU /**< SC low byte mask */
#define FELICA_EMU_PT_DESC_VALUE   0x01U /**< PT descriptor value */

/**
 * @brief FeliCa emulated card profile.
 */
typedef struct {
  uint8_t idm[FELICA_IDM_LEN];      /**< Manufacturer ID (8 bytes). */
  uint8_t pmm[FELICA_PMM_LEN];      /**< Manufacturer Parameters (8 bytes). */
  uint16_t system_code;             /**< System code to advertise. */
  uint16_t service_code;            /**< Service code served for R/W. */
  uint8_t block_count;              /**< Number of blocks available. */
  uint8_t mem[FELICA_EMU_MEM_SIZE]; /**< Tag memory flat array. */
} felica_emu_card_t;

/**
 * @brief Load card profile (call before configure_target).
 *
 * @param card  Card data (copied internally).
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if card is NULL
 */
hb_nfc_err_t felica_emu_init(const felica_emu_card_t *card);

/**
 * @brief Populate emulated card from a prior poller read.
 *
 * @param[out] card     Card profile to populate.
 * @param tag           Tag descriptor from SENSF_RES.
 * @param service_code  Service code to serve.
 * @param block_count   Number of blocks in raw_mem.
 * @param raw_mem       Raw block data (block_count x 16 bytes).
 */
void felica_emu_card_from_tag(felica_emu_card_t *card,
                              const felica_tag_t *tag,
                              uint16_t service_code,
                              uint8_t block_count,
                              const uint8_t *raw_mem);

/**
 * @brief Create a default blank card (IDm/PMm provided, 16 zeroed blocks).
 *
 * @param[out] card  Card profile to populate.
 * @param idm        Manufacturer ID (8 bytes).
 * @param pmm        Manufacturer Parameters (8 bytes).
 */
void felica_emu_card_default(felica_emu_card_t *card,
                             const uint8_t idm[FELICA_IDM_LEN],
                             const uint8_t pmm[FELICA_PMM_LEN]);

/**
 * @brief Configure ST25R3916 as NFC-F target and write PT Memory F.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on communication failure
 */
hb_nfc_err_t felica_emu_configure_target(void);

/**
 * @brief Activate field detector and enter SLEEP state.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on communication failure
 */
hb_nfc_err_t felica_emu_start(void);

/**
 * @brief Power down the emulator.
 */
void felica_emu_stop(void);

/**
 * @brief Run one emulation step (call in tight loop, 2 ms task). Non-blocking.
 */
void felica_emu_run_step(void);

#ifdef __cplusplus
}
#endif

#endif /* FELICA_EMU_H */
