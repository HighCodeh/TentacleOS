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
#include "mf_desfire.h"

#include <string.h>
#include "esp_log.h"
#include "esp_random.h"

#include "iso_dep.h"
#include "nfc_crypto.h"
#include "mbedtls/aes.h"
#include "mbedtls/des.h"

#define TAG "mf_desfire"

static int build_native_apdu(uint8_t cmd,
                             const uint8_t* data,
                             size_t data_len,
                             uint8_t* apdu,
                             size_t apdu_max)
{
    size_t need = 5 + data_len + 1;
    if (!apdu || apdu_max < need) return 0;
    apdu[0] = MF_DESFIRE_CLA;
    apdu[1] = cmd;
    apdu[2] = 0x00;
    apdu[3] = 0x00;
    apdu[4] = (uint8_t)data_len;
    if (data_len > 0 && data) {
        memcpy(&apdu[5], data, data_len);
    }
    apdu[5 + data_len] = 0x00; /* Le */
    return (int)need;
}

static void rand_bytes(uint8_t* out, size_t len)
{
    if (!out || len == 0) return;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = esp_random();
        size_t left = len - i;
        size_t n = left < 4 ? left : 4;
        memcpy(&out[i], &r, n);
    }
}

static void rotate_left_1(uint8_t* out, const uint8_t* in, size_t len)
{
    if (!out || !in || len == 0) return;
    for (size_t i = 0; i < len - 1; i++) out[i] = in[i + 1];
    out[len - 1] = in[0];
}

static bool des3_cbc_crypt(bool encrypt,
                            const uint8_t key[16],
                            const uint8_t iv_in[8],
                            const uint8_t* in,
                            size_t len,
                            uint8_t* out,
                            uint8_t iv_out[8])
{
    if (!key || !iv_in || !in || !out || (len % 8) != 0) return false;

    mbedtls_des3_context ctx;
    mbedtls_des3_init(&ctx);
    int rc = encrypt ? mbedtls_des3_set2key_enc(&ctx, key)
                     : mbedtls_des3_set2key_dec(&ctx, key);
    if (rc != 0) {
        mbedtls_des3_free(&ctx);
        return false;
    }

    uint8_t iv[8];
    memcpy(iv, iv_in, 8);
    rc = mbedtls_des3_crypt_cbc(&ctx, encrypt ? MBEDTLS_DES_ENCRYPT : MBEDTLS_DES_DECRYPT,
                                len, iv, in, out);
    if (iv_out) memcpy(iv_out, iv, 8);
    mbedtls_des3_free(&ctx);
    return rc == 0;
}

