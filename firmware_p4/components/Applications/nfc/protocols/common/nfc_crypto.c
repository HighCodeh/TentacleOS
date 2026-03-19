/**
 * @file nfc_crypto.c
 * @brief Common crypto helpers (CMAC, KDF, diversification).
 */
#include "nfc_crypto.h"

#include <string.h>
#include <stdlib.h>

#include "mbedtls/aes.h"
#include "mbedtls/des.h"

int nfc_memcmp_ct(const void* a, const void* b, size_t n)
{
    const uint8_t* pa = (const uint8_t*)a;
    const uint8_t* pb = (const uint8_t*)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (uint8_t)(pa[i] ^ pb[i]);
    return (int)diff;
}

bool nfc_aes_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    if (!key || !in || !out) return false;
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int rc = mbedtls_aes_setkey_enc(&ctx, key, 128);
    if (rc != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    rc = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

bool nfc_aes_cbc_crypt(bool encrypt,
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

static void leftshift_onebit_128(const uint8_t* in, uint8_t* out)
{
    for (int i = 0; i < 15; i++) {
        out[i] = (uint8_t)((in[i] << 1) | (in[i + 1] >> 7));
    }
    out[15] = (uint8_t)(in[15] << 1);
}

static void aes_cmac_subkeys(const uint8_t* key, uint8_t* k1, uint8_t* k2)
{
    static const uint8_t zero16[16] = {0};
    uint8_t L[16];
    (void)nfc_aes_ecb_encrypt(key, zero16, L);

    leftshift_onebit_128(L, k1);
    if (L[0] & 0x80U) k1[15] ^= 0x87U;

    leftshift_onebit_128(k1, k2);
    if (k1[0] & 0x80U) k2[15] ^= 0x87U;
}

int nfc_aes_cmac(const uint8_t* key,
                 size_t key_len,
                 const uint8_t* msg,
                 size_t msg_len,
                 uint8_t tag[16])
{
    if (!key || !msg || !tag || key_len != 16) return -1;

    uint8_t k1[16], k2[16];
    aes_cmac_subkeys(key, k1, k2);

    int n = (msg_len == 0) ? 1 : (int)((msg_len + 15U) / 16U);
    bool padded = (msg_len == 0) || (msg_len % 16U != 0);

    uint8_t X[16] = {0};
    uint8_t Y[16];

    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < 16; j++) Y[j] = msg[i * 16 + j] ^ X[j];
        (void)nfc_aes_ecb_encrypt(key, Y, X);
    }

    uint8_t last[16] = {0};
    size_t last_len = (msg_len == 0) ? 0 : (msg_len - (size_t)(n - 1) * 16U);
    if (last_len > 0) memcpy(last, msg + (size_t)(n - 1) * 16U, last_len);
    if (padded) last[last_len] = 0x80U;

    const uint8_t* subkey = padded ? k2 : k1;
    for (int j = 0; j < 16; j++) Y[j] = (uint8_t)(last[j] ^ subkey[j] ^ X[j]);
    (void)nfc_aes_ecb_encrypt(key, Y, tag);
    return 0;
}

static bool tdes_ecb_encrypt(const uint8_t* key, size_t key_len,
                             const uint8_t in[8], uint8_t out[8])
{
    if (!key || !in || !out) return false;
    if (key_len != 16 && key_len != 24) return false;

    mbedtls_des3_context ctx;
    mbedtls_des3_init(&ctx);
    int rc = (key_len == 16) ? mbedtls_des3_set2key_enc(&ctx, key)
                             : mbedtls_des3_set3key_enc(&ctx, key);
    if (rc != 0) {
        mbedtls_des3_free(&ctx);
        return false;
    }
    rc = mbedtls_des3_crypt_ecb(&ctx, in, out);
    mbedtls_des3_free(&ctx);
    return rc == 0;
}

static void leftshift_onebit_64(const uint8_t* in, uint8_t* out)
{
    for (int i = 0; i < 7; i++) {
        out[i] = (uint8_t)((in[i] << 1) | (in[i + 1] >> 7));
    }
    out[7] = (uint8_t)(in[7] << 1);
}

