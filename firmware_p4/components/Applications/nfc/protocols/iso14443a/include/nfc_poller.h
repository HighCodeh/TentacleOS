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
 * @file nfc_poller.h
 * @brief NFC Poller field control + transceive engine.
 */
#ifndef NFC_POLLER_H
#define NFC_POLLER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

/** Initialize poller: set NFC-A mode + field on. */
hb_nfc_err_t nfc_poller_start(void);

/** Stop poller: field off. */
void nfc_poller_stop(void);

/**
 * Transmit a frame and read the response from FIFO.
 *
 * @param tx Data to transmit.
 * @param tx_len TX length in bytes.
 * @param with_crc true = CMD_TX_WITH_CRC, false = CMD_TX_WO_CRC.
 * @param rx Buffer for received data.
 * @param rx_max Max RX buffer size.
 * @param rx_min Minimum expected RX bytes (0 = don't wait).
 * @param timeout_ms RX FIFO wait timeout.
 * @return Number of bytes received, 0 on failure.
 */
int nfc_poller_transceive(const uint8_t* tx, size_t tx_len, bool with_crc,
                           uint8_t* rx, size_t rx_max, size_t rx_min,
                           int timeout_ms);

/** Configure guard time and FDT validation (best-effort). */
void nfc_poller_set_timing(uint32_t guard_time_us, uint32_t fdt_min_us, bool validate_fdt);

/** Get last measured FDT in microseconds. */
uint32_t nfc_poller_get_last_fdt_us(void);

#endif
