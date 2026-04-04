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
 * @file nfc_apdu.c
 * @brief ISO/IEC 7816-4 APDU helpers over ISO14443-4 (T=CL).
 */
#include "nfc_apdu.h"

#include <string.h>
#include "nfc_tcl.h"

#define APDU_MAX_BUF 300U

const char* nfc_apdu_sw_str(uint16_t sw)
{
    switch (sw) {
    case APDU_SW_OK:             return "OK";
    case APDU_SW_WRONG_LENGTH:   return "Wrong length";
    case APDU_SW_SECURITY:       return "Security status not satisfied";
    case APDU_SW_AUTH_FAILED:    return "Authentication failed";
    case APDU_SW_FILE_NOT_FOUND: return "File not found";
    case APDU_SW_INS_NOT_SUP:    return "INS not supported";
    case APDU_SW_CLA_NOT_SUP:    return "CLA not supported";
    case APDU_SW_UNKNOWN:        return "Unknown error";
    default:                     return "SW error";
    }
}

hb_nfc_err_t nfc_apdu_sw_to_err(uint16_t sw)
{
    if (sw == APDU_SW_OK) return HB_NFC_OK;
    if (sw == APDU_SW_SECURITY || sw == APDU_SW_AUTH_FAILED) return HB_NFC_ERR_AUTH;
    return HB_NFC_ERR_PROTOCOL;
}

static int apdu_transceive_raw(hb_nfc_protocol_t proto,
                               const void*        ctx,
                               const uint8_t*     apdu,
                               size_t             apdu_len,
                               uint8_t*           rx,
                               size_t             rx_max,
                               int                timeout_ms)
{
    return nfc_tcl_transceive(proto, ctx, apdu, apdu_len, rx, rx_max, timeout_ms);
}

static int apdu_get_response(hb_nfc_protocol_t proto,
                             const void*        ctx,
                             uint8_t            le,
                             uint8_t*           rx,
                             size_t             rx_max,
                             int                timeout_ms)
{
    uint8_t apdu[5] = { 0x00, 0xC0, 0x00, 0x00, le };
    return apdu_transceive_raw(proto, ctx, apdu, sizeof(apdu), rx, rx_max, timeout_ms);
}

hb_nfc_err_t nfc_apdu_transceive(hb_nfc_protocol_t proto,
                                 const void*        ctx,
                                 const uint8_t*     apdu,
                                 size_t             apdu_len,
                                 uint8_t*           out,
                                 size_t             out_max,
                                 size_t*            out_len,
                                 uint16_t*          sw,
                                 int                timeout_ms)
{
    if (!ctx || !apdu || apdu_len == 0 || !out || out_max < 2U) return HB_NFC_ERR_PARAM;

    uint8_t cmd[APDU_MAX_BUF];
    if (apdu_len > sizeof(cmd)) return HB_NFC_ERR_PARAM;
    memcpy(cmd, apdu, apdu_len);

    bool sw_in_buf = true;

    int len = apdu_transceive_raw(proto, ctx, cmd, apdu_len, out, out_max, timeout_ms);
    if (len < 2) return HB_NFC_ERR_PROTOCOL;

    uint16_t status = (uint16_t)((out[len - 2] << 8) | out[len - 1]);

    /* 6Cxx: wrong length, retry with Le = SW2 */
    if (((status & 0xFF00U) == 0x6C00U) && apdu_len >= 5) {
        cmd[apdu_len - 1] = (uint8_t)(status & 0x00FFU);
        len = apdu_transceive_raw(proto, ctx, cmd, apdu_len, out, out_max, timeout_ms);
        if (len < 2) return HB_NFC_ERR_PROTOCOL;
        status = (uint16_t)((out[len - 2] << 8) | out[len - 1]);
    }

    /* 61xx: GET RESPONSE chaining */
    if ((status & 0xFF00U) == 0x6100U) {
        size_t out_pos = (size_t)(len - 2);
        uint8_t le = (uint8_t)(status & 0xFFU);
        if (le == 0) le = 0x00; /* 256 */

        for (int i = 0; i < 8; i++) {
            uint8_t tmp[APDU_MAX_BUF];
            int rlen = apdu_get_response(proto, ctx, le, tmp, sizeof(tmp), timeout_ms);
            if (rlen < 2) break;

            uint16_t swr = (uint16_t)((tmp[rlen - 2] << 8) | tmp[rlen - 1]);
            int data_len = rlen - 2;
            if (data_len > 0) {
                size_t copy = (out_pos + (size_t)data_len <= out_max)
                                ? (size_t)data_len
                                : (out_max > out_pos ? (out_max - out_pos) : 0);
                if (copy > 0) {
                    memcpy(&out[out_pos], tmp, copy);
                    out_pos += copy;
                }
            }

            status = swr;
            if ((status & 0xFF00U) != 0x6100U) break;
            le = (uint8_t)(status & 0xFFU);
            if (le == 0) le = 0x00;
        }

        if (out_pos + 2 <= out_max) {
            out[out_pos++] = (uint8_t)(status >> 8);
            out[out_pos++] = (uint8_t)(status & 0xFFU);
            sw_in_buf = true;
        } else {
            sw_in_buf = false;
        }
        len = (int)out_pos;
    }

    if (sw) *sw = status;
    if (out_len) *out_len = sw_in_buf ? (size_t)(len - 2) : (size_t)len;
    return nfc_apdu_sw_to_err(status);
}

