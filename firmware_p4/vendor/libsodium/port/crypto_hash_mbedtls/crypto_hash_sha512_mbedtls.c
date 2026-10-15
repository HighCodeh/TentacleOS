/*
 * SPDX-FileCopyrightText: 2017-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <mbedtls/version.h>

/* Keep forward-compatibility with Mbed TLS 3.x */
#if (MBEDTLS_VERSION_NUMBER < 0x03000000)
#define MBEDTLS_2_X_COMPAT
#else /* !(MBEDTLS_VERSION_NUMBER < 0x03000000) */
/* Macro wrapper for struct's private members */
#ifndef MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#endif /* MBEDTLS_ALLOW_PRIVATE_ACCESS */
#endif /* !(MBEDTLS_VERSION_NUMBER < 0x03000000) */

/* For MbedTLS 4.x support using PSA crypto */
#if (MBEDTLS_VERSION_NUMBER >= 0x04000000)
#define MBEDTLS_PSA_CRYPTO
#endif

#include "crypto_hash_sha512.h"
#include <string.h>

int
crypto_hash_sha512_init(crypto_hash_sha512_state *state)
{
#ifdef MBEDTLS_PSA_CRYPTO
    psa_status_t status;

    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        return -1;
    }

    state->_psa_op = psa_hash_operation_init();

    status = psa_hash_setup(&state->_psa_op, PSA_ALG_SHA_512);
    if (status != PSA_SUCCESS) {
        return -1;
    }
    return 0;
#else
    mbedtls_sha512_init(&state->ctx);
#ifdef MBEDTLS_2_X_COMPAT
    int ret = mbedtls_sha512_starts_ret(&state->ctx, 0);
#else
    int ret = mbedtls_sha512_starts(&state->ctx, 0);
#endif /* MBEDTLS_2_X_COMPAT */
    if (ret != 0) {
        return ret;
    }
    return 0;
#endif /* !MBEDTLS_PSA_CRYPTO */
}

int
crypto_hash_sha512_update(crypto_hash_sha512_state *state,
                          const unsigned char *in, unsigned long long inlen)
{
    if (inlen > 0 && in == NULL) {
        return -1;
    }
#ifdef MBEDTLS_PSA_CRYPTO
    psa_status_t status;

    status = psa_hash_update(&state->_psa_op, in, inlen);
    if (status != PSA_SUCCESS) {
        psa_hash_abort(&state->_psa_op);
        return -1;
    }
    return 0;
#else
#ifdef MBEDTLS_2_X_COMPAT
    int ret = mbedtls_sha512_update_ret(&state->ctx, in, inlen);
#else
    int ret = mbedtls_sha512_update(&state->ctx, in, inlen);
#endif /* MBEDTLS_2_X_COMPAT */
    if (ret != 0) {
        return ret;
    }
    return 0;
#endif /* !MBEDTLS_PSA_CRYPTO */
}

int
crypto_hash_sha512_final(crypto_hash_sha512_state *state, unsigned char *out)
{
#ifdef MBEDTLS_PSA_CRYPTO
    psa_status_t status;
    size_t hash_len;

    status = psa_hash_finish(&state->_psa_op, out, crypto_hash_sha512_BYTES, &hash_len);
    if (status != PSA_SUCCESS || hash_len != crypto_hash_sha512_BYTES) {
        psa_hash_abort(&state->_psa_op);
        return -1;
    }
    return 0;
#else
#ifdef MBEDTLS_2_X_COMPAT
    return mbedtls_sha512_finish_ret(&state->ctx, out);
#else
    return mbedtls_sha512_finish(&state->ctx, out);
#endif /* MBEDTLS_2_X_COMPAT */
#endif /* !MBEDTLS_PSA_CRYPTO */
}

int
crypto_hash_sha512(unsigned char *out, const unsigned char *in,
                   unsigned long long inlen)
{
    if (inlen > 0 && in == NULL) {
        return -1;
    }
#ifdef MBEDTLS_PSA_CRYPTO
    psa_status_t status;
    size_t hash_len;

    status = psa_hash_compute(PSA_ALG_SHA_512, in, inlen, out,
                              crypto_hash_sha512_BYTES, &hash_len);
    if (status != PSA_SUCCESS || hash_len != crypto_hash_sha512_BYTES) {
        return -1;
    }
    return 0;
#else
#ifdef MBEDTLS_2_X_COMPAT
    return mbedtls_sha512_ret(in, inlen, out, 0);
#else
    return mbedtls_sha512(in, inlen, out, 0);
#endif /* MBEDTLS_2_X_COMPAT */
#endif /* !MBEDTLS_PSA_CRYPTO */
}
