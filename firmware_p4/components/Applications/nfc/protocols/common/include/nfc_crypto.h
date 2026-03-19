/**
 * @file nfc_crypto.h
 * @brief Common crypto helpers for NFC phases (CMAC, KDF, diversification).
 */
#ifndef NFC_CRYPTO_H
#define NFC_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    NFC_MAC_AES_CMAC_16 = 0,   /* Full 16B CMAC (key diversification). */
    NFC_MAC_AES_CMAC_EV1_8,    /* First 8B (DESFire EV1, MFP SL3). */
    NFC_MAC_AES_CMAC_EV2_8,    /* Odd bytes (DESFire EV2/EV3). */
    NFC_MAC_3DES_CBC_8,        /* 8B 3DES-CBC-MAC. */
    NFC_MAC_3DES_CBC_4,        /* 4B 3DES-CBC-MAC (truncated). */
} nfc_mac_type_t;

int nfc_memcmp_ct(const void* a, const void* b, size_t n);

bool nfc_aes_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
bool nfc_aes_cbc_crypt(bool encrypt,
                       const uint8_t key[16],
                       const uint8_t iv_in[16],
                       const uint8_t* in,
                       size_t len,
                       uint8_t* out,
                       uint8_t iv_out[16]);

int nfc_aes_cmac(const uint8_t* key,
                 size_t key_len,
                 const uint8_t* msg,
                 size_t msg_len,
                 uint8_t tag[16]);

int nfc_tdes_cbc_mac(const uint8_t* key,
                     size_t key_len,
                     const uint8_t* data,
                     size_t data_len,
                     uint8_t mac_out[8]);

int nfc_mac_compute(nfc_mac_type_t type,
                    const uint8_t* key,
                    size_t key_len,
                    const uint8_t* data,
                    size_t data_len,
                    uint8_t* mac_out,
                    size_t* mac_len_out);

int nfc_mac_verify(nfc_mac_type_t type,
                   const uint8_t* key,
                   size_t key_len,
                   const uint8_t* data,
                   size_t data_len,
                   const uint8_t* mac_rx,
                   size_t mac_rx_len);

void nfc_ev2_derive_session_keys(const uint8_t* kx,
                                 const uint8_t* rnd_a,
                                 const uint8_t* rnd_b,
                                 uint8_t* ses_mac,
                                 uint8_t* ses_enc);

void nfc_ev2_mac_truncate(const uint8_t cmac16[16], uint8_t mac8[8]);

int nfc_ev2_compute_cmd_mac(const uint8_t* ses_mac_key,
                            uint8_t ins,
                            uint16_t cmd_ctr,
                            const uint8_t ti[4],
                            const uint8_t* cmd_header,
                            size_t hdr_len,
                            const uint8_t* cmd_data,
                            size_t data_len,
                            uint8_t mac8[8]);

int nfc_ev2_compute_resp_mac(const uint8_t* ses_mac_key,
                             uint8_t ins,
                             uint16_t cmd_ctr_next,
                             const uint8_t ti[4],
                             const uint8_t* resp_data,
                             size_t resp_len,
                             uint8_t mac8[8]);

void nfc_ev2_build_iv_cmd(const uint8_t ses_enc[16],
                          const uint8_t ti[4],
                          uint16_t cmd_ctr,
                          uint8_t iv_out[16]);

void nfc_ev2_build_iv_resp(const uint8_t ses_enc[16],
                           const uint8_t ti[4],
                           uint16_t cmd_ctr_next,
                           uint8_t iv_out[16]);

size_t nfc_iso9797_pad_method2(const uint8_t* in,
                               size_t in_len,
                               uint8_t* out,
                               size_t out_max);

size_t nfc_iso9797_unpad_method2(uint8_t* buf, size_t len);

size_t nfc_iso9797_pad_method1(const uint8_t* in,
                               size_t in_len,
                               uint8_t* out,
                               size_t out_max);

int nfc_diversify_aes128(const uint8_t* master_key,
                         const uint8_t* div_input,
                         size_t div_input_len,
                         uint8_t* div_key);

int nfc_diversify_desfire_key(const uint8_t* master_key,
                              const uint8_t* uid_7b,
                              const uint8_t* aid_3b,
                              uint8_t key_no,
                              const uint8_t* system_id,
                              size_t system_id_len,
                              uint8_t* div_key);

int nfc_diversify_mfp_key(const uint8_t* master_key,
                          const uint8_t* uid_7b,
                          uint16_t block_addr,
                          uint8_t* div_key);

int nfc_diversify_2tdea(const uint8_t* master_key,
                        const uint8_t* div_input,
                        size_t div_input_len,
                        const uint8_t* original_key,
                        uint8_t* div_key);

#endif /* NFC_CRYPTO_H */
