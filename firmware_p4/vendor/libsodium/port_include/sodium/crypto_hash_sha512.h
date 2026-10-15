/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef crypto_hash_sha512_H
#define crypto_hash_sha512_H

#include <mbedtls/version.h>
#include <sodium/export.h>

/* For MbedTLS 4.x support using PSA Crypto */
#if (MBEDTLS_VERSION_NUMBER >= 0x04000000)
#define MBEDTLS_PSA_CRYPTO
#endif

#ifdef MBEDTLS_PSA_CRYPTO
#include "psa/crypto.h"
#else
#include "mbedtls/sha512.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct crypto_hash_sha512_state {
#ifdef MBEDTLS_PSA_CRYPTO
    psa_hash_operation_t _psa_op;
#else
    mbedtls_sha512_context ctx;
#endif /* MBEDTLS_PSA_CRYPTO */
} crypto_hash_sha512_state;

SODIUM_EXPORT
size_t crypto_hash_sha512_statebytes(void);

#define crypto_hash_sha512_BYTES 64U
SODIUM_EXPORT
size_t crypto_hash_sha512_bytes(void);

SODIUM_EXPORT
int crypto_hash_sha512(unsigned char *out, const unsigned char *in,
                       unsigned long long inlen) __attribute__ ((nonnull(1)));

SODIUM_EXPORT
int crypto_hash_sha512_init(crypto_hash_sha512_state *state)
__attribute__ ((nonnull));

SODIUM_EXPORT
int crypto_hash_sha512_update(crypto_hash_sha512_state *state,
                              const unsigned char *in,
                              unsigned long long inlen)
__attribute__ ((nonnull(1)));

SODIUM_EXPORT
int crypto_hash_sha512_final(crypto_hash_sha512_state *state,
                             unsigned char *out)
__attribute__ ((nonnull));

#ifdef __cplusplus
}
#endif

#endif /* crypto_hash_sha512_H */