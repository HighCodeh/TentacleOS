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

#ifndef NFC_SCANNER_H
#define NFC_SCANNER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#define NFC_SCANNER_MAX_PROTOCOLS 8

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
