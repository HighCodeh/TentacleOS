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
 *
 * Why this is the easiest emulation on ST25R3916:
 *
 *  The chip has a dedicated PT Memory F (SPI command 0xA8) that stores
 *  IDm + PMm + System Code. When a reader sends SENSF_REQ, the chip
 *  replies automatically with SENSF_RES — no software needed.
 *  This is the exact same mechanism as PT Memory A for NFC-A (REQA/ATQA).
 *
 *  Software only handles Read/Write commands that arrive after SENSF.
 *
 * State machine (identical to T2T / MFC):
 *   SLEEP ──[field detected]──► SENSE ──[SENSF received, HW responds]──► ACTIVE
 *   ACTIVE ──[Read/Write cmd]──► respond ──► ACTIVE
 *   ACTIVE ──[field lost]──► SLEEP
 *
 * PT Memory F layout (19 bytes, written with SPI cmd 0xA8):
 *   [0..1]   NFCF_SC (system code, big endian)
 *   [2]      SENSF_RES code (0x01)
 *   [3..10]  NFCID2 / IDm (8 bytes)
 *   [11..18] PMm (8 bytes)
 *  The chip appends RD when requested (RCF) using NFCF_SC.
 */
#ifndef FELICA_EMU_H
#define FELICA_EMU_H

#include <stdint.h>
#include <stdbool.h>
#include "felica.h"
#include "highboy_nfc_error.h"

/* ─── Card profile ─────────────────────────────────────────────────────────── */
#define FELICA_EMU_MAX_BLOCKS   32
#define FELICA_EMU_MEM_SIZE     (FELICA_EMU_MAX_BLOCKS * FELICA_BLOCK_SIZE)

typedef struct {
    uint8_t  idm[FELICA_IDM_LEN];
    uint8_t  pmm[FELICA_PMM_LEN];
    uint16_t system_code;
    uint16_t service_code;            /**< service code served for R/W       */
    uint8_t  block_count;             /**< number of blocks available        */
    uint8_t  mem[FELICA_EMU_MEM_SIZE];
} felica_emu_card_t;

/* ─── API ──────────────────────────────────────────────────────────────────── */

/** Load card profile (call before configure_target). */
hb_nfc_err_t felica_emu_init(const felica_emu_card_t* card);

/** Populate from a prior poller read. raw_mem = block_count × 16 bytes. */
void felica_emu_card_from_tag(felica_emu_card_t*  card,
                               const felica_tag_t* tag,
                               uint16_t            service_code,
                               uint8_t             block_count,
                               const uint8_t*      raw_mem);

/** Default blank card (IDm/PMm provided, 16 zeroed blocks). */
void felica_emu_card_default(felica_emu_card_t*   card,
                              const uint8_t idm[FELICA_IDM_LEN],
                              const uint8_t pmm[FELICA_PMM_LEN]);

/** Configure ST25R3916 as NFC-F target and write PT Memory F. */
hb_nfc_err_t felica_emu_configure_target(void);

/** Activate field detector, enter SLEEP. */
hb_nfc_err_t felica_emu_start(void);

/** Power down. */
void felica_emu_stop(void);

/** Call in tight loop (2 ms task). Non-blocking. */
void felica_emu_run_step(void);

#endif /* FELICA_EMU_H */