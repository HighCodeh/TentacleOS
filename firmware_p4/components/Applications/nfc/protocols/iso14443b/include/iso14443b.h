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
 * @file iso14443b.h
 * @brief ISO 14443B (NFC-B) poller basics for ST25R3916.
 */
#ifndef ISO14443B_H
#define ISO14443B_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure the chip for NFC-B poller mode (106 kbps).
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on communication failure
 */
hb_nfc_err_t iso14443b_poller_init(void);

/**
 * @brief Send REQB and parse ATQB (PUPI/AppData/ProtInfo).
 *
 * @param afi    Application Family Identifier byte.
 * @param param  REQB parameter byte (number of slots, etc.).
 * @param[out] out  Parsed ATQB data.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_CARD if no response
 */
hb_nfc_err_t iso14443b_reqb(uint8_t afi, uint8_t param, nfc_iso14443b_data_t *out);

/**
 * @brief Send ATTRIB with basic/default parameters (best-effort).
 *
 * @param card  Card data from a prior REQB.
 * @param fsdi  Frame Size for proximity coupling Device Integer.
 * @param cid   Card Identifier (0 if unused).
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso14443b_attrib(const nfc_iso14443b_data_t *card, uint8_t fsdi, uint8_t cid);

/**
 * @brief Convenience wrapper: REQB followed by ATTRIB.
 *
 * @param[out] out   Populated card data on success.
 * @param afi        Application Family Identifier byte.
 * @param param      REQB parameter byte.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_CARD if no tag found
 */
hb_nfc_err_t iso14443b_select(nfc_iso14443b_data_t *out, uint8_t afi, uint8_t param);

/**
 * @brief Best-effort T4T NDEF read over ISO14443B.
 *
 * @param afi        Application Family Identifier byte.
 * @param param      REQB parameter byte.
 * @param[out] out       Output buffer for NDEF message.
 * @param out_max    Output buffer capacity in bytes.
 * @param[out] out_len   Set to actual NDEF length on success.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_NO_CARD if no tag found
 */
hb_nfc_err_t
iso14443b_read_ndef(uint8_t afi, uint8_t param, uint8_t *out, size_t out_max, size_t *out_len);

/**
 * @brief ISO-DEP (T=CL) transceive for NFC-B.
 *
 * @param card        Card data with valid PUPI.
 * @param tx          Transmit buffer.
 * @param tx_len      Number of bytes to transmit.
 * @param[out] rx     Receive buffer.
 * @param rx_max      Receive buffer capacity in bytes.
 * @param timeout_ms  Timeout in milliseconds.
 * @return Number of bytes received, or negative on error.
 */
int iso14443b_tcl_transceive(const nfc_iso14443b_data_t *card,
                             const uint8_t *tx,
                             size_t tx_len,
                             uint8_t *rx,
                             size_t rx_max,
                             int timeout_ms);

/**
 * @brief Single entry from NFC-B anticollision.
 */
typedef struct {
  nfc_iso14443b_data_t card; /**< Card data from ATQB. */
  uint8_t slot;              /**< Slot in which the card responded. */
} iso14443b_anticoll_entry_t;

/**
 * @brief Best-effort anticollision using 1 or 16 slots.
 *
 * @param afi      AFI byte (0x00 for all).
 * @param slots    1 or 16 (other values are rounded).
 * @param[out] out Output array of cards.
 * @param max_out  Max entries in output array.
 * @return Number of cards found.
 */
int iso14443b_anticoll(uint8_t afi, uint8_t slots, iso14443b_anticoll_entry_t *out, size_t max_out);

#ifdef __cplusplus
}
#endif

#endif /* ISO14443B_H */
