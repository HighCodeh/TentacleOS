// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef NFC_MFKEY_H
#define NFC_MFKEY_H
#include "highboy_nfc_compat.h"
#include "mfkey.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NFC_MFKEY_CONTINUE,
  NFC_MFKEY_FINISH_CAPTURE,
  NFC_MFKEY_CANCEL,
  NFC_MFKEY_CONTROL_COUNT
} nfc_mfkey_control_t;
typedef enum {
  NFC_MFKEY_CAPTURE,
  NFC_MFKEY_RECOVER,
  NFC_MFKEY_DONE,
  NFC_MFKEY_PHASE_COUNT
} nfc_mfkey_phase_t;
typedef struct {
  nfc_mfkey_phase_t phase;
  unsigned samples, pairs, processed, recovered, saved;
  mfkey_result_t recovery;
  esp_err_t error;
  bool cancelled;
  bool captures_saved;
} nfc_mfkey_status_t;
typedef struct {
  nfc_mfkey_control_t (*control)(void *context);
  void (*progress)(const nfc_mfkey_status_t *status, void *context);
  void *context;
} nfc_mfkey_callbacks_t;
/* Synchronous worker, never call on LVGL task. Captures bounded transcript pairs,
 * stops RF before recovery, saves validated keys to user dictionary. */
esp_err_t nfc_mfkey_run(const nfc_iso14443a_data_t *identity,
                        const nfc_mfkey_callbacks_t *callbacks,
                        nfc_mfkey_status_t *out);
#ifdef __cplusplus
}
#endif
#endif
