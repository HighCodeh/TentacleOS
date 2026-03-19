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
 * @file ndef.h
 * @brief NDEF parser/builder helpers.
 */
#ifndef NDEF_H
#define NDEF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

/* TNF values */
#define NDEF_TNF_EMPTY        0x00U
#define NDEF_TNF_WELL_KNOWN   0x01U
#define NDEF_TNF_MIME         0x02U
#define NDEF_TNF_URI          0x03U
#define NDEF_TNF_EXTERNAL     0x04U
#define NDEF_TNF_UNKNOWN      0x05U
#define NDEF_TNF_UNCHANGED    0x06U
#define NDEF_TNF_RESERVED     0x07U

/* TLV types (Type 1/2/3 Tag mappings) */
#define NDEF_TLV_NULL         0x00U
#define NDEF_TLV_LOCK_CTRL    0x01U
#define NDEF_TLV_MEM_CTRL     0x02U
#define NDEF_TLV_NDEF         0x03U
#define NDEF_TLV_PROPRIETARY  0xFD
#define NDEF_TLV_TERMINATOR   0xFEU

typedef struct {
    bool     mb;
    bool     me;
    bool     cf;
    bool     sr;
    bool     il;
    uint8_t  tnf;
    uint8_t  type_len;
    uint8_t  id_len;
    uint32_t payload_len;
    const uint8_t* type;
    const uint8_t* id;
    const uint8_t* payload;
} ndef_record_t;

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
    bool done;
    hb_nfc_err_t err;
} ndef_parser_t;

typedef struct {
    const uint8_t* lang;
    uint8_t  lang_len;
    const uint8_t* text;
    size_t   text_len;
    bool     utf16;
} ndef_text_t;

typedef struct {
    uint8_t* buf;
    size_t max;
    size_t len;
    size_t last_hdr_off;
    bool has_record;
} ndef_builder_t;

typedef struct {
    uint8_t  type;
    size_t   length;
    const uint8_t* value;
    size_t   tlv_len;
} ndef_tlv_t;

typedef enum {
    NDEF_TLV_STATUS_OK = 0,
    NDEF_TLV_STATUS_NOT_FOUND = 1,
    NDEF_TLV_STATUS_INVALID = 2
} ndef_tlv_status_t;

/* Parser helpers */
void ndef_parser_init(ndef_parser_t* parser, const uint8_t* data, size_t len);
bool ndef_parse_next(ndef_parser_t* parser, ndef_record_t* rec);
hb_nfc_err_t ndef_parser_error(const ndef_parser_t* parser);

bool ndef_record_is_text(const ndef_record_t* rec);
bool ndef_record_is_uri(const ndef_record_t* rec);
bool ndef_record_is_smart_poster(const ndef_record_t* rec);
bool ndef_record_is_mime(const ndef_record_t* rec);

bool ndef_decode_text(const ndef_record_t* rec, ndef_text_t* out);
bool ndef_decode_uri(const ndef_record_t* rec,
                     char* out, size_t out_max, size_t* out_len);

/* Builder helpers */
void ndef_builder_init(ndef_builder_t* b, uint8_t* out, size_t max);
size_t ndef_builder_len(const ndef_builder_t* b);
hb_nfc_err_t ndef_builder_add_record(ndef_builder_t* b,
                                     uint8_t tnf,
                                     const uint8_t* type, uint8_t type_len,
                                     const uint8_t* id, uint8_t id_len,
                                     const uint8_t* payload, uint32_t payload_len);
hb_nfc_err_t ndef_builder_add_text(ndef_builder_t* b,
                                   const char* text,
                                   const char* lang,
                                   bool utf16);
hb_nfc_err_t ndef_builder_add_uri(ndef_builder_t* b, const char* uri);
hb_nfc_err_t ndef_builder_add_mime(ndef_builder_t* b,
                                   const char* mime_type,
                                   const uint8_t* data, size_t data_len);
hb_nfc_err_t ndef_builder_add_smart_poster(ndef_builder_t* b,
                                           const uint8_t* nested_ndef,
                                           size_t nested_len);

/* TLV helpers (Type 1/2/3 mappings) */
ndef_tlv_status_t ndef_tlv_find(const uint8_t* data, size_t len,
                                uint8_t type, ndef_tlv_t* out);
ndef_tlv_status_t ndef_tlv_find_ndef(const uint8_t* data, size_t len,
                                     ndef_tlv_t* out);
ndef_tlv_status_t ndef_tlv_build(uint8_t* out, size_t max,
                                 uint8_t type,
                                 const uint8_t* value, size_t value_len,
                                 size_t* out_len);
ndef_tlv_status_t ndef_tlv_build_ndef(uint8_t* out, size_t max,
                                      const uint8_t* ndef, size_t ndef_len,
                                      size_t* out_len);

/** Print NDEF records to log (best-effort). */
void ndef_print(const uint8_t* data, size_t len);

#endif /* NDEF_H */