static int tdes_cmac_subkeys(const uint8_t* key, size_t key_len,
                             uint8_t* k1, uint8_t* k2)
{
    uint8_t L[8] = {0};
    if (!tdes_ecb_encrypt(key, key_len, L, L)) return -1;

    leftshift_onebit_64(L, k1);
    if (L[0] & 0x80U) k1[7] ^= 0x1BU;

    leftshift_onebit_64(k1, k2);
    if (k1[0] & 0x80U) k2[7] ^= 0x1BU;
    return 0;
}

static int nfc_tdes_cmac(const uint8_t* key, size_t key_len,
                         const uint8_t* msg, size_t msg_len,
                         uint8_t tag[8])
{
    if (!key || !msg || !tag) return -1;
    if (key_len != 16 && key_len != 24) return -1;

    uint8_t k1[8], k2[8];
    if (tdes_cmac_subkeys(key, key_len, k1, k2) != 0) return -1;

    int n = (msg_len == 0) ? 1 : (int)((msg_len + 7U) / 8U);
    bool padded = (msg_len == 0) || (msg_len % 8U != 0);

    uint8_t X[8] = {0};
    uint8_t Y[8];

    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < 8; j++) Y[j] = msg[i * 8 + j] ^ X[j];
        if (!tdes_ecb_encrypt(key, key_len, Y, X)) return -1;
    }

    uint8_t last[8] = {0};
    size_t last_len = (msg_len == 0) ? 0 : (msg_len - (size_t)(n - 1) * 8U);
    if (last_len > 0) memcpy(last, msg + (size_t)(n - 1) * 8U, last_len);
    if (padded) last[last_len] = 0x80U;

    const uint8_t* subkey = padded ? k2 : k1;
    for (int j = 0; j < 8; j++) Y[j] = (uint8_t)(last[j] ^ subkey[j] ^ X[j]);
    if (!tdes_ecb_encrypt(key, key_len, Y, tag)) return -1;
    return 0;
}

int nfc_tdes_cbc_mac(const uint8_t* key,
                     size_t key_len,
                     const uint8_t* data,
                     size_t data_len,
                     uint8_t mac_out[8])
{
    if (!key || !data || !mac_out) return -1;
    if (key_len != 16 && key_len != 24) return -1;

    mbedtls_des3_context ctx;
    mbedtls_des3_init(&ctx);
    int rc = (key_len == 16) ? mbedtls_des3_set2key_enc(&ctx, key)
                             : mbedtls_des3_set3key_enc(&ctx, key);
    if (rc != 0) {
        mbedtls_des3_free(&ctx);
        return -1;
    }

    uint8_t iv[8] = {0};
    size_t n_blocks = (data_len + 7U) / 8U;
    if (n_blocks == 0) n_blocks = 1;

    for (size_t i = 0; i < n_blocks; i++) {
        uint8_t block[8] = {0};
        size_t copy = (i * 8 + 8 <= data_len) ? 8 : (data_len - i * 8);
        if (copy > 0) memcpy(block, data + i * 8, copy);

        uint8_t out[8];
        rc = mbedtls_des3_crypt_cbc(&ctx, MBEDTLS_DES_ENCRYPT, 8, iv, block, out);
        if (rc != 0) {
            mbedtls_des3_free(&ctx);
            return -1;
        }
    }

    memcpy(mac_out, iv, 8);
    mbedtls_des3_free(&ctx);
    return 0;
}

int nfc_mac_compute(nfc_mac_type_t type,
                    const uint8_t* key,
                    size_t key_len,
                    const uint8_t* data,
                    size_t data_len,
                    uint8_t* mac_out,
                    size_t* mac_len_out)
{
    if (!key || !data || !mac_out || !mac_len_out) return -1;

    uint8_t full[16];
    switch (type) {
    case NFC_MAC_AES_CMAC_16:
        if (nfc_aes_cmac(key, key_len, data, data_len, full) != 0) return -1;
        memcpy(mac_out, full, 16); *mac_len_out = 16; break;

    case NFC_MAC_AES_CMAC_EV1_8:
        if (nfc_aes_cmac(key, key_len, data, data_len, full) != 0) return -1;
        memcpy(mac_out, full, 8); *mac_len_out = 8; break;

    case NFC_MAC_AES_CMAC_EV2_8:
        if (nfc_aes_cmac(key, key_len, data, data_len, full) != 0) return -1;
        for (int i = 0; i < 8; i++) mac_out[i] = full[1 + 2 * i];
        *mac_len_out = 8; break;

    case NFC_MAC_3DES_CBC_8:
        if (nfc_tdes_cbc_mac(key, key_len, data, data_len, full) != 0) return -1;
        memcpy(mac_out, full, 8); *mac_len_out = 8; break;

    case NFC_MAC_3DES_CBC_4:
        if (nfc_tdes_cbc_mac(key, key_len, data, data_len, full) != 0) return -1;
        memcpy(mac_out, full, 4); *mac_len_out = 4; break;

    default:
        return -1;
    }
    return 0;
}

