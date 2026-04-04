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
 * @file mf_desfire_emu.h
 * @brief Minimal MIFARE DESFire (native) APDU emulation helpers.
 */
#ifndef MF_DESFIRE_EMU_H
#define MF_DESFIRE_EMU_H

#include <stdint.h>
#include <stdbool.h>

/** Reset DESFire emu session state. */
void mf_desfire_emu_reset(void);

/**
 * Handle a DESFire native APDU (CLA=0x90).
 *
 * @param apdu     input APDU buffer
 * @param apdu_len input length
 * @param out      output buffer (data + SW1 SW2)
 * @param out_len  output length
 * @return true if handled, false if not a DESFire APDU
 */
bool mf_desfire_emu_handle_apdu(const uint8_t* apdu, int apdu_len,
                                uint8_t* out, int* out_len);

#endif /* MF_DESFIRE_EMU_H */