hb_nfc_err_t nfc_apdu_send(hb_nfc_protocol_t proto,
                           const void*        ctx,
                           const uint8_t*     apdu,
                           size_t             apdu_len,
                           uint8_t*           out,
                           size_t             out_max,
                           size_t*            out_len,
                           uint16_t*          sw,
                           int                timeout_ms)
{
    return nfc_apdu_transceive(proto, ctx, apdu, apdu_len,
                               out, out_max, out_len, sw, timeout_ms);
}

hb_nfc_err_t nfc_apdu_recv(hb_nfc_protocol_t proto,
                           const void*        ctx,
                           const uint8_t*     apdu,
                           size_t             apdu_len,
                           uint8_t*           out,
                           size_t             out_max,
                           size_t*            out_len,
                           uint16_t*          sw,
                           int                timeout_ms)
{
    return nfc_apdu_transceive(proto, ctx, apdu, apdu_len,
                               out, out_max, out_len, sw, timeout_ms);
}

size_t nfc_apdu_build_select_aid(uint8_t* out, size_t max,
                                 const uint8_t* aid, size_t aid_len,
                                 bool le_present, uint8_t le)
{
    if (!out || !aid || aid_len == 0) return 0;
    if (max < 5U + aid_len + (le_present ? 1U : 0U)) return 0;
    size_t pos = 0;
    out[pos++] = 0x00;
    out[pos++] = 0xA4;
    out[pos++] = 0x04;
    out[pos++] = 0x00;
    out[pos++] = (uint8_t)aid_len;
    memcpy(&out[pos], aid, aid_len);
    pos += aid_len;
    if (le_present) out[pos++] = le;
    return pos;
}

size_t nfc_apdu_build_select_file(uint8_t* out, size_t max,
                                  uint16_t fid, bool le_present, uint8_t le)
{
    if (!out || max < (7U + (le_present ? 1U : 0U))) return 0;
    size_t pos = 0;
    out[pos++] = 0x00;
    out[pos++] = 0xA4;
    out[pos++] = 0x00;
    out[pos++] = 0x0C;
    out[pos++] = 0x02;
    out[pos++] = (uint8_t)((fid >> 8) & 0xFFU);
    out[pos++] = (uint8_t)(fid & 0xFFU);
    if (le_present) out[pos++] = le;
    return pos;
}

size_t nfc_apdu_build_read_binary(uint8_t* out, size_t max,
                                  uint16_t offset, uint8_t le)
{
    if (!out || max < 5U) return 0;
    out[0] = 0x00;
    out[1] = 0xB0;
    out[2] = (uint8_t)((offset >> 8) & 0xFFU);
    out[3] = (uint8_t)(offset & 0xFFU);
    out[4] = le;
    return 5U;
}

size_t nfc_apdu_build_update_binary(uint8_t* out, size_t max,
                                    uint16_t offset,
                                    const uint8_t* data, size_t data_len)
{
    if (!out || !data || data_len == 0 || data_len > 0xFFU) return 0;
    if (max < 5U + data_len) return 0;
    size_t pos = 0;
    out[pos++] = 0x00;
    out[pos++] = 0xD6;
    out[pos++] = (uint8_t)((offset >> 8) & 0xFFU);
    out[pos++] = (uint8_t)(offset & 0xFFU);
    out[pos++] = (uint8_t)data_len;
    memcpy(&out[pos], data, data_len);
    pos += data_len;
    return pos;
}

size_t nfc_apdu_build_verify(uint8_t* out, size_t max,
                             uint8_t p1, uint8_t p2,
                             const uint8_t* data, size_t data_len)
{
    if (!out || !data || data_len == 0 || data_len > 0xFFU) return 0;
    if (max < 5U + data_len) return 0;
    size_t pos = 0;
    out[pos++] = 0x00;
    out[pos++] = 0x20;
    out[pos++] = p1;
    out[pos++] = p2;
    out[pos++] = (uint8_t)data_len;
    memcpy(&out[pos], data, data_len);
    pos += data_len;
    return pos;
}

size_t nfc_apdu_build_get_data(uint8_t* out, size_t max,
                               uint8_t p1, uint8_t p2, uint8_t le)
{
    if (!out || max < 5U) return 0;
    out[0] = 0x00;
    out[1] = 0xCA;
    out[2] = p1;
    out[3] = p2;
    out[4] = le;
    return 5U;
}

bool nfc_apdu_parse_fci(const uint8_t* fci, size_t fci_len, nfc_fci_info_t* out)
{
    if (!fci || fci_len < 2U || !out) return false;
    memset(out, 0, sizeof(*out));

    size_t pos = 0;
    while (pos + 2U <= fci_len) {
        uint8_t tag = fci[pos++];
        uint8_t len = fci[pos++];
        if (pos + len > fci_len) break;

        if (tag == 0x84U) {
            size_t copy = (len > sizeof(out->df_name)) ? sizeof(out->df_name) : len;
            memcpy(out->df_name, &fci[pos], copy);
            out->df_name_len = (uint8_t)copy;
            return true;
        }

        pos += len;
    }
    return false;
}
