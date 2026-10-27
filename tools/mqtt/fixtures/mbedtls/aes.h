#ifndef TEST_AES_H
#define TEST_AES_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint8_t key[16]; } mbedtls_aes_context;
void mbedtls_aes_init(mbedtls_aes_context *ctx);
void mbedtls_aes_free(mbedtls_aes_context *ctx);
int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const uint8_t *key, unsigned bits);
int mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx, size_t length, size_t *offset,
                         uint8_t nonce[16], uint8_t stream[16], const uint8_t *in, uint8_t *out);
#endif
