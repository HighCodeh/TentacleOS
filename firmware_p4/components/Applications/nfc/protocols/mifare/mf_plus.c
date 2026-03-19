/**
 * @file mf_plus.c
 * @brief MiFARE Plus SL3 auth and crypto helpers.
 */
#include "mf_plus.h"

#include <string.h>

#include "nfc_crypto.h"
#include "esp_log.h"
#include "esp_random.h"

#define TAG "mf_plus"

#define MFP_CLA         0x90
#define MFP_SW_OK       0x9100
#define MFP_SW_MORE     0x91AF

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

hb_nfc_err_t mfp_poller_init(const nfc_iso14443a_data_t* card,
                             mfp_session_t*             session)
{
    if (!card || !session) return HB_NFC_ERR_PARAM;
    memset(session, 0, sizeof(*session));
    hb_nfc_err_t err = iso_dep_rats(8, 0, &session->dep);
    if (err == HB_NFC_OK) session->dep_ready = true;
    return err;
}

int mfp_apdu_transceive(mfp_session_t* session,
                        const uint8_t*  apdu,
                        size_t          apdu_len,
                        uint8_t*        rx,
                        size_t          rx_max,
                        int             timeout_ms)
{
    if (!session || !session->dep_ready) return 0;
    return iso_dep_apdu_transceive(&session->dep, apdu, apdu_len, rx, rx_max, timeout_ms);
}

uint16_t mfp_key_block_addr(uint8_t sector, bool key_b)
{
    return (uint16_t)(0x4000U + (uint16_t)sector * 2U + (key_b ? 1U : 0U));
}

void mfp_sl3_derive_session_keys(const uint8_t* rnd_a,
                                 const uint8_t* rnd_b,
                                 uint8_t*       ses_enc,
                                 uint8_t*       ses_auth)
{
    if (!rnd_a || !rnd_b || !ses_enc || !ses_auth) return;
    memcpy(ses_enc +  0, rnd_a +  0, 4);
    memcpy(ses_enc +  4, rnd_b +  0, 4);
    memcpy(ses_enc +  8, rnd_a +  8, 4);
    memcpy(ses_enc + 12, rnd_b +  8, 4);

    memcpy(ses_auth +  0, rnd_a +  4, 4);
    memcpy(ses_auth +  4, rnd_b +  4, 4);
    memcpy(ses_auth +  8, rnd_a + 12, 4);
    memcpy(ses_auth + 12, rnd_b + 12, 4);
}

static hb_nfc_err_t mfp_sl3_auth(uint8_t ins,
                                 mfp_session_t* session,
                                 uint16_t block_addr,
                                 const uint8_t key[16])
{
    if (!session || !session->dep_ready || !key) return HB_NFC_ERR_PARAM;
    session->authenticated = false;

    uint8_t cmd1[9] = {
        MFP_CLA, ins, 0x00, 0x00, 0x03,
        (uint8_t)(block_addr >> 8),
        (uint8_t)(block_addr & 0xFF),
        0x00,
        0x00
    };

    uint8_t resp[64];
    int rlen = mfp_apdu_transceive(session, cmd1, sizeof(cmd1),
                                   resp, sizeof(resp), 0);
    if (rlen < 2) return HB_NFC_ERR_PROTOCOL;
    uint16_t sw = (uint16_t)((resp[rlen - 2] << 8) | resp[rlen - 1]);
    int data_len = rlen - 2;
    if (data_len != 16 || sw != MFP_SW_MORE) return HB_NFC_ERR_PROTOCOL;

    uint8_t rndB[16];
    uint8_t iv0[16] = {0};
    if (!nfc_aes_cbc_crypt(false, key, iv0, resp, 16, rndB, NULL)) {
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
    if (!nfc_aes_cbc_crypt(true, key, iv0, plain, sizeof(plain), enc2, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t cmd2[38] = { MFP_CLA, 0xAF, 0x00, 0x00, 0x20 };
    memcpy(cmd2 + 5, enc2, 32);
    cmd2[37] = 0x00;

    rlen = mfp_apdu_transceive(session, cmd2, sizeof(cmd2),
                               resp, sizeof(resp), 0);
    if (rlen < 2) return HB_NFC_ERR_PROTOCOL;
    sw = (uint16_t)((resp[rlen - 2] << 8) | resp[rlen - 1]);
    data_len = rlen - 2;
    if (data_len != 16 || sw != MFP_SW_OK) return HB_NFC_ERR_PROTOCOL;

    uint8_t rndA_prime[16];
    if (!nfc_aes_cbc_crypt(false, key, iv0, resp, 16, rndA_prime, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rndA_rot[16];
    rotate_left_1(rndA_rot, rndA, sizeof(rndA));
    if (nfc_memcmp_ct(rndA_rot, rndA_prime, 16) != 0) return HB_NFC_ERR_AUTH;

    mfp_sl3_derive_session_keys(rndA, rndB, session->ses_enc, session->ses_auth);
    memset(session->iv_enc, 0, sizeof(session->iv_enc));
    memset(session->iv_mac, 0, sizeof(session->iv_mac));
    session->authenticated = true;
    return HB_NFC_OK;
}

hb_nfc_err_t mfp_sl3_auth_first(mfp_session_t* session,
                               uint16_t       block_addr,
                               const uint8_t  key[16])
{
    return mfp_sl3_auth(0x70, session, block_addr, key);
}

hb_nfc_err_t mfp_sl3_auth_nonfirst(mfp_session_t* session,
                                  uint16_t       block_addr,
                                  const uint8_t  key[16])
{
    return mfp_sl3_auth(0x76, session, block_addr, key);
}

int mfp_sl3_compute_mac(const mfp_session_t* session,
                        const uint8_t*       data,
                        size_t               data_len,
                        uint8_t              mac8[8])
{
    if (!session || !session->authenticated || !mac8) return -1;
    uint8_t full[16];
    if (nfc_aes_cmac(session->ses_auth, 16, data, data_len, full) != 0) return -1;
    memcpy(mac8, full, 8);
    return 0;
}

size_t mfp_sl3_encrypt(mfp_session_t* session,
                       const uint8_t* plain,
                       size_t plain_len,
                       uint8_t* out,
                       size_t out_max)
{
    if (!session || !session->authenticated || !out) return 0;
    size_t padded_len = nfc_iso9797_pad_method1(plain, plain_len, out, out_max);
    if (padded_len == 0) return 0;

    uint8_t iv_in[16];
    memcpy(iv_in, session->iv_enc, 16);
    if (!nfc_aes_cbc_crypt(true, session->ses_enc, iv_in,
                           out, padded_len, out, session->iv_enc)) {
        return 0;
    }
    return padded_len;
}

size_t mfp_sl3_decrypt(mfp_session_t* session,
                       const uint8_t* enc,
                       size_t enc_len,
                       uint8_t* out,
                       size_t out_max)
{
    if (!session || !session->authenticated || !enc || !out) return 0;
    if ((enc_len % 16) != 0 || enc_len > out_max) return 0;

    uint8_t iv_in[16];
    memcpy(iv_in, session->iv_enc, 16);
    if (!nfc_aes_cbc_crypt(false, session->ses_enc, iv_in,
                           enc, enc_len, out, session->iv_enc)) {
        return 0;
    }
    return enc_len;
}

#undef TAG