int nfc_mac_verify(nfc_mac_type_t type,
                   const uint8_t* key,
                   size_t key_len,
                   const uint8_t* data,
                   size_t data_len,
                   const uint8_t* mac_rx,
                   size_t mac_rx_len)
{
    uint8_t mac_calc[16];
    size_t mac_calc_len = 0;
    if (!mac_rx) return -1;
    if (nfc_mac_compute(type, key, key_len, data, data_len,
                        mac_calc, &mac_calc_len) != 0) return -1;
    if (mac_calc_len != mac_rx_len) return -2;
    return (nfc_memcmp_ct(mac_calc, mac_rx, mac_calc_len) == 0) ? 0 : -3;
}

void nfc_ev2_derive_session_keys(const uint8_t* kx,
                                 const uint8_t* rnd_a,
                                 const uint8_t* rnd_b,
                                 uint8_t* ses_mac,
                                 uint8_t* ses_enc)
{
    uint8_t ctx[26];
    uint8_t sv[32];

    ctx[0]  = rnd_a[14];
    ctx[1]  = rnd_a[15];
    ctx[2]  = (uint8_t)(rnd_a[13] ^ rnd_b[10]);
    ctx[3]  = (uint8_t)(rnd_a[12] ^ rnd_b[11]);
    ctx[4]  = (uint8_t)(rnd_a[11] ^ rnd_b[12]);
    ctx[5]  = (uint8_t)(rnd_a[10] ^ rnd_b[13]);
    ctx[6]  = (uint8_t)(rnd_a[ 9] ^ rnd_b[14]);
    ctx[7]  = (uint8_t)(rnd_a[ 8] ^ rnd_b[15]);
    ctx[8]  = rnd_b[ 9];
    ctx[9]  = rnd_b[ 8];
    ctx[10] = rnd_b[ 7];
    ctx[11] = rnd_b[ 6];
    ctx[12] = rnd_b[ 5];
    ctx[13] = rnd_b[ 4];
    ctx[14] = rnd_b[ 3];
    ctx[15] = rnd_b[ 2];
    ctx[16] = rnd_b[ 1];
    ctx[17] = rnd_b[ 0];
    ctx[18] = rnd_a[ 7];
    ctx[19] = rnd_a[ 6];
    ctx[20] = rnd_a[ 5];
    ctx[21] = rnd_a[ 4];
    ctx[22] = rnd_a[ 3];
    ctx[23] = rnd_a[ 2];
    ctx[24] = rnd_a[ 1];
    ctx[25] = rnd_a[ 0];

    sv[0] = 0xA5; sv[1] = 0x5A; sv[2] = 0x00; sv[3] = 0x01; sv[4] = 0x00; sv[5] = 0x80;
    memcpy(sv + 6, ctx, 26);
    (void)nfc_aes_cmac(kx, 16, sv, sizeof(sv), ses_mac);

    sv[0] = 0x5A; sv[1] = 0xA5;
    (void)nfc_aes_cmac(kx, 16, sv, sizeof(sv), ses_enc);
}

void nfc_ev2_mac_truncate(const uint8_t cmac16[16], uint8_t mac8[8])
{
    for (int i = 0; i < 8; i++) mac8[i] = cmac16[1 + 2 * i];
}

