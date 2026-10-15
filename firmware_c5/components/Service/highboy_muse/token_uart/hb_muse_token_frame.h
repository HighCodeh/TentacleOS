#ifndef HB_MUSE_TOKEN_FRAME_H
#define HB_MUSE_TOKEN_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define HB_MUSE_TOKEN_MAX       1023u
#define HB_MUSE_TOKEN_HEADER    23u
#define HB_MUSE_TOKEN_FRAME_MAX (HB_MUSE_TOKEN_HEADER + HB_MUSE_TOKEN_MAX + 4u)
/* HBMU1 + 16-byte challenge + LE16 length + ASCII mgst_ token + LE32 CRC32.
 * Challenge prevents accidental stale upload; UART/physical access is trust.
 * CRC detects transfer errors, it is NOT authentication or encryption. */
bool hb_muse_token_frame_decode(
    uint8_t *frame, size_t bytes, const uint8_t nonce[16], char **token, size_t *n);
#endif
