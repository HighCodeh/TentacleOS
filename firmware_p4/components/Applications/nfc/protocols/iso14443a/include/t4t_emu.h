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
 * @file t4t_emu.h
 * @brief ISO14443-4 / T4T emulation (ISO-DEP target).
 */
#ifndef T4T_EMU_H
#define T4T_EMU_H

#include "highboy_nfc_error.h"

/** Initialize default NDEF + CC. */
hb_nfc_err_t t4t_emu_init_default(void);

/** Configure ST25R3916 as NFC-A target for ISO-DEP. */
hb_nfc_err_t t4t_emu_configure_target(void);

/** Start emulation (enter SLEEP). */
hb_nfc_err_t t4t_emu_start(void);

/** Stop emulation. */
void t4t_emu_stop(void);

/** Run one emulation step (call in tight loop). */
void t4t_emu_run_step(void);

#endif /* T4T_EMU_H */
