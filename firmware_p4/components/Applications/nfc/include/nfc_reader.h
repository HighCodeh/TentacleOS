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

#ifndef NFC_READER_H
#define NFC_READER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "highboy_nfc_types.h"
#include "highboy_nfc_compat.h"
#include "mf_classic_emu.h"

/** @brief Global emulation card data populated by mf_classic_read_full(). */
extern mfc_emu_card_data_t s_emu_card;

/** @brief true when s_emu_card contains valid data from a completed read. */
extern bool s_is_emu_data_ready;

/**
 * @brief Read all sectors of a MIFARE Classic card.
 *
 * @param card Card data with UID/SAK/ATQA.
 */
void mf_classic_read_full(nfc_iso14443a_data_t *card);

/**
 * @brief Write all sectors from s_emu_card back to a target card.
 *
 * Requires s_is_emu_data_ready == true (i.e. mf_classic_read_full completed).
 * Uses the keys stored in s_emu_card for authentication.
 * Data blocks only - trailer write is skipped unless is_write_trailers is true.
 * WARNING: writing trailers with wrong access bits can permanently lock sectors.
 *
 * @param target            Target card data.
 * @param is_write_trailers Whether to write trailer blocks.
 */
void mf_classic_write_all(nfc_iso14443a_data_t *target, bool is_write_trailers);

/**
 * @brief Probe and dump MIFARE Plus card.
 *
 * @param card Card data.
 */
void mfp_probe_and_dump(nfc_iso14443a_data_t *card);

/**
 * @brief Dump MIFARE Ultralight card.
 *
 * @param card Card data.
 */
void mful_dump_card(nfc_iso14443a_data_t *card);

/**
 * @brief Dump NDEF data from a T4T card.
 *
 * @param card Card data.
 */
void t4t_dump_ndef(nfc_iso14443a_data_t *card);

#ifdef __cplusplus
}
#endif

#endif /* NFC_READER_H */