static bool aes_cbc_crypt(bool encrypt,
                           const uint8_t key[16],
                           const uint8_t iv_in[16],
                           const uint8_t* in,
                           size_t len,
                           uint8_t* out,
                           uint8_t iv_out[16])
{
    if (!key || !iv_in || !in || !out || (len % 16) != 0) return false;

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int rc = encrypt ? mbedtls_aes_setkey_enc(&ctx, key, 128)
                     : mbedtls_aes_setkey_dec(&ctx, key, 128);
    if (rc != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }

    uint8_t iv[16];
    memcpy(iv, iv_in, 16);
    rc = mbedtls_aes_crypt_cbc(&ctx,
                               encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                               len, iv, in, out);
    if (iv_out) memcpy(iv_out, iv, 16);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

static int desfire_send_native_once(mf_desfire_session_t* session,
                                     uint8_t              cmd,
                                     const uint8_t*       data,
                                     size_t               data_len,
                                     uint8_t*             out,
                                     size_t               out_max,
                                     uint16_t*            sw)
{
    if (!session || !session->dep_ready || !out) return 0;

    uint8_t apdu[300];
    int apdu_len = build_native_apdu(cmd, data, data_len, apdu, sizeof(apdu));
    if (apdu_len <= 0) return 0;

    int rlen = mf_desfire_apdu_transceive(session, apdu, (size_t)apdu_len, out, out_max, 0);
    if (rlen < 2) return 0;

    if (sw) *sw = (uint16_t)((out[rlen - 2] << 8) | out[rlen - 1]);
    return rlen - 2;
}

hb_nfc_err_t mf_desfire_poller_init(const nfc_iso14443a_data_t* card,
                                     mf_desfire_session_t*       session)
{
    if (!card || !session) return HB_NFC_ERR_PARAM;
    memset(session, 0, sizeof(*session));

    hb_nfc_err_t err = iso_dep_rats(8, 0, &session->dep);
    if (err == HB_NFC_OK) {
        session->dep_ready = true;
    }
    return err;
}

int mf_desfire_apdu_transceive(mf_desfire_session_t* session,
                                const uint8_t*       apdu,
                                size_t               apdu_len,
                                uint8_t*             rx,
                                size_t               rx_max,
                                int                  timeout_ms)
{
    if (!session || !session->dep_ready) return 0;
    return iso_dep_apdu_transceive(&session->dep, apdu, apdu_len, rx, rx_max, timeout_ms);
}

hb_nfc_err_t mf_desfire_transceive_native(mf_desfire_session_t* session,
                                           uint8_t              cmd,
                                           const uint8_t*       data,
                                           size_t               data_len,
                                           uint8_t*             out,
                                           size_t               out_max,
                                           size_t*              out_len,
                                           uint16_t*            sw)
{
    if (!session || !session->dep_ready || !out || !out_len) return HB_NFC_ERR_PARAM;

    *out_len = 0;
    if (sw) *sw = 0;

    uint8_t apdu[300];
    int apdu_len = build_native_apdu(cmd, data, data_len, apdu, sizeof(apdu));
    if (apdu_len <= 0) return HB_NFC_ERR_PARAM;

    uint8_t rx[300];
    int rlen = mf_desfire_apdu_transceive(session, apdu, (size_t)apdu_len, rx, sizeof(rx), 0);
    if (rlen < 2) return HB_NFC_ERR_PROTOCOL;

    while (1) {
        uint16_t status = (uint16_t)((rx[rlen - 2] << 8) | rx[rlen - 1]);
        int data_len_rx = rlen - 2;

        if (data_len_rx > 0) {
            size_t copy = (*out_len + (size_t)data_len_rx <= out_max)
                            ? (size_t)data_len_rx
                            : (out_max > *out_len ? (out_max - *out_len) : 0);
            if (copy > 0) {
                memcpy(&out[*out_len], rx, copy);
                *out_len += copy;
            }
        }

        if (sw) *sw = status;
        if (status != MF_DESFIRE_SW_MORE) break;

        apdu_len = build_native_apdu(MF_DESFIRE_CMD_ADDITIONAL, NULL, 0, apdu, sizeof(apdu));
        if (apdu_len <= 0) return HB_NFC_ERR_PARAM;
        rlen = mf_desfire_apdu_transceive(session, apdu, (size_t)apdu_len, rx, sizeof(rx), 0);
        if (rlen < 2) return HB_NFC_ERR_PROTOCOL;
    }

    return HB_NFC_OK;
}

hb_nfc_err_t mf_desfire_get_version(mf_desfire_session_t* session,
                                     mf_desfire_version_t* out)
{
    if (!session || !out) return HB_NFC_ERR_PARAM;

    uint8_t buf[64];
    size_t len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_GET_VERSION,
                                                    NULL, 0, buf, sizeof(buf), &len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != MF_DESFIRE_SW_OK) return HB_NFC_ERR_PROTOCOL;
    if (len < (7 + 7 + 7 + 7)) return HB_NFC_ERR_PROTOCOL;

    memcpy(out->hw, &buf[0], 7);
    memcpy(out->sw, &buf[7], 7);
    memcpy(out->uid, &buf[14], 7);
    memcpy(out->batch, &buf[21], 7);
    return HB_NFC_OK;
}

