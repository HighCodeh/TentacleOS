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
 * @file snep.c
 * @brief SNEP (Simple NDEF Exchange Protocol) client helpers.
 */
#include "snep.h"

#include <string.h>
#include <stdlib.h>

#define SNEP_SN            "urn:nfc:sn:snep"
#define SNEP_WKS_SAP       0x04U
#define SNEP_LOCAL_SSAP    0x20U

const char* snep_resp_str(uint8_t code)
{
    switch (code) {
    case SNEP_RES_SUCCESS:         return "Success";
    case SNEP_RES_CONTINUE:        return "Continue";
    case SNEP_RES_NOT_FOUND:       return "Not found";
    case SNEP_RES_EXCESS_DATA:     return "Excess data";
    case SNEP_RES_BAD_REQUEST:     return "Bad request";
    case SNEP_RES_NOT_IMPLEMENTED: return "Not implemented";
    case SNEP_RES_UNSUPPORTED_VER: return "Unsupported version";
    case SNEP_RES_REJECT:          return "Reject";
    default:                       return "Unknown";
    }
}

static hb_nfc_err_t snep_llcp_connect(llcp_link_t* link, int timeout_ms)
{
    if (!link || !link->llcp_active) return HB_NFC_ERR_PARAM;
    uint8_t tx[96];
    size_t tx_len = llcp_build_connect(SNEP_WKS_SAP, SNEP_LOCAL_SSAP,
                                       SNEP_SN, &link->local,
                                       tx, sizeof(tx));
    if (tx_len == 0) return HB_NFC_ERR_PARAM;

    uint8_t rx[96];
    int rlen = llcp_exchange_pdu(link, tx, tx_len, rx, sizeof(rx), timeout_ms);
    if (rlen < 2) return HB_NFC_ERR_PROTOCOL;

    uint8_t dsap = 0, ptype = 0, ssap = 0;
    if (!llcp_parse_header(rx, (size_t)rlen, &dsap, &ptype, &ssap, NULL, NULL)) {
        return HB_NFC_ERR_PROTOCOL;
    }
    if (ptype == LLCP_PTYPE_CC) return HB_NFC_OK;
    if (ptype == LLCP_PTYPE_DM) return HB_NFC_ERR_PROTOCOL;
    return HB_NFC_ERR_PROTOCOL;
}

static void snep_llcp_disconnect(llcp_link_t* link, int timeout_ms)
{
    if (!link || !link->llcp_active) return;
    uint8_t tx[8];
    size_t tx_len = llcp_build_disc(SNEP_WKS_SAP, SNEP_LOCAL_SSAP, tx, sizeof(tx));
    if (tx_len == 0) return;
    uint8_t rx[16];
    (void)llcp_exchange_pdu(link, tx, tx_len, rx, sizeof(rx), timeout_ms);
}

static hb_nfc_err_t snep_send_req(llcp_link_t* link,
                                  const uint8_t* snep, size_t snep_len,
                                  uint8_t* out, size_t out_max, size_t* out_len,
                                  int timeout_ms)
{
    if (!link || !link->llcp_active || !snep || snep_len == 0) return HB_NFC_ERR_PARAM;
    if (link->negotiated.miu && snep_len > link->negotiated.miu) {
        return HB_NFC_ERR_PARAM;
    }

    size_t pdu_max = 3U + snep_len;
    uint8_t stack_pdu[512];
    uint8_t* pdu = stack_pdu;
    bool dyn = false;
    if (pdu_max > sizeof(stack_pdu)) {
        pdu = (uint8_t*)malloc(pdu_max);
        if (!pdu) return HB_NFC_ERR_INTERNAL;
        dyn = true;
    }

    size_t pdu_len = llcp_build_i(SNEP_WKS_SAP, SNEP_LOCAL_SSAP,
                                  0, 0, snep, snep_len,
                                  pdu, pdu_max);
    if (pdu_len == 0) {
        if (dyn) free(pdu);
        return HB_NFC_ERR_PARAM;
    }

    uint8_t rx[512];
    int rlen = llcp_exchange_pdu(link, pdu, pdu_len, rx, sizeof(rx), timeout_ms);
    if (dyn) free(pdu);
    if (rlen < 3) return HB_NFC_ERR_PROTOCOL;

    uint8_t ptype = 0;
    if (!llcp_parse_header(rx, (size_t)rlen, NULL, &ptype, NULL, NULL, NULL)) {
        return HB_NFC_ERR_PROTOCOL;
    }
    if (ptype != LLCP_PTYPE_I) return HB_NFC_ERR_PROTOCOL;

    size_t info_len = (size_t)rlen - 3U;
    if (out && out_max > 0) {
        size_t copy = (info_len > out_max) ? out_max : info_len;
        memcpy(out, &rx[3], copy);
        if (out_len) *out_len = copy;
    } else if (out_len) {
        *out_len = info_len;
    }
    return HB_NFC_OK;
}