int nfc_ev2_compute_cmd_mac(const uint8_t* ses_mac_key,
                            uint8_t ins,
                            uint16_t cmd_ctr,
                            const uint8_t ti[4],
                            const uint8_t* cmd_header,
                            size_t hdr_len,
                            const uint8_t* cmd_data,
                            size_t data_len,
                            uint8_t mac8[8])
{
    if (!ses_mac_key || !ti || !mac8) return -1;

    size_t buf_len = 7 + hdr_len + data_len;
    uint8_t* buf = (uint8_t*)malloc(buf_len);
    if (!buf) return -1;

    buf[0] = ins;
    buf[1] = (uint8_t)(cmd_ctr & 0xFF);
    buf[2] = (uint8_t)((cmd_ctr >> 8) & 0xFF);
    memcpy(buf + 3, ti, 4);
    if (hdr_len && cmd_header) memcpy(buf + 7, cmd_header, hdr_len);
    if (data_len && cmd_data) memcpy(buf + 7 + hdr_len, cmd_data, data_len);

    uint8_t cmac[16];
    (void)nfc_aes_cmac(ses_mac_key, 16, buf, buf_len, cmac);
    nfc_ev2_mac_truncate(cmac, mac8);
    free(buf);
    return 0;
}

int nfc_ev2_compute_resp_mac(const uint8_t* ses_mac_key,
                             uint8_t ins,
                             uint16_t cmd_ctr_next,
                             const uint8_t ti[4],
                             const uint8_t* resp_data,
                             size_t resp_len,
                             uint8_t mac8[8])
{
    if (!ses_mac_key || !ti || !mac8) return -1;

    size_t buf_len = 7 + resp_len;
    uint8_t* buf = (uint8_t*)malloc(buf_len);
    if (!buf) return -1;

    buf[0] = ins;
    buf[1] = (uint8_t)(cmd_ctr_next & 0xFF);
    buf[2] = (uint8_t)((cmd_ctr_next >> 8) & 0xFF);
    memcpy(buf + 3, ti, 4);
    if (resp_len && resp_data) memcpy(buf + 7, resp_data, resp_len);

    uint8_t cmac[16];
    (void)nfc_aes_cmac(ses_mac_key, 16, buf, buf_len, cmac);
    nfc_ev2_mac_truncate(cmac, mac8);
    free(buf);
    return 0;
}

void nfc_ev2_build_iv_cmd(const uint8_t ses_enc[16],
                          const uint8_t ti[4],
                          uint16_t cmd_ctr,
                          uint8_t iv_out[16])
{
    uint8_t in[16] = {0};
    in[0] = 0xA5; in[1] = 0x5A;
    in[8] = (uint8_t)(cmd_ctr & 0xFF);
    in[9] = (uint8_t)((cmd_ctr >> 8) & 0xFF);
    memcpy(in + 10, ti, 4);
    (void)nfc_aes_ecb_encrypt(ses_enc, in, iv_out);
}

void nfc_ev2_build_iv_resp(const uint8_t ses_enc[16],
                           const uint8_t ti[4],
                           uint16_t cmd_ctr_next,
                           uint8_t iv_out[16])
{
    uint8_t in[16] = {0};
    in[0] = 0x5A; in[1] = 0xA5;
    in[8] = (uint8_t)(cmd_ctr_next & 0xFF);
    in[9] = (uint8_t)((cmd_ctr_next >> 8) & 0xFF);
    memcpy(in + 10, ti, 4);
    (void)nfc_aes_ecb_encrypt(ses_enc, in, iv_out);
}

size_t nfc_iso9797_pad_method2(const uint8_t* in,
                               size_t in_len,
                               uint8_t* out,
                               size_t out_max)
{
    if (!out || (!in && in_len)) return 0;
    size_t pad_len = 16U - (in_len % 16U);
    if (pad_len == 0) pad_len = 16U;
    size_t total = in_len + pad_len;
    if (total > out_max) return 0;
    if (in_len > 0) memcpy(out, in, in_len);
    out[in_len] = 0x80U;
    if (pad_len > 1) memset(out + in_len + 1, 0x00, pad_len - 1);
    return total;
}

size_t nfc_iso9797_unpad_method2(uint8_t* buf, size_t len)
{
    if (!buf || len == 0 || (len % 16U) != 0) return 0;
    size_t i = len;
    while (i > 0 && buf[i - 1] == 0x00U) i--;
    if (i == 0 || buf[i - 1] != 0x80U) return 0;
    return i - 1;
}

