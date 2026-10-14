#pragma once

#include <stddef.h>
#include <stdint.h>

// mbedtls is not built for the host. Keys can be set, but every cipher call
// fails, which the NFC code already treats as an authentication failure.
#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0
#define MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED -0x0072

typedef struct {
    uint8_t unused;
} mbedtls_aes_context;

static inline void mbedtls_aes_init(mbedtls_aes_context *ctx) { (void)ctx; }
static inline void mbedtls_aes_free(mbedtls_aes_context *ctx) { (void)ctx; }
static inline int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key,
                                         unsigned int keybits) {
    (void)ctx;
    (void)key;
    (void)keybits;
    return 0;
}
static inline int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx, const unsigned char *key,
                                         unsigned int keybits) {
    (void)ctx;
    (void)key;
    (void)keybits;
    return 0;
}
static inline int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx, int mode,
                                        const unsigned char input[16], unsigned char output[16]) {
    (void)ctx;
    (void)mode;
    (void)input;
    (void)output;
    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
}
static inline int mbedtls_aes_crypt_cbc(mbedtls_aes_context *ctx, int mode, size_t length,
                                        unsigned char iv[16], const unsigned char *input,
                                        unsigned char *output) {
    (void)ctx;
    (void)mode;
    (void)length;
    (void)iv;
    (void)input;
    (void)output;
    return MBEDTLS_ERR_PLATFORM_FEATURE_UNSUPPORTED;
}
