#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mbedtls/aes.h"

#define MBEDTLS_DES_ENCRYPT 1
#define MBEDTLS_DES_DECRYPT 0

typedef struct {
    uint8_t unused;
} mbedtls_des3_context;

static inline void mbedtls_des3_init(mbedtls_des3_context *ctx) { (void)ctx; }
static inline void mbedtls_des3_free(mbedtls_des3_context *ctx) { (void)ctx; }
static inline int mbedtls_des3_set2key_enc(mbedtls_des3_context *ctx, const unsigned char key[16]) {
    (void)ctx;
    (void)key;
    return 0;
}
static inline int mbedtls_des3_set2key_dec(mbedtls_des3_context *ctx, const unsigned char key[16]) {
    (void)ctx;
    (void)key;
    return 0;
}
static inline int mbedtls_des3_set3key_enc(mbedtls_des3_context *ctx, const unsigned char key[24]) {
    (void)ctx;
    (void)key;
    return 0;
}
static inline int mbedtls_des3_crypt_ecb(mbedtls_des3_context *ctx, const unsigned char input[8],
                                         unsigned char output[8]) {
    (void)ctx;
    (void)input;
    (void)output;
    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
}
static inline int mbedtls_des3_crypt_cbc(mbedtls_des3_context *ctx, int mode, size_t length,
                                         unsigned char iv[8], const unsigned char *input,
                                         unsigned char *output) {
    (void)ctx;
    (void)mode;
    (void)length;
    (void)iv;
    (void)input;
    (void)output;
    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
}
