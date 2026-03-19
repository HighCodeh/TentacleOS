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
 * @file iso15693_emu.h
 * @brief ISO 15693 (NFC-V) tag emulation for ST25R3916.
 *
 * Architecture mirrors t2t_emu exactly:
 *  - iso15693_emu_init()          -> set tag memory + UID
 *  - iso15693_emu_configure_target() -> configure ST25R3916 as NFC-V target
 *  - iso15693_emu_start()         -> enter SLEEP (field-detector active)
 *  - iso15693_emu_run_step()      -> call in tight loop (no blocking)
 *  - iso15693_emu_stop()          -> power down
 *
 * State machine (identical to T2T):
 *
 *   SLEEP --[field detected]--> SENSE --[INVENTORY received]--> ACTIVE
 *   ACTIVE --[field lost]--> SLEEP
 *   ACTIVE --[HALT / stay quiet]--> SLEEP
 *
 * Emulation limitations on ST25R3916 in NFC-V mode:
 *  - No hardware anti-collision (unlike NFC-A): the chip does NOT
 *    automatically respond to INVENTORY.  Software must detect the
 *    INVENTORY command in RX and build the response manually.
 *  - Single-tag environment assumed (no slot-based collision avoidance).
 *  - Only high-data-rate / single-subcarrier supported.
 *
 * Supported commands (emulated in software):
 *  -  *  - INVENTORY        -> reply [resp_flags][DSFID][UID]
 *  -  *  - GET_SYSTEM_INFO  -> reply with block_count, block_size, DSFID, AFI
 *  - READ_SINGLE_BLOCK
 *  - WRITE_SINGLE_BLOCK
 *  - READ_MULTIPLE_BLOCKS
 *  - LOCK_BLOCK
 *  - WRITE_AFI / LOCK_AFI
 *  - WRITE_DSFID / LOCK_DSFID
 *  - GET_MULTI_BLOCK_SEC
 *  -  *  - STAY_QUIET       -> go to SLEEP (stop responding until power cycle)
 */
#ifndef ISO15693_EMU_H
#define ISO15693_EMU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "iso15693.h"
#include "highboy_nfc_error.h"
/* Memory limits */
#define ISO15693_EMU_MAX_BLOCKS     256
#define ISO15693_EMU_MAX_BLOCK_SIZE 4       /**< bytes per block (default)  */
#define ISO15693_EMU_MEM_SIZE       (ISO15693_EMU_MAX_BLOCKS * ISO15693_EMU_MAX_BLOCK_SIZE)
/* Card profile */
typedef struct {
    uint8_t  uid[ISO15693_UID_LEN];  /**< 8-byte UID, LSB first              */
    uint8_t  dsfid;                  /**< Data Storage Format Identifier      */
    uint8_t  afi;                    /**< Application Family Identifier       */
    uint8_t  ic_ref;                 /**< IC reference byte                   */
    uint16_t block_count;            /**< number of blocks  (<= MAX_BLOCKS)   */
    uint8_t  block_size;             /**< bytes per block   (<= MAX_BLOCK_SIZE)*/
    uint8_t  mem[ISO15693_EMU_MEM_SIZE]; /**< tag memory flat array           */
} iso15693_emu_card_t;
/* API */
/**
 * @brief Initialise emulator with a card profile.
 *
 * Must be called before configure_target.
 * @param card  Card data (copied internally).
 */
hb_nfc_err_t iso15693_emu_init(const iso15693_emu_card_t* card);

/**
 * @brief Populate a card from a previously read iso15693_tag_t + raw dump.
 *
 * Convenience helper: fills uid/dsfid/afi/block_count/block_size from tag,
 *  * copies raw_mem (block_count x block_size bytes) into card->mem.
 */
void iso15693_emu_card_from_tag(iso15693_emu_card_t*   card,
                                 const iso15693_tag_t*  tag,
                                 const uint8_t*         raw_mem);

/**
 * @brief Create a minimal writable NFC-V tag for testing.
 *
 * UID is fixed to the values passed in.
 *  * Initialises 8 blocks x 4 bytes = 32 bytes of zeroed memory.
 */
void iso15693_emu_card_default(iso15693_emu_card_t* card,
                                const uint8_t uid[ISO15693_UID_LEN]);

/**
 * @brief Configure ST25R3916 as NFC-V target.
 *
 *  * Resets chip -> oscillator -> registers -> starts field detector.
 * Does NOT activate the field (we are a target, not an initiator).
 */
hb_nfc_err_t iso15693_emu_configure_target(void);

/**
 * @brief Activate emulation (enter SLEEP state, field detector on).
 *
 * After this call, iso15693_emu_run_step() must be called in a tight loop.
 */
hb_nfc_err_t iso15693_emu_start(void);

/**
 * @brief Power down the emulator.
 */
void iso15693_emu_stop(void);

/**
 * @brief  * @brief Single polling step - call as fast as possible in a FreeRTOS task.
 *
 * Non-blocking.  Checks IRQs, transitions state machine, handles commands.
 */
void iso15693_emu_run_step(void);

/**
 * @brief Return pointer to internal tag memory (for live inspection).
 */
uint8_t* iso15693_emu_get_mem(void);

#endif /* ISO15693_EMU_H */
