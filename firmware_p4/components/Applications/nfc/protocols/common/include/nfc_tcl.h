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
 * @file nfc_tcl.h
 * @brief ISO14443-4 (T=CL) transport wrapper for A/B.
 */
#ifndef NFC_TCL_H
#define NFC_TCL_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

/**
 * Generic ISO14443-4 transceive.
 *
 * @param proto   HB_PROTO_ISO14443_4A or HB_PROTO_ISO14443_4B.
 * @param ctx     For A: pointer to nfc_iso_dep_data_t
 *                For B: pointer to nfc_iso14443b_data_t
 * @return number of bytes received, 0 on failure.
 */
int nfc_tcl_transceive(hb_nfc_protocol_t proto,
                        const void*        ctx,
                        const uint8_t*     tx,
                        size_t             tx_len,
                        uint8_t*           rx,
                        size_t             rx_max,
                        int                timeout_ms);

#endif
