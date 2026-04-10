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

#ifndef NFC_SCANNER_H
#define NFC_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#define NFC_SCANNER_MAX_PROTOCOLS 4

/**
 * @brief Scanner event containing detected protocols.
 */
typedef struct {
  hb_nfc_protocol_t protocols[NFC_SCANNER_MAX_PROTOCOLS]; /**< @brief Detected protocol list. */
  uint8_t count; /**< @brief Number of detected protocols. */
} nfc_scanner_event_t;

/**
 * @brief Callback invoked when scanner detects protocols.
 *
 * @param event Detected protocols.
 * @param ctx   User context.
 */
typedef void (*nfc_scanner_cb_t)(nfc_scanner_event_t event, void *ctx);

typedef struct nfc_scanner nfc_scanner_t;

/**
 * @brief Allocate a new scanner instance.
 *
 * @return Pointer to scanner, or NULL on failure.
 */
nfc_scanner_t *nfc_scanner_alloc(void);

/**
 * @brief Free a scanner instance.
 *
 * @param s Scanner to free. NULL is safe.
 */
void nfc_scanner_free(nfc_scanner_t *s);

/**
 * @brief Start scanning for NFC cards.
 *
 * @param s   Scanner instance.
 * @param cb  Callback for detected protocols.
 * @param ctx User context.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if s or cb is NULL
 *   - HB_NFC_ERR_INTERNAL on hardware failure
 */
hb_nfc_err_t nfc_scanner_start(nfc_scanner_t *s, nfc_scanner_cb_t cb, void *ctx);

/**
 * @brief Stop scanner.
 *
 * @param s Scanner instance.
 */
void nfc_scanner_stop(nfc_scanner_t *s);

#ifdef __cplusplus
}
#endif

#endif /* NFC_SCANNER_H */
