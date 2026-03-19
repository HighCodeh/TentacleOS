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
 * @file nfc_tag.h
 * @brief NFC Forum Tag Type handlers (Phase 7).
 */
#ifndef NFC_TAG_H
#define NFC_TAG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

#include "t1t.h"
#include "t4t.h"
#include "felica.h"
#include "iso15693.h"
#include "mf_plus.h"

typedef enum {
    NFC_TAG_TYPE_UNKNOWN = 0,
    NFC_TAG_TYPE_1 = 1,
    NFC_TAG_TYPE_2,
    NFC_TAG_TYPE_3,
    NFC_TAG_TYPE_4A,
    NFC_TAG_TYPE_4B,
    NFC_TAG_TYPE_6,
    NFC_TAG_TYPE_7,
    NFC_TAG_TYPE_V,
} nfc_tag_type_t;

typedef enum {
    NFC_TAG_SEC_PLAIN = 0x00,
    NFC_TAG_SEC_MAC   = 0x01,
    NFC_TAG_SEC_FULL  = 0x03,
} nfc_tag_sec_mode_t;

typedef struct {
    nfc_tag_type_t      type;
    hb_nfc_protocol_t   protocol;
    uint8_t             uid[NFC_UID_MAX_LEN];
    uint8_t             uid_len;
    uint16_t            block_size;

    nfc_iso14443a_data_t iso14443a;
    nfc_iso14443b_data_t iso14443b;
    t1t_tag_t            t1t;
    felica_tag_t         felica;
    iso15693_tag_t       iso15693;

    nfc_iso_dep_data_t   dep;
    t4t_cc_t             t4t_cc;

    uint16_t             felica_service;

    mfp_session_t        mfp;
    nfc_tag_sec_mode_t   sec_mode;
} nfc_tag_t;

const char* nfc_tag_type_str(nfc_tag_type_t type);

hb_nfc_err_t nfc_tag_detect(nfc_tag_t* tag);

hb_nfc_err_t nfc_tag_read_block(nfc_tag_t* tag,
                                uint32_t   block_num,
                                uint8_t*   out,
                                size_t     out_max,
                                size_t*    out_len);

hb_nfc_err_t nfc_tag_write_block(nfc_tag_t* tag,
                                 uint32_t   block_num,
                                 const uint8_t* data,
                                 size_t     data_len);

hb_nfc_err_t nfc_tag_get_uid(const nfc_tag_t* tag,
                             uint8_t*         uid,
                             size_t*          uid_len);

hb_nfc_err_t nfc_tag_mfp_auth_first(nfc_tag_t* tag,
                                   uint16_t   block_addr,
                                   const uint8_t key[16]);

hb_nfc_err_t nfc_tag_mfp_auth_nonfirst(nfc_tag_t* tag,
                                      uint16_t   block_addr,
                                      const uint8_t key[16]);

void nfc_tag_set_felica_service(nfc_tag_t* tag, uint16_t service_code);

#endif /* NFC_TAG_H */
