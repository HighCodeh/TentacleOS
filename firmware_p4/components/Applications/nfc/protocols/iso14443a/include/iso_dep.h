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
/**
 * @file iso_dep.h
 * @brief ISO-DEP (ISO14443-4) RATS, I-Block, chaining (Phase 6).
 */
#ifndef ISO_DEP_H
#define ISO_DEP_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send RATS and receive ATS.
 *
 * Validates ATS and fills the negotiated FSC/FSD, FWI and CID.
 *
 * @param fsdi  Frame size for proximity coupling device integer.
 * @param cid   Card identifier.
 * @param[out] dep  ISO-DEP data populated with ATS results.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso_dep_rats(uint8_t fsdi, uint8_t cid, nfc_iso_dep_data_t *dep);

/**
 * @brief Send optional PPS request.
 *
 * Keeps 106 kbps when dsi/dri are 0. Higher rates are rejected until the
 * radio bitrate and timing transition are implemented.
 *
 * @param cid  Card identifier.
 * @param dsi  Divisor send integer.
 * @param dri  Divisor receive integer.
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_TIMEOUT on no response
 */
hb_nfc_err_t iso_dep_pps(uint8_t cid, uint8_t dsi, uint8_t dri);

/**
 * @brief Exchange ISO-DEP I-Blocks with bounded chaining, CID and WTX handling.
 *
 * Returns 0 on incomplete/oversized responses; output must be ignored on error.
 * NAD and rates above 106 kbps are not negotiated by this reader.
 *
 * @param dep         ISO-DEP session data.
 * @param tx          Data to transmit.
 * @param tx_len      Length of TX data in bytes.
 * @param[out] rx     Buffer for received data.
 * @param rx_max      Maximum RX buffer size.
 * @param timeout_ms  Receive timeout in milliseconds.
 * @return Number of bytes received, or 0 on failure.
 */
int iso_dep_transceive(const nfc_iso_dep_data_t *dep,
                       const uint8_t *tx,
                       size_t tx_len,
                       uint8_t *rx,
                       size_t rx_max,
                       int timeout_ms);

/**
 * @brief APDU transceive convenience wrapper.
 *
 * Equivalent to iso_dep_transceive for APDU-level exchanges.
 *
 * @param dep         ISO-DEP session data.
 * @param apdu        APDU command bytes.
 * @param apdu_len    Length of APDU in bytes.
 * @param[out] rx     Buffer for response data.
 * @param rx_max      Maximum RX buffer size.
 * @param timeout_ms  Receive timeout in milliseconds.
 * @return Number of bytes received, or 0 on failure.
 */
int iso_dep_apdu_transceive(const nfc_iso_dep_data_t *dep,
                            const uint8_t *apdu,
                            size_t apdu_len,
                            uint8_t *rx,
                            size_t rx_max,
                            int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* ISO_DEP_H */
