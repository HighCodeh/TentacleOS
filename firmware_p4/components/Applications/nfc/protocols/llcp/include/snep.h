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
 * @file snep.h
 * @brief SNEP (Simple NDEF Exchange Protocol) client helpers.
 */
#ifndef SNEP_H
#define SNEP_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_error.h"
#include "llcp.h"

#define SNEP_VERSION_1_0    0x10U

#define SNEP_REQ_GET        0x01U
#define SNEP_REQ_PUT        0x02U

#define SNEP_RES_SUCCESS            0x81U
#define SNEP_RES_CONTINUE           0x80U
#define SNEP_RES_NOT_FOUND          0xC0U
#define SNEP_RES_EXCESS_DATA        0xC1U
#define SNEP_RES_BAD_REQUEST        0xC2U
#define SNEP_RES_NOT_IMPLEMENTED    0xE0U
#define SNEP_RES_UNSUPPORTED_VER    0xE1U
#define SNEP_RES_REJECT             0xFFU

const char* snep_resp_str(uint8_t code);

hb_nfc_err_t snep_client_put(llcp_link_t* link,
                            const uint8_t* ndef, size_t ndef_len,
                            uint8_t* resp_code,
                            int timeout_ms);

hb_nfc_err_t snep_client_get(llcp_link_t* link,
                            const uint8_t* req_ndef, size_t req_len,
                            uint8_t* out, size_t out_max, size_t* out_len,
                            uint8_t* resp_code,
                            int timeout_ms);

#endif /* SNEP_H */