hb_nfc_err_t snep_client_put(llcp_link_t* link,
                             const uint8_t* ndef, size_t ndef_len,
                             uint8_t* resp_code,
                             int timeout_ms)
{
    if (!link || !ndef || ndef_len == 0) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = snep_llcp_connect(link, timeout_ms);
    if (err != HB_NFC_OK) return err;

    size_t snep_len = 6U + ndef_len;
    uint8_t stack_msg[256];
    uint8_t* msg = stack_msg;
    bool dyn = false;
    if (snep_len > sizeof(stack_msg)) {
        msg = (uint8_t*)malloc(snep_len);
        if (!msg) return HB_NFC_ERR_INTERNAL;
        dyn = true;
    }

    msg[0] = SNEP_VERSION_1_0;
    msg[1] = SNEP_REQ_PUT;
    msg[2] = (uint8_t)((ndef_len >> 24) & 0xFFU);
    msg[3] = (uint8_t)((ndef_len >> 16) & 0xFFU);
    msg[4] = (uint8_t)((ndef_len >> 8) & 0xFFU);
    msg[5] = (uint8_t)(ndef_len & 0xFFU);
    memcpy(&msg[6], ndef, ndef_len);

    uint8_t resp[64];
    size_t resp_len = 0;
    err = snep_send_req(link, msg, snep_len, resp, sizeof(resp), &resp_len, timeout_ms);
    if (dyn) free(msg);
    if (err != HB_NFC_OK) {
        snep_llcp_disconnect(link, timeout_ms);
        return err;
    }

    if (resp_len < 6U) {
        snep_llcp_disconnect(link, timeout_ms);
        return HB_NFC_ERR_PROTOCOL;
    }

    uint8_t code = resp[1];
    if (resp_code) *resp_code = code;
    snep_llcp_disconnect(link, timeout_ms);
    return (code == SNEP_RES_SUCCESS) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t snep_client_get(llcp_link_t* link,
                             const uint8_t* req_ndef, size_t req_len,
                             uint8_t* out, size_t out_max, size_t* out_len,
                             uint8_t* resp_code,
                             int timeout_ms)
{
    if (!link || !req_ndef || req_len == 0) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = snep_llcp_connect(link, timeout_ms);
    if (err != HB_NFC_OK) return err;

    size_t snep_len = 6U + req_len;
    uint8_t stack_msg[256];
    uint8_t* msg = stack_msg;
    bool dyn = false;
    if (snep_len > sizeof(stack_msg)) {
        msg = (uint8_t*)malloc(snep_len);
        if (!msg) return HB_NFC_ERR_INTERNAL;
        dyn = true;
    }

    msg[0] = SNEP_VERSION_1_0;
    msg[1] = SNEP_REQ_GET;
    msg[2] = (uint8_t)((req_len >> 24) & 0xFFU);
    msg[3] = (uint8_t)((req_len >> 16) & 0xFFU);
    msg[4] = (uint8_t)((req_len >> 8) & 0xFFU);
    msg[5] = (uint8_t)(req_len & 0xFFU);
    memcpy(&msg[6], req_ndef, req_len);

    uint8_t resp[512];
    size_t resp_len = 0;
    err = snep_send_req(link, msg, snep_len, resp, sizeof(resp), &resp_len, timeout_ms);
    if (dyn) free(msg);
    if (err != HB_NFC_OK) {
        snep_llcp_disconnect(link, timeout_ms);
        return err;
    }

    if (resp_len < 6U) {
        snep_llcp_disconnect(link, timeout_ms);
        return HB_NFC_ERR_PROTOCOL;
    }

    uint8_t code = resp[1];
    if (resp_code) *resp_code = code;
    if (code != SNEP_RES_SUCCESS) {
        snep_llcp_disconnect(link, timeout_ms);
        return HB_NFC_ERR_PROTOCOL;
    }

    size_t ndef_len = ((size_t)resp[2] << 24) |
                      ((size_t)resp[3] << 16) |
                      ((size_t)resp[4] << 8) |
                      (size_t)resp[5];
    if (resp_len < 6U + ndef_len) {
        snep_llcp_disconnect(link, timeout_ms);
        return HB_NFC_ERR_PROTOCOL;
    }

    if (out && out_max >= ndef_len) {
        memcpy(out, &resp[6], ndef_len);
        if (out_len) *out_len = ndef_len;
    } else if (out_len) {
        *out_len = ndef_len;
        snep_llcp_disconnect(link, timeout_ms);
        return HB_NFC_ERR_PARAM;
    }

    snep_llcp_disconnect(link, timeout_ms);
    return HB_NFC_OK;
}
