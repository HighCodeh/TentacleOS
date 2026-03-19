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
 * @file nfc_reader.h
 * @brief MIFARE Classic/Ultralight read helpers.
 */
#ifndef NFC_READER_H
#define NFC_READER_H

#include "highboy_nfc_types.h"
#include "mf_classic_emu.h"

extern mfc_emu_card_data_t s_emu_card;
extern bool s_emu_data_ready;

void mf_classic_read_full(nfc_iso14443a_data_t* card);

/**
 * Write all sectors from s_emu_card back to a target card.
 * Requires s_emu_data_ready == true (i.e. mf_classic_read_full completed).
 * Uses the keys stored in s_emu_card for authentication.
 * Data blocks only - trailer write is skipped unless write_trailers is true.
 * WARNING: writing trailers with wrong access bits can permanently lock sectors.
 */
void mf_classic_write_all(nfc_iso14443a_data_t* target, bool write_trailers);

void mfp_probe_and_dump(nfc_iso14443a_data_t* card);
void mful_dump_card(nfc_iso14443a_data_t* card);
void t4t_dump_ndef(nfc_iso14443a_data_t* card);

#endif
