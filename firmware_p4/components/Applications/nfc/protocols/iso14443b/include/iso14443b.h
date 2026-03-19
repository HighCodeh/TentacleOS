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
#include "highboy_nfc_types.h"

/** Configure the chip for NFC-B poller mode (106 kbps). */
hb_nfc_err_t iso14443b_poller_init(void);

/** Send REQB and parse ATQB (PUPI/AppData/ProtInfo). */
hb_nfc_err_t iso14443b_reqb(uint8_t afi, uint8_t param, nfc_iso14443b_data_t* out);

/** Send ATTRIB with basic/default parameters (best-effort). */
hb_nfc_err_t iso14443b_attrib(const nfc_iso14443b_data_t* card, uint8_t fsdi, uint8_t cid);

/** Convenience: REQB + ATTRIB. */
hb_nfc_err_t iso14443b_select(nfc_iso14443b_data_t* out, uint8_t afi, uint8_t param);

/** Best-effort T4T NDEF read over ISO14443B. */
hb_nfc_err_t iso14443b_read_ndef(uint8_t afi, uint8_t param,
                                  uint8_t* out, size_t out_max, size_t* out_len);

/** ISO-DEP (T=CL) transceive for NFC-B. */
int iso14443b_tcl_transceive(const nfc_iso14443b_data_t* card,
                              const uint8_t*             tx,
                              size_t                     tx_len,
                              uint8_t*                   rx,
                              size_t                     rx_max,
                              int                        timeout_ms);

typedef struct {
    nfc_iso14443b_data_t card;
    uint8_t slot;
} iso14443b_anticoll_entry_t;

/**
 * Best-effort anticollision using 1 or 16 slots.
 * @param afi     AFI byte (0x00 for all).
 * @param slots   1 or 16 (other values are rounded).
 * @param out     Output array of cards.
 * @param max_out Max entries.
 * @return number of cards found.
 */
int iso14443b_anticoll(uint8_t afi, uint8_t slots,
                        iso14443b_anticoll_entry_t* out, size_t max_out);

#endif /* ISO14443B_H */
