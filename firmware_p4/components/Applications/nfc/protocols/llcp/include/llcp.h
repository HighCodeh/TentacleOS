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
 * @file llcp.h
 * @brief LLCP (Logical Link Control Protocol) over NFC-DEP.
 */
#ifndef LLCP_H
#define LLCP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

#define LLCP_MAGIC_0 0x46U
#define LLCP_MAGIC_1 0x66U
#define LLCP_MAGIC_2 0x6DU

/* LLCP PTYPE values */
#define LLCP_PTYPE_SYMM    0x0U
#define LLCP_PTYPE_PAX     0x1U
#define LLCP_PTYPE_AGF     0x2U
#define LLCP_PTYPE_UI      0x3U
#define LLCP_PTYPE_CONNECT 0x4U
#define LLCP_PTYPE_DISC    0x5U
#define LLCP_PTYPE_CC      0x6U
#define LLCP_PTYPE_DM      0x7U
#define LLCP_PTYPE_FRMR    0x8U
#define LLCP_PTYPE_SNL     0x9U
#define LLCP_PTYPE_I       0xCU
#define LLCP_PTYPE_RR      0xDU
#define LLCP_PTYPE_RNR     0xEU

/* LLCP TLV types */
#define LLCP_TLV_VERSION 0x01U
#define LLCP_TLV_MIUX    0x02U
#define LLCP_TLV_WKS     0x03U
#define LLCP_TLV_LTO     0x04U
#define LLCP_TLV_RW      0x05U
#define LLCP_TLV_SN      0x06U
#define LLCP_TLV_OPT     0x07U

typedef struct {
    uint8_t  version_major;
    uint8_t  version_minor;
    uint16_t miu;       /* Maximum Information Unit */
    uint16_t wks;       /* Well-Known Services bitmap */
    uint16_t lto_ms;    /* Link Timeout in ms */
    uint8_t  rw;        /* Receive window */
} llcp_params_t;

typedef struct {
    uint8_t  nfcid3[10];
    uint8_t  did;
    uint8_t  bs;
    uint8_t  br;
    uint8_t  pp;
    uint8_t  to;
    uint16_t lr;
    uint16_t rwt_ms;
    uint8_t  pni;
    llcp_params_t local;
    llcp_params_t remote;
    llcp_params_t negotiated;
    bool     llcp_active;
} llcp_link_t;

void   llcp_params_default(llcp_params_t* params);
void   llcp_link_init(llcp_link_t* link);
size_t llcp_build_gt(const llcp_params_t* params, uint8_t* out, size_t max);
bool   llcp_parse_gt(const uint8_t* gt, size_t gt_len, llcp_params_t* out);

hb_nfc_err_t llcp_initiator_activate(llcp_link_t* link);
int          llcp_exchange_pdu(llcp_link_t* link,
                               const uint8_t* tx_pdu, size_t tx_len,
                               uint8_t* rx_pdu, size_t rx_max,
                               int timeout_ms);

size_t llcp_build_header(uint8_t dsap, uint8_t ptype, uint8_t ssap, uint8_t* out, size_t max);
bool   llcp_parse_header(const uint8_t* pdu, size_t len,
                         uint8_t* dsap, uint8_t* ptype, uint8_t* ssap,
                         uint8_t* ns, uint8_t* nr);

size_t llcp_build_symm(uint8_t* out, size_t max);
size_t llcp_build_ui(uint8_t dsap, uint8_t ssap,
                     const uint8_t* info, size_t info_len,
                     uint8_t* out, size_t max);
size_t llcp_build_connect(uint8_t dsap, uint8_t ssap, const char* service_name,
                          const llcp_params_t* params, uint8_t* out, size_t max);
size_t llcp_build_cc(uint8_t dsap, uint8_t ssap,
                     const llcp_params_t* params, uint8_t* out, size_t max);
size_t llcp_build_disc(uint8_t dsap, uint8_t ssap, uint8_t* out, size_t max);
size_t llcp_build_dm(uint8_t dsap, uint8_t ssap, uint8_t reason, uint8_t* out, size_t max);
size_t llcp_build_i(uint8_t dsap, uint8_t ssap, uint8_t ns, uint8_t nr,
                    const uint8_t* info, size_t info_len,
                    uint8_t* out, size_t max);
size_t llcp_build_rr(uint8_t dsap, uint8_t ssap, uint8_t nr, uint8_t* out, size_t max);
size_t llcp_build_rnr(uint8_t dsap, uint8_t ssap, uint8_t nr, uint8_t* out, size_t max);

size_t llcp_tlv_version(uint8_t* out, size_t max, uint8_t version);
size_t llcp_tlv_miux(uint8_t* out, size_t max, uint16_t miux);
size_t llcp_tlv_wks(uint8_t* out, size_t max, uint16_t wks);
size_t llcp_tlv_lto(uint8_t* out, size_t max, uint8_t lto);
size_t llcp_tlv_rw(uint8_t* out, size_t max, uint8_t rw);
size_t llcp_tlv_sn(uint8_t* out, size_t max, const char* sn);

#endif
