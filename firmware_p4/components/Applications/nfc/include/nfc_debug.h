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
 * @file nfc_debug.h
 * @brief NFC Debug tools CW, register dump, AAT sweep.
 */
#ifndef NFC_DEBUG_H
#define NFC_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "highboy_nfc_error.h"

/**
 * @brief Enable continuous wave (CW) output.
 */
hb_nfc_err_t nfc_debug_cw_on(void);

/**
 * @brief Disable continuous wave (CW) output.
 */
void nfc_debug_cw_off(void);

/**
 * @brief Dump all ST25R3916 registers to log.
 */
void nfc_debug_dump_regs(void);

/**
 * @brief Run AAT (Automatic Antenna Tuning) sweep.
 */
hb_nfc_err_t nfc_debug_aat_sweep(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_DEBUG_H */
