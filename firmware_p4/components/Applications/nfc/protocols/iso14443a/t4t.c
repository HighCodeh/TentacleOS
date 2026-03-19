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
 * @file t4t.c
 * @brief ISO14443-4 / Type 4 Tag (T4T) NDEF reader helpers.
 */
#include "t4t.h"

#include <string.h>
#include "esp_log.h"

#include "iso_dep.h"
#include "nfc_common.h"

#define TAG "t4t"

#define T4T_AID_LEN   7
#define T4T_CC_FILE   0xE103U

static const uint8_t T4T_AID[T4T_AID_LEN] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };

static hb_nfc_err_t t4t_apdu(const nfc_iso_dep_data_t* dep,
                              const uint8_t*           apdu,
                              size_t                   apdu_len,
                              uint8_t*                 out,
                              size_t                   out_max,
                              size_t*                  out_len,
                              uint16_t*                sw)
{
    if (!dep || !apdu || apdu_len == 0 || !out) return HB_NFC_ERR_PARAM;

    int len = iso_dep_apdu_transceive(dep, apdu, apdu_len, out, out_max, 0);
    if (len < 2) return HB_NFC_ERR_PROTOCOL;

    uint16_t status = (uint16_t)((out[len - 2] << 8) | out[len - 1]);
    if (sw) *sw = status;
    if (out_len) {
        *out_len = (size_t)(len - 2);
    }
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_select_ndef_app(const nfc_iso_dep_data_t* dep)
{
    uint8_t apdu[5 + T4T_AID_LEN + 1];
    size_t pos = 0;
    apdu[pos++] = 0x00;
    apdu[pos++] = 0xA4;
    apdu[pos++] = 0x04;
    apdu[pos++] = 0x00;
    apdu[pos++] = T4T_AID_LEN;
    memcpy(&apdu[pos], T4T_AID, T4T_AID_LEN);
    pos += T4T_AID_LEN;
    apdu[pos++] = 0x00; /* Le */

    uint8_t rsp[64] = { 0 };
    size_t  rsp_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = t4t_apdu(dep, apdu, pos, rsp, sizeof(rsp), &rsp_len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != 0x9000 && (sw & 0xFF00U) != 0x6100U) {
        ESP_LOGW(TAG, "SELECT NDEF APP failed SW=0x%04X", sw);
        return HB_NFC_ERR_PROTOCOL;
    }
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_select_file(const nfc_iso_dep_data_t* dep, uint16_t fid)
{
    uint8_t apdu[7];
    apdu[0] = 0x00;
    apdu[1] = 0xA4;
    apdu[2] = 0x00;
    apdu[3] = 0x0C; /* no FCI */
    apdu[4] = 0x02;
    apdu[5] = (uint8_t)((fid >> 8) & 0xFFU);
    apdu[6] = (uint8_t)(fid & 0xFFU);

    uint8_t rsp[32] = { 0 };
    size_t  rsp_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = t4t_apdu(dep, apdu, sizeof(apdu), rsp, sizeof(rsp), &rsp_len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != 0x9000) {
        ESP_LOGW(TAG, "SELECT FILE 0x%04X failed SW=0x%04X", fid, sw);
        return HB_NFC_ERR_PROTOCOL;
    }
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_read_binary(const nfc_iso_dep_data_t* dep,
                                     uint16_t                 offset,
                                     uint8_t*                 out,
                                     size_t                   out_len)
{
    if (!out || out_len == 0 || out_len > 0xFF) return HB_NFC_ERR_PARAM;

    uint8_t apdu[5];
    apdu[0] = 0x00;
    apdu[1] = 0xB0;
    apdu[2] = (uint8_t)((offset >> 8) & 0xFFU);
    apdu[3] = (uint8_t)(offset & 0xFFU);
    apdu[4] = (uint8_t)out_len;

    uint8_t rsp[260] = { 0 };
    size_t  rsp_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = t4t_apdu(dep, apdu, sizeof(apdu), rsp, sizeof(rsp), &rsp_len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != 0x9000) {
        ESP_LOGW(TAG, "READ_BINARY failed SW=0x%04X", sw);
        return HB_NFC_ERR_PROTOCOL;
    }
    if (rsp_len < out_len) return HB_NFC_ERR_PROTOCOL;
    memcpy(out, rsp, out_len);
    return HB_NFC_OK;
}

static hb_nfc_err_t t4t_update_binary(const nfc_iso_dep_data_t* dep,
                                       uint16_t                 offset,
                                       const uint8_t*           data,
                                       size_t                   data_len)
{
    if (!data || data_len == 0 || data_len > 0xFF) return HB_NFC_ERR_PARAM;

    uint8_t apdu[5 + 0xFF];
    apdu[0] = 0x00;
    apdu[1] = 0xD6;
    apdu[2] = (uint8_t)((offset >> 8) & 0xFFU);
    apdu[3] = (uint8_t)(offset & 0xFFU);
    apdu[4] = (uint8_t)data_len;
    memcpy(&apdu[5], data, data_len);

    uint8_t rsp[16] = { 0 };
    size_t  rsp_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = t4t_apdu(dep, apdu, 5 + data_len, rsp, sizeof(rsp), &rsp_len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != 0x9000) {
        ESP_LOGW(TAG, "UPDATE_BINARY failed SW=0x%04X", sw);
        return HB_NFC_ERR_PROTOCOL;
    }
    return HB_NFC_OK;
}

hb_nfc_err_t t4t_read_cc(const nfc_iso_dep_data_t* dep, t4t_cc_t* cc)
{
    if (!dep || !cc) return HB_NFC_ERR_PARAM;
    memset(cc, 0, sizeof(*cc));

    hb_nfc_err_t err = t4t_select_ndef_app(dep);
    if (err != HB_NFC_OK) return err;

    err = t4t_select_file(dep, T4T_CC_FILE);
    if (err != HB_NFC_OK) return err;

    uint8_t buf[15] = { 0 };
    err = t4t_read_binary(dep, 0x0000, buf, sizeof(buf));
    if (err != HB_NFC_OK) return err;

    cc->cc_len      = (uint16_t)((buf[0] << 8) | buf[1]);
    cc->mle         = (uint16_t)((buf[3] << 8) | buf[4]);
    cc->mlc         = (uint16_t)((buf[5] << 8) | buf[6]);
    cc->ndef_fid    = (uint16_t)((buf[9] << 8) | buf[10]);
    cc->ndef_max    = (uint16_t)((buf[11] << 8) | buf[12]);
    cc->read_access = buf[13];
    cc->write_access = buf[14];

    ESP_LOGI(TAG, "CC: ndef_fid=0x%04X ndef_max=%u mle=%u mlc=%u",
             cc->ndef_fid, cc->ndef_max, cc->mle, cc->mlc);
    return HB_NFC_OK;
}

hb_nfc_err_t t4t_read_ndef(const nfc_iso_dep_data_t* dep,
                            const t4t_cc_t*         cc,
                            uint8_t*               out,
                            size_t                 out_max,
                            size_t*                out_len)
{
    if (!dep || !cc || !out || out_max < 2) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = t4t_select_ndef_app(dep);
    if (err != HB_NFC_OK) return err;

    if (cc->read_access != 0x00U) {
        ESP_LOGW(TAG, "NDEF read not allowed (access=0x%02X)", cc->read_access);
        return HB_NFC_ERR_PROTOCOL;
    }

    err = t4t_select_file(dep, cc->ndef_fid);
    if (err != HB_NFC_OK) return err;

    uint8_t len_buf[2] = { 0 };
    err = t4t_read_binary(dep, 0x0000, len_buf, sizeof(len_buf));
    if (err != HB_NFC_OK) return err;

    uint16_t ndef_len = (uint16_t)((len_buf[0] << 8) | len_buf[1]);
    if (ndef_len == 0 || ndef_len > cc->ndef_max) {
        ESP_LOGW(TAG, "Invalid NDEF length %u", ndef_len);
        return HB_NFC_ERR_PROTOCOL;
    }
    if ((size_t)ndef_len > out_max) {
        ESP_LOGW(TAG, "NDEF too large (%u > %u)", ndef_len, (unsigned)out_max);
        return HB_NFC_ERR_PARAM;
    }

    uint16_t offset = 2;
    uint16_t remaining = ndef_len;
    uint16_t mle = cc->mle ? cc->mle : 0xFFU;
    if (mle > 0xFFU) mle = 0xFFU;

    while (remaining > 0) {
        uint16_t chunk = remaining;
        if (chunk > mle) chunk = mle;

        err = t4t_read_binary(dep, offset, &out[ndef_len - remaining], chunk);
        if (err != HB_NFC_OK) return err;

        offset += chunk;
        remaining -= chunk;
    }

    if (out_len) *out_len = ndef_len;
    return HB_NFC_OK;
}

hb_nfc_err_t t4t_write_ndef(const nfc_iso_dep_data_t* dep,
                             const t4t_cc_t*         cc,
                             const uint8_t*         data,
                             size_t                 data_len)
{
    if (!dep || !cc || !data) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = t4t_select_ndef_app(dep);
    if (err != HB_NFC_OK) return err;

    if (cc->write_access != 0x00U) {
        ESP_LOGW(TAG, "NDEF write not allowed (access=0x%02X)", cc->write_access);
        return HB_NFC_ERR_PROTOCOL;
    }

    if (data_len > cc->ndef_max) {
        ESP_LOGW(TAG, "NDEF too large (%u > %u)",
                 (unsigned)data_len, (unsigned)cc->ndef_max);
        return HB_NFC_ERR_PARAM;
    }

    err = t4t_select_file(dep, cc->ndef_fid);
    if (err != HB_NFC_OK) return err;

    /* Set NLEN = 0 first (atomic update pattern) */
    uint8_t zero[2] = { 0x00, 0x00 };
    err = t4t_update_binary(dep, 0x0000, zero, sizeof(zero));
    if (err != HB_NFC_OK) return err;

    uint16_t mlc = cc->mlc ? cc->mlc : 0xFFU;
    if (mlc > 0xFFU) mlc = 0xFFU;

    uint16_t offset = 2;
    size_t remaining = data_len;
    while (remaining > 0) {
        uint16_t chunk = (remaining > mlc) ? mlc : (uint16_t)remaining;
        err = t4t_update_binary(dep, offset, data + (data_len - remaining), chunk);
        if (err != HB_NFC_OK) return err;
        offset += chunk;
        remaining -= chunk;
    }

    /* Write NLEN actual */
    uint8_t nlen[2];
    nlen[0] = (uint8_t)((data_len >> 8) & 0xFFU);
    nlen[1] = (uint8_t)(data_len & 0xFFU);
    return t4t_update_binary(dep, 0x0000, nlen, sizeof(nlen));
}

hb_nfc_err_t t4t_read_binary_ndef(const nfc_iso_dep_data_t* dep,
                                   const t4t_cc_t*         cc,
                                   uint16_t               offset,
                                   uint8_t*               out,
                                   size_t                 out_len)
{
    if (!dep || !cc || !out || out_len == 0) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = t4t_select_ndef_app(dep);
    if (err != HB_NFC_OK) return err;

    if (cc->read_access != 0x00U) return HB_NFC_ERR_PROTOCOL;

    err = t4t_select_file(dep, cc->ndef_fid);
    if (err != HB_NFC_OK) return err;

    if ((size_t)offset + out_len > (size_t)cc->ndef_max + 2U) {
        return HB_NFC_ERR_PARAM;
    }
    return t4t_read_binary(dep, offset, out, out_len);
}

hb_nfc_err_t t4t_update_binary_ndef(const nfc_iso_dep_data_t* dep,
                                     const t4t_cc_t*         cc,
                                     uint16_t               offset,
                                     const uint8_t*         data,
                                     size_t                 data_len)
{
    if (!dep || !cc || !data || data_len == 0) return HB_NFC_ERR_PARAM;

    hb_nfc_err_t err = t4t_select_ndef_app(dep);
    if (err != HB_NFC_OK) return err;

    if (cc->write_access != 0x00U) return HB_NFC_ERR_PROTOCOL;

    err = t4t_select_file(dep, cc->ndef_fid);
    if (err != HB_NFC_OK) return err;

    if ((size_t)offset + data_len > (size_t)cc->ndef_max + 2U) {
        return HB_NFC_ERR_PARAM;
    }
    return t4t_update_binary(dep, offset, data, data_len);
}