hb_nfc_err_t mf_desfire_select_application(mf_desfire_session_t* session,
                                            const mf_desfire_aid_t* aid)
{
    if (!session || !aid) return HB_NFC_ERR_PARAM;
    uint8_t buf[16];
    size_t len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_SELECT_APP,
                                                    aid->aid, sizeof(aid->aid),
                                                    buf, sizeof(buf), &len, &sw);
    if (err != HB_NFC_OK) return err;
    return (sw == MF_DESFIRE_SW_OK) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t mf_desfire_authenticate_ev1_3des(mf_desfire_session_t* session,
                                               uint8_t              key_no,
                                               const uint8_t        key[16])
{
    if (!session || !session->dep_ready || !key) return HB_NFC_ERR_PARAM;
    session->authenticated = false;

    uint8_t data_in[32];
    uint16_t sw = 0;
    int len = desfire_send_native_once(session, MF_DESFIRE_CMD_AUTH_3DES,
                                       &key_no, 1, data_in, sizeof(data_in), &sw);
    if (len != 8 || sw != MF_DESFIRE_SW_MORE) return HB_NFC_ERR_PROTOCOL;

    uint8_t iv0[8] = { 0 };
    uint8_t rndB[8];
    if (!des3_cbc_crypt(false, key, iv0, data_in, 8, rndB, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rndA[8];
    rand_bytes(rndA, sizeof(rndA));
    uint8_t rndB_rot[8];
    rotate_left_1(rndB_rot, rndB, sizeof(rndB));

    uint8_t plain[16];
    memcpy(plain, rndA, 8);
    memcpy(&plain[8], rndB_rot, 8);

    uint8_t enc2[16];
    uint8_t iv2[8];
    if (!des3_cbc_crypt(true, key, data_in, plain, sizeof(plain), enc2, iv2)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t data_out[32];
    len = desfire_send_native_once(session, MF_DESFIRE_CMD_ADDITIONAL,
                                   enc2, sizeof(enc2), data_out, sizeof(data_out), &sw);
    if (len != 8 || sw != MF_DESFIRE_SW_OK) return HB_NFC_ERR_PROTOCOL;

    uint8_t rndA_rot[8];
    if (!des3_cbc_crypt(false, key, iv2, data_out, 8, rndA_rot, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rndA_rot_exp[8];
    rotate_left_1(rndA_rot_exp, rndA, sizeof(rndA));
    if (nfc_memcmp_ct(rndA_rot, rndA_rot_exp, 8) != 0) return HB_NFC_ERR_AUTH;

    /* Session key: A0..3 || B0..3 || A4..7 || B4..7 */
    session->session_key[0] = rndA[0];
    session->session_key[1] = rndA[1];
    session->session_key[2] = rndA[2];
    session->session_key[3] = rndA[3];
    session->session_key[4] = rndB[0];
    session->session_key[5] = rndB[1];
    session->session_key[6] = rndB[2];
    session->session_key[7] = rndB[3];
    session->session_key[8] = rndA[4];
    session->session_key[9] = rndA[5];
    session->session_key[10] = rndA[6];
    session->session_key[11] = rndA[7];
    session->session_key[12] = rndB[4];
    session->session_key[13] = rndB[5];
    session->session_key[14] = rndB[6];
    session->session_key[15] = rndB[7];

    memcpy(session->iv, iv2, 8);
    memset(&session->iv[8], 0, 8);
    session->authenticated = true;
    session->key_type = MF_DESFIRE_KEY_2K3DES;
    session->key_no = key_no;
    return HB_NFC_OK;
}

hb_nfc_err_t mf_desfire_authenticate_ev1_aes(mf_desfire_session_t* session,
                                              uint8_t              key_no,
                                              const uint8_t        key[16])
{
    if (!session || !session->dep_ready || !key) return HB_NFC_ERR_PARAM;
    session->authenticated = false;

    uint8_t data_in[48];
    uint16_t sw = 0;
    int len = desfire_send_native_once(session, MF_DESFIRE_CMD_AUTH_AES,
                                       &key_no, 1, data_in, sizeof(data_in), &sw);
    if (len != 16 || sw != MF_DESFIRE_SW_MORE) return HB_NFC_ERR_PROTOCOL;

    uint8_t iv0[16] = { 0 };
    uint8_t rndB[16];
    if (!aes_cbc_crypt(false, key, iv0, data_in, 16, rndB, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rndA[16];
    rand_bytes(rndA, sizeof(rndA));
    uint8_t rndB_rot[16];
    rotate_left_1(rndB_rot, rndB, sizeof(rndB));

    uint8_t plain[32];
    memcpy(plain, rndA, 16);
    memcpy(&plain[16], rndB_rot, 16);

    uint8_t enc2[32];
    uint8_t iv2[16];
    if (!aes_cbc_crypt(true, key, data_in, plain, sizeof(plain), enc2, iv2)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t data_out[48];
    len = desfire_send_native_once(session, MF_DESFIRE_CMD_ADDITIONAL,
                                   enc2, sizeof(enc2), data_out, sizeof(data_out), &sw);
    if (len != 16 || sw != MF_DESFIRE_SW_OK) return HB_NFC_ERR_PROTOCOL;

    uint8_t rndA_rot[16];
    if (!aes_cbc_crypt(false, key, iv2, data_out, 16, rndA_rot, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rndA_rot_exp[16];
    rotate_left_1(rndA_rot_exp, rndA, sizeof(rndA));
    if (nfc_memcmp_ct(rndA_rot, rndA_rot_exp, 16) != 0) return HB_NFC_ERR_AUTH;

    /* Session key: A0..3 || B0..3 || A12..15 || B12..15 */
    session->session_key[0] = rndA[0];
    session->session_key[1] = rndA[1];
    session->session_key[2] = rndA[2];
    session->session_key[3] = rndA[3];
    session->session_key[4] = rndB[0];
    session->session_key[5] = rndB[1];
    session->session_key[6] = rndB[2];
    session->session_key[7] = rndB[3];
    session->session_key[8] = rndA[12];
    session->session_key[9] = rndA[13];
    session->session_key[10] = rndA[14];
    session->session_key[11] = rndA[15];
    session->session_key[12] = rndB[12];
    session->session_key[13] = rndB[13];
    session->session_key[14] = rndB[14];
    session->session_key[15] = rndB[15];

    memcpy(session->iv, iv2, 16);
    session->authenticated = true;
    session->key_type = MF_DESFIRE_KEY_AES;
    session->key_no = key_no;
    return HB_NFC_OK;
}

hb_nfc_err_t mf_desfire_authenticate_ev2_first(mf_desfire_session_t* session,
                                                uint8_t              key_no,
                                                const uint8_t        key[16])
{
    if (!session || !session->dep_ready || !key) return HB_NFC_ERR_PARAM;
    session->authenticated = false;
    session->ev2_authenticated = false;

    uint8_t data_in[64];
    uint16_t sw = 0;
    uint8_t cmd_data[2] = { key_no, 0x00 };
    int len = desfire_send_native_once(session, MF_DESFIRE_CMD_AUTH_EV2_FIRST,
                                       cmd_data, sizeof(cmd_data),
                                       data_in, sizeof(data_in), &sw);
    if (len != 16 || sw != MF_DESFIRE_SW_MORE) return HB_NFC_ERR_PROTOCOL;

    uint8_t iv0[16] = { 0 };
    uint8_t rndB[16];
    if (!aes_cbc_crypt(false, key, iv0, data_in, 16, rndB, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rndA[16];
    rand_bytes(rndA, sizeof(rndA));
    uint8_t rndB_rot[16];
    rotate_left_1(rndB_rot, rndB, sizeof(rndB));

    uint8_t plain[32];
    memcpy(plain, rndA, 16);
    memcpy(&plain[16], rndB_rot, 16);

    uint8_t enc2[32];
    if (!aes_cbc_crypt(true, key, iv0, plain, sizeof(plain), enc2, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t resp[64];
    len = desfire_send_native_once(session, MF_DESFIRE_CMD_ADDITIONAL,
                                   enc2, sizeof(enc2), resp, sizeof(resp), &sw);
    if (len != 32 || sw != MF_DESFIRE_SW_OK) return HB_NFC_ERR_PROTOCOL;

    uint8_t resp_plain[32];
    if (!aes_cbc_crypt(false, key, iv0, resp, 32, resp_plain, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    memcpy(session->ev2_ti, resp_plain, 4);

    uint8_t rndA_rot[16];
    rotate_left_1(rndA_rot, rndA, sizeof(rndA));
    if (nfc_memcmp_ct(rndA_rot, resp_plain + 4, 16) != 0) return HB_NFC_ERR_AUTH;

    memcpy(session->ev2_pd_cap2, resp_plain + 20, 6);
    memcpy(session->ev2_pcd_cap2, resp_plain + 26, 6);

    session->ev2_cmd_ctr = 0;
    nfc_ev2_derive_session_keys(key, rndA, rndB,
                                session->ev2_ses_mac,
                                session->ev2_ses_enc);
    session->authenticated = true;
    session->ev2_authenticated = true;
    session->key_type = MF_DESFIRE_KEY_AES;
    session->key_no = key_no;
    return HB_NFC_OK;
}

hb_nfc_err_t mf_desfire_authenticate_ev2_nonfirst(mf_desfire_session_t* session,
                                                   uint8_t              key_no,
                                                   const uint8_t        key[16])
{
    if (!session || !session->dep_ready || !key) return HB_NFC_ERR_PARAM;
    if (!session->ev2_authenticated) return HB_NFC_ERR_AUTH;

    uint8_t data_in[64];
    uint16_t sw = 0;
    int len = desfire_send_native_once(session, MF_DESFIRE_CMD_AUTH_EV2_NONFIRST,
                                       &key_no, 1, data_in, sizeof(data_in), &sw);
    if (len != 16 || sw != MF_DESFIRE_SW_MORE) return HB_NFC_ERR_PROTOCOL;

    uint8_t iv0[16] = { 0 };
    uint8_t rndB[16];
    if (!aes_cbc_crypt(false, key, iv0, data_in, 16, rndB, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rndA[16];
    rand_bytes(rndA, sizeof(rndA));
    uint8_t rndB_rot[16];
    rotate_left_1(rndB_rot, rndB, sizeof(rndB));

    uint8_t plain[32];
    memcpy(plain, rndA, 16);
    memcpy(&plain[16], rndB_rot, 16);

    uint8_t enc2[32];
    if (!aes_cbc_crypt(true, key, iv0, plain, sizeof(plain), enc2, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t resp[64];
    len = desfire_send_native_once(session, MF_DESFIRE_CMD_ADDITIONAL,
                                   enc2, sizeof(enc2), resp, sizeof(resp), &sw);
    if (len != 32 || sw != MF_DESFIRE_SW_OK) return HB_NFC_ERR_PROTOCOL;

    uint8_t resp_plain[32];
    if (!aes_cbc_crypt(false, key, iv0, resp, 32, resp_plain, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    if (nfc_memcmp_ct(resp_plain, session->ev2_ti, 4) != 0) return HB_NFC_ERR_AUTH;

    uint8_t rndA_rot[16];
    rotate_left_1(rndA_rot, rndA, sizeof(rndA));
    if (nfc_memcmp_ct(rndA_rot, resp_plain + 4, 16) != 0) return HB_NFC_ERR_AUTH;

    nfc_ev2_derive_session_keys(key, rndA, rndB,
                                session->ev2_ses_mac,
                                session->ev2_ses_enc);
    session->authenticated = true;
    session->ev2_authenticated = true;
    session->key_type = MF_DESFIRE_KEY_AES;
    session->key_no = key_no;
    return HB_NFC_OK;
}

hb_nfc_err_t mf_desfire_get_application_ids(mf_desfire_session_t* session,
                                             uint8_t*             out,
                                             size_t               out_max,
                                             size_t*              out_len)
{
    if (!session || !out || !out_len) return HB_NFC_ERR_PARAM;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_GET_APP_IDS,
                                                    NULL, 0, out, out_max, out_len, &sw);
    if (err != HB_NFC_OK) return err;
    return (sw == MF_DESFIRE_SW_OK) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t mf_desfire_get_file_ids(mf_desfire_session_t* session,
                                      uint8_t*             out,
                                      size_t               out_max,
                                      size_t*              out_len)
{
    if (!session || !out || !out_len) return HB_NFC_ERR_PARAM;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_GET_FILE_IDS,
                                                    NULL, 0, out, out_max, out_len, &sw);
    if (err != HB_NFC_OK) return err;
    return (sw == MF_DESFIRE_SW_OK) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t mf_desfire_get_key_settings(mf_desfire_session_t* session,
                                          uint8_t*             key_settings,
                                          uint8_t*             max_keys)
{
    if (!session || !key_settings || !max_keys) return HB_NFC_ERR_PARAM;
    uint8_t buf[8];
    size_t len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_GET_KEY_SET,
                                                    NULL, 0, buf, sizeof(buf), &len, &sw);
    if (err != HB_NFC_OK) return err;
    if (sw != MF_DESFIRE_SW_OK || len < 2) return HB_NFC_ERR_PROTOCOL;
    *key_settings = buf[0];
    *max_keys = buf[1];
    return HB_NFC_OK;
}

hb_nfc_err_t mf_desfire_read_data(mf_desfire_session_t* session,
                                   uint8_t              file_id,
                                   uint32_t             offset,
                                   uint32_t             length,
                                   uint8_t*             out,
                                   size_t               out_max,
                                   size_t*              out_len)
{
    if (!session || !out || !out_len) return HB_NFC_ERR_PARAM;
    uint8_t params[7];
    params[0] = file_id;
    params[1] = (uint8_t)(offset & 0xFF);
    params[2] = (uint8_t)((offset >> 8) & 0xFF);
    params[3] = (uint8_t)((offset >> 16) & 0xFF);
    params[4] = (uint8_t)(length & 0xFF);
    params[5] = (uint8_t)((length >> 8) & 0xFF);
    params[6] = (uint8_t)((length >> 16) & 0xFF);
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_READ_DATA,
                                                    params, sizeof(params),
                                                    out, out_max, out_len, &sw);
    if (err != HB_NFC_OK) return err;
    return (sw == MF_DESFIRE_SW_OK) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t mf_desfire_write_data(mf_desfire_session_t* session,
                                    uint8_t              file_id,
                                    uint32_t             offset,
                                    const uint8_t*       data,
                                    size_t               data_len)
{
    if (!session || !data) return HB_NFC_ERR_PARAM;
    /* Single-frame limit: 7-byte header + data must fit in the 294-byte APDU payload. */
    if (data_len > 256) return HB_NFC_ERR_PARAM;
    uint8_t params[7 + 256];
    params[0] = file_id;
    params[1] = (uint8_t)(offset & 0xFF);
    params[2] = (uint8_t)((offset >> 8) & 0xFF);
    params[3] = (uint8_t)((offset >> 16) & 0xFF);
    params[4] = (uint8_t)(data_len & 0xFF);
    params[5] = (uint8_t)((data_len >> 8) & 0xFF);
    params[6] = (uint8_t)((data_len >> 16) & 0xFF);
    memcpy(&params[7], data, data_len);
    uint8_t rx[8];
    size_t rx_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_WRITE_DATA,
                                                    params, 7 + data_len,
                                                    rx, sizeof(rx), &rx_len, &sw);
    if (err != HB_NFC_OK) return err;
    return (sw == MF_DESFIRE_SW_OK) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t mf_desfire_commit_transaction(mf_desfire_session_t* session)
{
    if (!session) return HB_NFC_ERR_PARAM;
    uint8_t rx[8];
    size_t rx_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_COMMIT_TRANSACTION,
                                                    NULL, 0, rx, sizeof(rx), &rx_len, &sw);
    if (err != HB_NFC_OK) return err;
    return (sw == MF_DESFIRE_SW_OK) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t mf_desfire_abort_transaction(mf_desfire_session_t* session)
{
    if (!session) return HB_NFC_ERR_PARAM;
    uint8_t rx[8];
    size_t rx_len = 0;
    uint16_t sw = 0;
    hb_nfc_err_t err = mf_desfire_transceive_native(session, MF_DESFIRE_CMD_ABORT_TRANSACTION,
                                                    NULL, 0, rx, sizeof(rx), &rx_len, &sw);
    if (err != HB_NFC_OK) return err;
    return (sw == MF_DESFIRE_SW_OK) ? HB_NFC_OK : HB_NFC_ERR_PROTOCOL;
}

#undef TAG
