#pragma once

// libsodium is not built for the host, so .hb signature checks fail closed.
static inline int sodium_init(void) { return 0; }
static inline int crypto_sign_verify_detached(const unsigned char *sig, const unsigned char *m,
                                              unsigned long long mlen, const unsigned char *pk) {
    (void)sig;
    (void)m;
    (void)mlen;
    (void)pk;
    return -1;
}
