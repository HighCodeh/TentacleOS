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
 * @file nfc_scanner.h
 * @brief NFC Scanner auto-detect card technology (Phase 8).
 *
 * Probes NFC-A NFC-B NFC-F NFC-V in sequence.
 * Reports detected protocol(s) via callback.
 */
#ifndef NFC_SCANNER_H
#define NFC_SCANNER_H

#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"

#define NFC_SCANNER_MAX_PROTOCOLS  4

typedef struct {
    hb_nfc_protocol_t protocols[NFC_SCANNER_MAX_PROTOCOLS];
    uint8_t           count;
} nfc_scanner_event_t;

typedef void (*nfc_scanner_cb_t)(nfc_scanner_event_t event, void* ctx);

typedef struct nfc_scanner nfc_scanner_t;

nfc_scanner_t* nfc_scanner_alloc(void);
void           nfc_scanner_free(nfc_scanner_t* s);
hb_nfc_err_t   nfc_scanner_start(nfc_scanner_t* s, nfc_scanner_cb_t cb, void* ctx);
void           nfc_scanner_stop(nfc_scanner_t* s);

#endif
