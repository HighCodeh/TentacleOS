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
 * @file nfc_poller.h
 * @brief NFC Poller field control + transceive engine.
 */
#ifndef NFC_POLLER_H
#define NFC_POLLER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize poller in NFC-A mode with field on.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_IO on hardware failure
 */
hb_nfc_err_t nfc_poller_start(void);

/**
 * @brief Stop poller and turn field off.
 */
void nfc_poller_stop(void);

/**
 * @brief Transmit a frame and read the response from FIFO.
 *
 * @param tx          Data to transmit.
 * @param tx_len      TX length in bytes.
 * @param with_crc    True for CMD_TX_WITH_CRC, false for CMD_TX_WO_CRC.
 * @param[out] rx     Buffer for received data.
 * @param rx_max      Maximum RX buffer size.
 * @param rx_min      Minimum expected RX bytes (0 to not wait).
 * @param timeout_ms  RX FIFO wait timeout in milliseconds.
 * @return Number of bytes received, or 0 on failure.
 */
int nfc_poller_transceive(const uint8_t *tx,
                          size_t tx_len,
                          bool with_crc,
                          uint8_t *rx,
                          size_t rx_max,
                          size_t rx_min,
                          int timeout_ms);

/** Receive the four-byte, CRC-less initial Classic authentication nonce.
 * Only the CRC error indication is tolerated; parity, framing and timeout remain errors.
 */
/* True only after successful TX and a clean receive timeout with an empty FIFO. */
bool nfc_poller_expect_silence(const uint8_t *tx, size_t tx_len, int timeout_ms);

int nfc_poller_classic_nonce(const uint8_t auth[2], uint8_t nonce[4], int timeout_ms);

/**
 * @brief Configure guard time and FDT validation.
 *
 * @param guard_time_us  Guard time in microseconds.
 * @param fdt_min_us     Minimum FDT in microseconds.
 * @param validate_fdt   True to enable FDT validation.
 */
void nfc_poller_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt);

/**
 * @brief Get last measured FDT in microseconds.
 *
 * @return Last measured FDT value.
 */
uint32_t nfc_poller_get_last_fdt_us(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_POLLER_H */
