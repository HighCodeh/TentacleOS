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
 * @file iso14443b_emu.h
 * @brief ISO14443B (NFC-B) target emulation with basic ISO-DEP/T4T APDUs.
 */
#ifndef ISO14443B_EMU_H
#define ISO14443B_EMU_H

#include <stdint.h>
#include "highboy_nfc_error.h"

typedef struct {
    uint8_t pupi[4];
    uint8_t app_data[4];
    uint8_t prot_info[3];
} iso14443b_emu_card_t;

/** Initialize default ATQB + T4T NDEF. */
hb_nfc_err_t iso14443b_emu_init_default(void);

/** Configure ST25R3916 as NFC-B target (ISO14443-3B). */
hb_nfc_err_t iso14443b_emu_configure_target(void);

/** Start emulation (enter SLEEP). */
hb_nfc_err_t iso14443b_emu_start(void);

/** Stop emulation. */
void iso14443b_emu_stop(void);

/** Run one emulation step (call in tight loop). */
void iso14443b_emu_run_step(void);

#endif /* ISO14443B_EMU_H */
