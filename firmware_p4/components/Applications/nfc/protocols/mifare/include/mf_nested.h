#ifndef MF_NESTED_H
#define MF_NESTED_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "mf_classic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Nested Authentication Attack for MIFARE Classic.
 *
 * When you already know at least ONE key for any sector, this attack
 * recovers keys for sectors where the dictionary failed.
 *
 * Algorithm:
 *  1. Authenticate to a known sector (sector_src, key_src).
 *  2. While still in the Crypto1 session, send AUTH to the target sector.
 *     The card responds with nt_target ENCRYPTED by the current Crypto1 stream.
 *  3. Decrypt nt_target using the known keystream state.
 *  4. Repeat to collect multiple (nt, nr, ar) pairs on the wire.
 *  5. Use mfkey32 to recover the target key from two wire-level auth traces.
 *
 * Works best on cards with static or weak PRNG (most clones / Gen1a / Gen2).
 * Falls back to dictionary check with known nt for all card types.
 */

/* Result from one nested auth probe. */
typedef struct {
    uint32_t nt;      /* decrypted target nonce (plaintext) */
    uint32_t nr_enc;  /* encrypted reader nonce (as sent on wire) */
    uint32_t ar_enc;  /* encrypted reader response (as sent on wire) */
} mf_nested_sample_t;

/*
 * Collect one nested auth sample to target_block.
 * Requires that mf_classic_auth(src_block, src_key_type, src_key, uid) was
 * called successfully JUST BEFORE this function (Crypto1 must be active).
 *
 * Returns HB_NFC_OK and fills *sample on success.
 */
hb_nfc_err_t mf_nested_collect_sample(uint8_t      target_block,
                                        const uint8_t uid[4],
                                        uint32_t      nr_chosen,
                                        mf_nested_sample_t* sample);

/*
 * Full nested attack:
 *  - Authenticates to src_sector with src_key
 *  - Collects MF_NESTED_SAMPLE_COUNT samples to target_sector
 *  - Runs mfkey32 on pairs
 *  - Verifies recovered key by real auth
 *
 * Returns HB_NFC_OK and fills found_key on success.
 */
#define MF_NESTED_SAMPLE_COUNT   8
#define MF_NESTED_MAX_ATTEMPTS  32

hb_nfc_err_t mf_nested_attack(nfc_iso14443a_data_t* card,
                                uint8_t               src_sector,
                                const mf_classic_key_t* src_key,
                                mf_key_type_t           src_key_type,
                                uint8_t               target_sector,
                                mf_classic_key_t*     found_key_out,
                                mf_key_type_t*        found_key_type_out);

#ifdef __cplusplus
}
#endif
#endif
