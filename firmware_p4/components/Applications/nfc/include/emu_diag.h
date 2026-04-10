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

#ifndef EMU_DIAG_H
#define EMU_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

/**
 * @brief Run full target mode diagnostic.
 *
 * Tests field detection, multiple configs, PT Memory, oscillator.
 * Takes ~60 seconds. Share the FULL serial output!
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_COMM on communication failure
 *   - HB_NFC_ERR_INTERNAL on hardware error
 */
hb_nfc_err_t emu_diag_full(void);

/**
 * @brief Monitor target interrupts for N seconds.
 *
 * @param seconds Number of seconds to monitor.
 */
void emu_diag_monitor(int seconds);

#ifdef __cplusplus
}
#endif

#endif /* EMU_DIAG_H */