size_t nfc_iso9797_pad_method1(const uint8_t* in,
                               size_t in_len,
                               uint8_t* out,
                               size_t out_max)
{
    if (!out || (!in && in_len)) return 0;
    size_t pad_len = (16U - (in_len % 16U)) % 16U;
    size_t total = in_len + pad_len;
    if (total > out_max) return 0;
    if (in_len > 0) memcpy(out, in, in_len);
    if (pad_len > 0) memset(out + in_len, 0x00, pad_len);
    return total;
}

int nfc_diversify_aes128(const uint8_t* master_key,
                         const uint8_t* div_input,
                         size_t div_input_len,
                         uint8_t* div_key)
{
    if (!master_key || !div_input || !div_key) return -1;
    if (div_input_len == 0 || div_input_len > 31) return -1;

    uint8_t m_padded[32];
    memset(m_padded, 0x00, sizeof(m_padded));
    memcpy(m_padded, div_input, div_input_len);
    m_padded[div_input_len] = 0x80U;

    uint8_t sv[33];
    sv[0] = 0x01;
    memcpy(sv + 1, m_padded, sizeof(m_padded));

    return nfc_aes_cmac(master_key, 16, sv, sizeof(sv), div_key);
}

int nfc_diversify_desfire_key(const uint8_t* master_key,
                              const uint8_t* uid_7b,
                              const uint8_t* aid_3b,
                              uint8_t key_no,
                              const uint8_t* system_id,
                              size_t system_id_len,
                              uint8_t* div_key)
{
    if (!master_key || !uid_7b || !div_key) return -1;

    uint8_t m[31];
    size_t m_len = 0;
    memcpy(m + m_len, uid_7b, 7); m_len += 7;
    if (aid_3b) { memcpy(m + m_len, aid_3b, 3); m_len += 3; }
    m[m_len++] = key_no;
    if (system_id && system_id_len > 0) {
        size_t copy = (m_len + system_id_len > sizeof(m)) ? (sizeof(m) - m_len) : system_id_len;
        memcpy(m + m_len, system_id, copy);
        m_len += copy;
    }
    return nfc_diversify_aes128(master_key, m, m_len, div_key);
}

int nfc_diversify_mfp_key(const uint8_t* master_key,
                          const uint8_t* uid_7b,
                          uint16_t block_addr,
                          uint8_t* div_key)
{
    if (!master_key || !uid_7b || !div_key) return -1;
    uint8_t m[9];
    memcpy(m, uid_7b, 7);
    m[7] = (uint8_t)(block_addr & 0xFF);
    m[8] = (uint8_t)((block_addr >> 8) & 0xFF);
    return nfc_diversify_aes128(master_key, m, sizeof(m), div_key);
}

int nfc_diversify_2tdea(const uint8_t* master_key,
                        const uint8_t* div_input,
                        size_t div_input_len,
                        const uint8_t* original_key,
                        uint8_t* div_key)
{
    if (!master_key || !div_input || !div_key) return -1;
    if (div_input_len == 0 || div_input_len > 15) return -1;

    uint8_t m_padded[16];
    memset(m_padded, 0x00, sizeof(m_padded));
    memcpy(m_padded, div_input, div_input_len);
    m_padded[div_input_len] = 0x80U;

    uint8_t sv[17];
    sv[0] = 0x01;
    memcpy(sv + 1, m_padded, sizeof(m_padded));

    uint8_t cmac1[8];
    uint8_t cmac2[8];
    if (nfc_tdes_cmac(master_key, 16, sv, sizeof(sv), cmac1) != 0) return -1;

    sv[0] = 0x02;
    if (nfc_tdes_cmac(master_key, 16, sv, sizeof(sv), cmac2) != 0) return -1;

    memcpy(div_key, cmac1, 8);
    memcpy(div_key + 8, cmac2, 8);

    if (original_key) {
        div_key[7]  = (uint8_t)((div_key[7]  & 0xFEU) | (original_key[7]  & 0x01U));
        div_key[15] = (uint8_t)((div_key[15] & 0xFEU) | (original_key[15] & 0x01U));
    }
    return 0;
}
