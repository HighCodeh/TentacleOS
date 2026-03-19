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
 * @file highboy_nfc_types.h
 * @brief High Boy NFC Shared data types.
 */
#ifndef HIGHBOY_NFC_TYPES_H
#define HIGHBOY_NFC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NFC_UID_MAX_LEN     10
#define NFC_ATS_MAX_LEN     64
#define NFC_FIFO_MAX_LEN    512

typedef struct {
    uint8_t uid[NFC_UID_MAX_LEN];
    uint8_t uid_len;                
    uint8_t atqa[2];
    uint8_t sak;
} nfc_iso14443a_data_t;

typedef struct {
    uint8_t ats[NFC_ATS_MAX_LEN];
    size_t  ats_len;
    uint8_t fsc;
    uint8_t fwi;
} nfc_iso_dep_data_t;

typedef struct {
    uint8_t pupi[4];
    uint8_t app_data[4];
    uint8_t prot_info[3];
} nfc_iso14443b_data_t;

typedef struct {
    uint8_t idm[8];
    uint8_t pmm[8];
} nfc_felica_data_t;

typedef struct {
    uint8_t uid[8];
    uint8_t dsfid;
    uint16_t block_count;
    uint8_t block_size;
} nfc_iso15693_data_t;

typedef enum {
    MF_KEY_A = 0x60,
    MF_KEY_B = 0x61,
} mf_key_type_t;

typedef struct {
    uint8_t data[6];
} mf_classic_key_t;

typedef enum {
    MF_CLASSIC_MINI = 0,    
    MF_CLASSIC_1K,          
    MF_CLASSIC_4K,          
} mf_classic_type_t;

typedef enum {
    HB_PROTO_ISO14443_3A = 0,
    HB_PROTO_ISO14443_3B,
    HB_PROTO_ISO14443_4A,
    HB_PROTO_ISO14443_4B,
    HB_PROTO_FELICA,
    HB_PROTO_ISO15693,
    HB_PROTO_ST25TB,
    HB_PROTO_MF_CLASSIC,
    HB_PROTO_MF_ULTRALIGHT,
    HB_PROTO_MF_DESFIRE,
    HB_PROTO_MF_PLUS,
    HB_PROTO_SLIX,
    HB_PROTO_UNKNOWN = 0xFF,
} hb_nfc_protocol_t;

typedef struct {
    hb_nfc_protocol_t protocol;
    nfc_iso14443a_data_t iso14443a;
    union {
        nfc_iso_dep_data_t   iso_dep;
        nfc_iso14443b_data_t iso14443b;
        nfc_felica_data_t    felica;
        nfc_iso15693_data_t  iso15693;
    };
} hb_nfc_card_data_t;

#endif
