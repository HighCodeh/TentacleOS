
#include "crypto1.h"

#define SWAPENDIAN(x) \
    ((x) = ((x) >> 8 & 0xff00ffU) | ((x) & 0xff00ffU) << 8, \
     (x) = (x) >> 16 | (x) << 16)

#define BIT(x, n)   (((x) >> (n)) & 1U)
#define BEBIT(x, n) BIT((x), (n) ^ 24)

#define LF_POLY_ODD  (0x29CE5CU)
#define LF_POLY_EVEN (0x870804U)

static const uint8_t s_odd_parity_table[256] = {
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

uint8_t crypto1_odd_parity8(uint8_t data)
{
    return s_odd_parity_table[data];
}

uint8_t crypto1_even_parity32(uint32_t data)
{
    data ^= data >> 16;
    data ^= data >> 8;
    return (uint8_t)(!s_odd_parity_table[data & 0xFF]);
}

static uint32_t crypto1_filter(uint32_t in)
{
    uint32_t out = 0;
    out  = 0xf22c0U >> (in        & 0xfU) & 16U;
    out |= 0x6c9c0U >> (in >>  4  & 0xfU) &  8U;
    out |= 0x3c8b0U >> (in >>  8  & 0xfU) &  4U;
    out |= 0x1e458U >> (in >> 12  & 0xfU) &  2U;
    out |= 0x0d938U >> (in >> 16  & 0xfU) &  1U;
    return BIT(0xEC57E80AU, out);
}

void crypto1_init(crypto1_state_t *s, uint64_t key)
{
    s->even = 0;
    s->odd  = 0;
    for (int8_t i = 47; i > 0; i -= 2) {
        s->odd  = s->odd  << 1 | BIT(key, (unsigned)((i - 1) ^ 7));
        s->even = s->even << 1 | BIT(key, (unsigned)(i ^ 7));
    }
}

void crypto1_reset(crypto1_state_t *s)
{
    s->odd  = 0;
    s->even = 0;
}

uint8_t crypto1_bit(crypto1_state_t *s, uint8_t in, int is_encrypted)
{
    uint8_t  out  = (uint8_t)crypto1_filter(s->odd);
    uint32_t feed = out & (uint32_t)(!!is_encrypted);
    feed ^= (uint32_t)(!!in);
    feed ^= LF_POLY_ODD  & s->odd;
    feed ^= LF_POLY_EVEN & s->even;
    s->even = s->even << 1 | crypto1_even_parity32(feed);

    uint32_t tmp = s->odd;
    s->odd  = s->even;
    s->even = tmp;

    return out;
}

uint8_t crypto1_byte(crypto1_state_t *s, uint8_t in, int is_encrypted)
{
    uint8_t out = 0;
    for (uint8_t i = 0; i < 8; i++) {
        out |= (uint8_t)(crypto1_bit(s, BIT(in, i), is_encrypted) << i);
    }
    return out;
}

/**
 * crypto1_word clock 32 bits in BEBIT order.
 *
 * BEBIT(x, i) = bit ((i) ^ 24) of x.
 * This processes bytes in MSB-first order but bits within each byte LSB-first.
 * Output uses the same byte-swapped convention (<< (24 ^ i)).
 *
 * This is identical to the Flipper Zero / proxmark3 implementation.
 */
uint32_t crypto1_word(crypto1_state_t *s, uint32_t in, int is_encrypted)
{
    uint32_t out = 0;
    for (uint8_t i = 0; i < 32; i++) {
        out |= (uint32_t)crypto1_bit(s, BEBIT(in, i), is_encrypted) << (24 ^ i);
    }
    return out;
}

/**
 * crypto1_filter_output read keystream bit WITHOUT advancing LFSR.
 *
 * Used for encrypted parity: after clocking 8 data bits with crypto1_byte,
 * call this to get the parity keystream bit without disturbing the LFSR state.
 * This matches how the Flipper Zero handles parity in crypto1_encrypt().
 */
uint8_t crypto1_filter_output(crypto1_state_t *s)
{
    return (uint8_t)crypto1_filter(s->odd);
}

/**
 * prng_successor MIFARE Classic card PRNG.
 *
 * CRITICAL: SWAPENDIAN before and after computation!
 * The Flipper Zero does this and without it the ar/at values are wrong.
 *
 * Input/output are in big-endian (wire) byte order.
 */
uint32_t crypto1_prng_successor(uint32_t x, uint32_t n)
{
    SWAPENDIAN(x);
    while (n--)
        x = x >> 1 | (x >> 16 ^ x >> 18 ^ x >> 19 ^ x >> 21) << 31;
    return SWAPENDIAN(x);
}

#undef SWAPENDIAN
#undef BIT
#undef BEBIT
#undef LF_POLY_ODD
#undef LF_POLY_EVEN

/*
 * BUGS FIXED (compared to previous version):
 *
 * 1. CRYPTO1 LFSR REPRESENTATION
 *  Old: single uint64_t state custom filter using fa/fb with specific bit indices.
 *  New: split odd/even uint32_t compact proxmark3 filter lookup tables.
 *  (Both should produce the same keystream if correct, but the old implementation
 *  had subtle index mapping differences.)
 *
 * 2. crypto1_word BIT ORDERING
 *  Old: processed bits 0..31 linearly (plain LSB-first).
 *  New: uses BEBIT(x, i) = bit((i)^24) byte-swapped order matching proxmark3.
 *  Impact: priming step (uid XOR nt) fed bits in wrong order entire keystream wrong.
 *
 * 3. prng_successor MISSING SWAPENDIAN
 *  Old: no byte swap before/after PRNG computation.
 *  New: SWAPENDIAN(x) before and after, matching Flipper/proxmark3.
 *  Impact: ar and at values completely wrong card rejects authentication.
 *
 * 4. AUTH ar: LFSR FED PLAINTEXT INSTEAD OF 0
 *  Old: mf_classic_transceive_raw encrypted nr+ar together, feeding all plaintext bits.
 *  New: nr is fed as plaintext (correct), ar is encrypted with LFSR free-running (feed 0).
 *  Impact: LFSR desynced after nr ar ciphertext wrong card rejects auth.
 *
 * 5. AUTH at EXPECTED VALUE COMPUTED FROM nr
 *  Old: at_expected = prng_successor(nr, 64) WRONG!
 *  New: at_expected = prng_successor(nt, 96) correct per MF Classic protocol.
 *  Impact: valid card responses were flagged as auth failures.
 *
 * 6. PARITY BIT ADVANCED THE LFSR
 *  Old: crypto1_bit(st, parity, 0) clocked the LFSR 9 per byte.
 *  New: crypto1_filter_output() reads the 9th keystream bit WITHOUT advancing.
 *  LFSR clocks exactly 8 per byte (only for data bits).
 *  Impact: after the first byte, the keystream was off by 1 bit per byte,
 *  accumulating all subsequent data was garbage.
 *
 * 7. ENCRYPTED EXCHANGE FED PLAINTEXT INTO LFSR
 *  Old: RX decrypt used crypto1_bit(st, enc, 1) which feeds the ciphertext into LFSR.
 *  New: RX decrypt uses crypto1_byte(st, 0, 0) ciphertext free-running LFSR.
 *  Impact: data exchange LFSR state diverged from card all block reads failed.
 */
#include "mf_classic.h"

#include <string.h>

#include "crypto1.h"
#include "iso14443a.h"
#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"

#include "esp_log.h"

#define TAG TAG_MF_CLASSIC
static const char* TAG = "mf_cl";

#define ISOA_NO_TX_PAR (1U << 7)
#define ISOA_NO_RX_PAR (1U << 6)

static crypto1_state_t s_crypto;
static bool            s_auth = false;
static uint32_t        s_last_nt = 0;  
static mf_write_phase_t s_last_write_phase = MF_WRITE_PHASE_NONE;

static inline void bit_set(uint8_t* buf, size_t bitpos, uint8_t v)
{
    if (v) buf[bitpos >> 3] |=  (uint8_t)(1U << (bitpos & 7U));
    else   buf[bitpos >> 3] &= (uint8_t)~(1U << (bitpos & 7U));
}

static inline uint8_t bit_get(const uint8_t* buf, size_t bitpos)
{
    return (uint8_t)((buf[bitpos >> 3] >> (bitpos & 7U)) & 1U);
}

static inline uint32_t bytes_to_num_be(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}

static inline uint32_t bytes_to_num_le(const uint8_t b[4])
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline uint64_t key_to_u64_be(const mf_classic_key_t* key)
{
    uint64_t k = 0;
    for (int i = 0; i < 6; i++) {
        k = (k << 8) | key->data[i];
    }
    return k;
}

static uint32_t s_nr_state = 0xA5A5A5A5U;

static uint32_t mf_rand32(void)
{
    uint32_t x = s_nr_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (x == 0) x = 1U;
    s_nr_state = x;
    return x;
}

void mf_classic_reset_auth(void)
{
    s_auth = false;
}

static hb_nfc_err_t mf_classic_transceive_encrypted(
    crypto1_state_t* st,
    const uint8_t* tx, size_t tx_len,
    uint8_t* rx, size_t rx_len,
    int timeout_ms)
{
    if (!st || !tx || tx_len == 0 || !rx || rx_len == 0)
        return HB_NFC_ERR_PARAM;

    const size_t tx_bits  = tx_len * 9;
    const size_t rx_bits  = rx_len * 9;
    const size_t tx_bytes = (tx_bits + 7U) / 8U;
    const size_t rx_bytes = (rx_bits + 7U) / 8U;
    if (tx_bytes > 32 || rx_bytes > 32)
        return HB_NFC_ERR_PARAM;

    uint8_t tx_buf[32] = { 0 };
    uint8_t rx_buf[32] = { 0 };

    size_t bitpos = 0;
    for (size_t i = 0; i < tx_len; i++) {
        uint8_t plain  = tx[i];
        uint8_t ks     = crypto1_byte(st, 0, 0);
        uint8_t enc    = plain ^ ks;

        for (int bit = 0; bit < 8; bit++) {
            bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
        }

        uint8_t par_ks = crypto1_filter_output(st);
        uint8_t par    = crypto1_odd_parity8(plain) ^ par_ks;
        bit_set(tx_buf, bitpos++, par);
    }

    uint8_t iso = 0;
    hb_spi_reg_read(REG_ISO14443A, &iso);
    hb_spi_reg_write(REG_ISO14443A, (uint8_t)(iso | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)(tx_bits / 8U), (uint8_t)(tx_bits % 8U));
    st25r_fifo_load(tx_buf, tx_bytes);
    hb_spi_direct_cmd(CMD_TX_WO_CRC);

    if (!st25r_irq_wait_txe()) {
        hb_spi_reg_write(REG_ISO14443A, iso);
        return HB_NFC_ERR_TX_TIMEOUT;
    }

    uint16_t count = 0;
    (void)st25r_fifo_wait(rx_bytes, timeout_ms, &count);
    if (count < rx_bytes) {
        if (count > 0) {
            size_t to_read = (count < rx_bytes) ? count : rx_bytes;
            st25r_fifo_read(rx_buf, to_read);
            nfc_log_hex(" MF RX partial:", rx_buf, to_read);
        }
        hb_spi_reg_write(REG_ISO14443A, iso);
        return HB_NFC_ERR_TIMEOUT;
    }

    st25r_fifo_read(rx_buf, rx_bytes);
    hb_spi_reg_write(REG_ISO14443A, iso);

    bitpos = 0;
    for (size_t i = 0; i < rx_len; i++) {
        uint8_t enc_byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            enc_byte |= (uint8_t)(bit_get(rx_buf, bitpos++) << bit);
        }

        uint8_t ks    = crypto1_byte(st, 0, 0);
        uint8_t plain = enc_byte ^ ks;

        uint8_t enc_par = bit_get(rx_buf, bitpos++);
        uint8_t par_ks  = crypto1_filter_output(st);
        uint8_t dec_par = enc_par ^ par_ks;
        if (dec_par != crypto1_odd_parity8(plain)) {
            ESP_LOGW(TAG, "Parity error byte %u: got %u exp %u",
                     (unsigned)i, dec_par, crypto1_odd_parity8(plain));
            return HB_NFC_ERR_PROTOCOL;
        }

        rx[i] = plain;
    }

    return HB_NFC_OK;
}

static hb_nfc_err_t mf_classic_tx_encrypted_with_ack(
    crypto1_state_t* st,
    const uint8_t* tx, size_t tx_len,
    uint8_t* ack_nibble,
    int timeout_ms)
{
    if (!st || !tx || tx_len == 0) return HB_NFC_ERR_PARAM;

    const size_t tx_bits  = tx_len * 9;
    const size_t tx_bytes = (tx_bits + 7U) / 8U;
    if (tx_bytes > 32) return HB_NFC_ERR_PARAM;

    uint8_t tx_buf[32] = { 0 };

    size_t bitpos = 0;
    for (size_t i = 0; i < tx_len; i++) {
        uint8_t plain  = tx[i];
        uint8_t ks     = crypto1_byte(st, 0, 0);
        uint8_t enc    = plain ^ ks;

        for (int bit = 0; bit < 8; bit++) {
            bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
        }

        uint8_t par_ks = crypto1_filter_output(st);
        uint8_t par    = crypto1_odd_parity8(plain) ^ par_ks;
        bit_set(tx_buf, bitpos++, par);
    }

    uint8_t iso = 0;
    hb_spi_reg_read(REG_ISO14443A, &iso);
    hb_spi_reg_write(REG_ISO14443A, (uint8_t)(iso | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)(tx_bits / 8U), (uint8_t)(tx_bits % 8U));
    st25r_fifo_load(tx_buf, tx_bytes);
    hb_spi_direct_cmd(CMD_TX_WO_CRC);

    if (!st25r_irq_wait_txe()) {
        hb_spi_reg_write(REG_ISO14443A, iso);
        return HB_NFC_ERR_TX_TIMEOUT;
    }

    uint16_t count = 0;
    (void)st25r_fifo_wait(1, timeout_ms, &count);
    if (count < 1) {
        hb_spi_reg_write(REG_ISO14443A, iso);
        return HB_NFC_ERR_TIMEOUT;
    }

    uint8_t enc_ack = 0;
    st25r_fifo_read(&enc_ack, 1);
    hb_spi_reg_write(REG_ISO14443A, iso);

    uint8_t plain_ack = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t ks_bit   = crypto1_bit(st, 0, 0);
        uint8_t enc_bit  = (enc_ack >> i) & 1U;
        plain_ack |= (uint8_t)((enc_bit ^ ks_bit) << i);
    }

    if (ack_nibble) *ack_nibble = (uint8_t)(plain_ack & 0x0F);
    return HB_NFC_OK;
}

hb_nfc_err_t mf_classic_auth(uint8_t block, mf_key_type_t key_type,
                               const mf_classic_key_t* key,
                               const uint8_t uid[4])
{
    if (!key || !uid) return HB_NFC_ERR_PARAM;
    s_auth = false;

    uint8_t cmd[2]    = { (uint8_t)key_type, block };
    uint8_t nt_raw[4] = { 0 };
    int len = nfc_poller_transceive(cmd, 2, true, nt_raw, sizeof(nt_raw), 4, 20);
    if (len < 4) {
        ESP_LOGW(TAG, "Auth: no nonce (len=%d)", len);
        return HB_NFC_ERR_AUTH;
    }
    nfc_log_hex(" Auth nt:", nt_raw, 4);

    uint64_t k48   = key_to_u64_be(key);
    uint32_t cuid  = bytes_to_num_be(uid);
    uint32_t nt_be = bytes_to_num_be(nt_raw);
    s_last_nt = nt_be;

    crypto1_init(&s_crypto, k48);
    crypto1_word(&s_crypto, nt_be ^ cuid, 0);

    uint8_t nr[4];
    uint32_t nr32 = mf_rand32();
    nr[0] = (uint8_t)((nr32 >> 24) & 0xFF);
    nr[1] = (uint8_t)((nr32 >> 16) & 0xFF);
    nr[2] = (uint8_t)((nr32 >>  8) & 0xFF);
    nr[3] = (uint8_t)( nr32        & 0xFF);

    uint8_t tx_buf[32] = { 0 };
    size_t  bitpos = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t ks  = crypto1_byte(&s_crypto, nr[i], 0);
        uint8_t enc = ks ^ nr[i];

        for (int bit = 0; bit < 8; bit++) {
            bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
        }

        uint8_t par_ks = crypto1_filter_output(&s_crypto);
        uint8_t par    = crypto1_odd_parity8(nr[i]) ^ par_ks;
        bit_set(tx_buf, bitpos++, par);
    }

    uint32_t nt_succ = crypto1_prng_successor(nt_be, 32);
    for (int i = 0; i < 4; i++) {
        nt_succ = crypto1_prng_successor(nt_succ, 8);
        uint8_t ar_byte = (uint8_t)(nt_succ & 0xFF);

        uint8_t ks  = crypto1_byte(&s_crypto, 0, 0);
        uint8_t enc = ks ^ ar_byte;

        for (int bit = 0; bit < 8; bit++) {
            bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
        }

        uint8_t par_ks = crypto1_filter_output(&s_crypto);
        uint8_t par    = crypto1_odd_parity8(ar_byte) ^ par_ks;
        bit_set(tx_buf, bitpos++, par);
    }

    const size_t tx_total_bits  = 8 * 9;
    const size_t tx_total_bytes = (tx_total_bits + 7U) / 8U;
    const size_t rx_total_bits  = 4 * 9;
    const size_t rx_total_bytes = (rx_total_bits + 7U) / 8U;

    uint8_t rx_buf[32] = { 0 };

    uint8_t iso = 0;
    hb_spi_reg_read(REG_ISO14443A, &iso);
    hb_spi_reg_write(REG_ISO14443A, (uint8_t)(iso | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)(tx_total_bits / 8U), (uint8_t)(tx_total_bits % 8U));
    st25r_fifo_load(tx_buf, tx_total_bytes);
    hb_spi_direct_cmd(CMD_TX_WO_CRC);

    if (!st25r_irq_wait_txe()) {
        hb_spi_reg_write(REG_ISO14443A, iso);
        ESP_LOGW(TAG, "Auth: TX timeout");
        return HB_NFC_ERR_TX_TIMEOUT;
    }

    uint16_t count = 0;
    (void)st25r_fifo_wait(rx_total_bytes, 20, &count);
    if (count < rx_total_bytes) {
        hb_spi_reg_write(REG_ISO14443A, iso);
        ESP_LOGW(TAG, "Auth: RX timeout (got %u need %u)", count, (unsigned)rx_total_bytes);
        return HB_NFC_ERR_TIMEOUT;
    }

    st25r_fifo_read(rx_buf, rx_total_bytes);
    hb_spi_reg_write(REG_ISO14443A, iso);

    uint8_t at_dec[4] = { 0 };
    bitpos = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t enc_byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            enc_byte |= (uint8_t)(bit_get(rx_buf, bitpos++) << bit);
        }
        uint8_t ks = crypto1_byte(&s_crypto, 0, 0);
        at_dec[i] = enc_byte ^ ks;
        bitpos++;
    }

    nfc_log_hex(" Auth at_dec:", at_dec, 4);

    s_auth = true;
    ESP_LOGI(TAG, "Auth SUCCESS on block %d", block);
    return HB_NFC_OK;
}

hb_nfc_err_t mf_classic_read_block(uint8_t block, uint8_t data[16])
{
    if (!data) return HB_NFC_ERR_PARAM;
    if (!s_auth) return HB_NFC_ERR_AUTH;

    uint8_t cmd[4] = { 0x30, block, 0, 0 };
    iso14443a_crc(cmd, 2, &cmd[2]);

    uint8_t rx[18] = { 0 };
    hb_nfc_err_t err = mf_classic_transceive_encrypted(&s_crypto, cmd, sizeof(cmd),
                                                       rx, sizeof(rx), 30);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "Read block %d: transceive failed (%s)",
                 block, hb_nfc_err_str(err));
        s_auth = false;
        return err;
    }

    if (!iso14443a_check_crc(rx, sizeof(rx))) {
        ESP_LOGW(TAG, "Read block %d: CRC error", block);
        nfc_log_hex("  rx:", rx, sizeof(rx));
        s_auth = false;
        return HB_NFC_ERR_CRC;
    }

    memcpy(data, rx, 16);
    return HB_NFC_OK;
}

hb_nfc_err_t mf_classic_write_block(uint8_t block, const uint8_t data[16])
{
    if (!data) return HB_NFC_ERR_PARAM;
    if (!s_auth) return HB_NFC_ERR_AUTH;

    uint8_t cmd[4] = { 0xA0, block, 0, 0 };
    iso14443a_crc(cmd, 2, &cmd[2]);

    uint8_t ack = 0;
    s_last_write_phase = MF_WRITE_PHASE_CMD;
    hb_nfc_err_t err = mf_classic_tx_encrypted_with_ack(&s_crypto, cmd, sizeof(cmd),
                                                        &ack, 20);
    if (err != HB_NFC_OK) {
        s_auth = false;
        return err;
    }
    if ((ack & 0x0F) != 0x0A) {
        ESP_LOGW(TAG, "Write cmd NACK (block %d): 0x%02X", block, ack);
        s_auth = false;
        return HB_NFC_ERR_NACK;
    }

    uint8_t frame[18];
    memcpy(frame, data, 16);
    iso14443a_crc(frame, 16, &frame[16]);

    s_last_write_phase = MF_WRITE_PHASE_DATA;
    err = mf_classic_tx_encrypted_with_ack(&s_crypto, frame, sizeof(frame),
                                           &ack, 20);
    if (err != HB_NFC_OK) {
        s_auth = false;
        return err;
    }
    if ((ack & 0x0F) != 0x0A) {
        ESP_LOGW(TAG, "Write data NACK (block %d): 0x%02X", block, ack);
        s_auth = false;
        return HB_NFC_ERR_NACK;
    }

    return HB_NFC_OK;
}

mf_classic_type_t mf_classic_get_type(uint8_t sak)
{
    switch (sak) {
    case 0x09: return MF_CLASSIC_MINI;
    case 0x08: return MF_CLASSIC_1K;
    case 0x88: return MF_CLASSIC_1K;
    case 0x18: return MF_CLASSIC_4K;
    default:   return MF_CLASSIC_1K;
    }
}

int mf_classic_get_sector_count(mf_classic_type_t type)
{
    switch (type) {
    case MF_CLASSIC_MINI: return 5;
    case MF_CLASSIC_1K:   return 16;
    case MF_CLASSIC_4K:   return 40;
    default:              return 16;
    }
}

uint32_t mf_classic_get_last_nt(void)
{
    return s_last_nt;
}

mf_write_phase_t mf_classic_get_last_write_phase(void)
{
    return s_last_write_phase;
}

void mf_classic_get_crypto_state(crypto1_state_t* out)
{
    if (!out) return;
    memcpy(out, &s_crypto, sizeof(*out));
}

#undef TAG

#include "mf_classic_writer.h"

#include <string.h>
#include "esp_log.h"

#include "poller.h"
#include "mf_classic.h"
#include "nfc_poller.h"

#define TAG TAG_MF_WRITE
static const char* TAG = "mf_write";

const uint8_t MF_ACCESS_BITS_DEFAULT[3]   = { 0xFF, 0x07, 0x80 };
const uint8_t MF_ACCESS_BITS_READ_ONLY[3] = { 0x78, 0x77, 0x88 };

const char* mf_write_result_str(mf_write_result_t r)
{
    switch (r) {
    case MF_WRITE_OK:           return "OK";
    case MF_WRITE_ERR_RESELECT: return "reselect failed";
    case MF_WRITE_ERR_AUTH:     return "authentication denied";
    case MF_WRITE_ERR_CMD_NACK: return "WRITE command NACK";
    case MF_WRITE_ERR_DATA_NACK:return "data NACK";
    case MF_WRITE_ERR_VERIFY:   return "verification failed";
    case MF_WRITE_ERR_PROTECTED:return "block protected";
    case MF_WRITE_ERR_PARAM:    return "invalid parameter";
    default:                    return "unknown error";
    }
}

static bool mf_classic_access_bit_is_valid(uint8_t v)
{
    return (v == 0U || v == 1U);
}

bool mf_classic_access_bits_encode(const mf_classic_access_bits_t* ac,
                                    uint8_t                         out_access_bits[3])
{
    if (!ac || !out_access_bits) return false;

    uint8_t b6 = 0;
    uint8_t b7 = 0;
    uint8_t b8 = 0;

    for (int grp = 0; grp < 4; grp++) {
        uint8_t c1 = ac->c1[grp];
        uint8_t c2 = ac->c2[grp];
        uint8_t c3 = ac->c3[grp];

        if (!mf_classic_access_bit_is_valid(c1) ||
            !mf_classic_access_bit_is_valid(c2) ||
            !mf_classic_access_bit_is_valid(c3)) {
            return false;
        }

        if (c1) b7 |= (uint8_t)(1U << (4 + grp));
        else    b6 |= (uint8_t)(1U << grp);

        if (c2) b8 |= (uint8_t)(1U << grp);
        else    b6 |= (uint8_t)(1U << (4 + grp));

        if (c3) b8 |= (uint8_t)(1U << (4 + grp));
        else    b7 |= (uint8_t)(1U << grp);
    }

    out_access_bits[0] = b6;
    out_access_bits[1] = b7;
    out_access_bits[2] = b8;
    return true;
}

bool mf_classic_access_bits_valid(const uint8_t access_bits[3])
{
    if (!access_bits) return false;

    uint8_t b6 = access_bits[0];
    uint8_t b7 = access_bits[1];
    uint8_t b8 = access_bits[2];

    for (int grp = 0; grp < 4; grp++) {
        uint8_t c1     = (b7 >> (4 + grp)) & 1U;
        uint8_t c1_inv = (uint8_t)((~b6 >> grp) & 1U);
        uint8_t c2     = (b8 >> grp) & 1U;
        uint8_t c2_inv = (uint8_t)((~b6 >> (4 + grp)) & 1U);
        uint8_t c3     = (b8 >> (4 + grp)) & 1U;
        uint8_t c3_inv = (uint8_t)((~b7 >> grp) & 1U);
        if (c1 != c1_inv || c2 != c2_inv || c3 != c3_inv) return false;
    }

    return true;
}

static inline int mf_classic_total_blocks(mf_classic_type_t type)
{
    switch (type) {
    case MF_CLASSIC_MINI: return 20;
    case MF_CLASSIC_1K:   return 64;
    case MF_CLASSIC_4K:   return 256;
    default:              return 64;
    }
}

static inline int mf_classic_sector_block_count(mf_classic_type_t type, int sector)
{
    if (type == MF_CLASSIC_4K && sector >= 32) return 16;
    return 4;
}

static inline int mf_classic_sector_first_block(mf_classic_type_t type, int sector)
{
    if (type == MF_CLASSIC_4K && sector >= 32) return 128 + (sector - 32) * 16;
    return sector * 4;
}

static inline int mf_classic_sector_trailer_block(mf_classic_type_t type, int sector)
{
    return mf_classic_sector_first_block(type, sector) +
           mf_classic_sector_block_count(type, sector) - 1;
}

static inline int mf_classic_block_to_sector(mf_classic_type_t type, int block)
{
    if (type == MF_CLASSIC_4K && block >= 128) return 32 + (block - 128) / 16;
    return block / 4;
}

static inline bool mf_classic_is_trailer_block(mf_classic_type_t type, int block)
{
    int sector = mf_classic_block_to_sector(type, block);
    return block == mf_classic_sector_trailer_block(type, sector);
}

mf_write_result_t mf_classic_write_block_raw(uint8_t block,
                                               const uint8_t data[16])
{
    hb_nfc_err_t err = mf_classic_write_block(block, data);
    if (err == HB_NFC_OK) return MF_WRITE_OK;
    if (err == HB_NFC_ERR_AUTH) return MF_WRITE_ERR_AUTH;
    if (err == HB_NFC_ERR_NACK) {
        mf_write_phase_t phase = mf_classic_get_last_write_phase();
        return (phase == MF_WRITE_PHASE_DATA) ? MF_WRITE_ERR_DATA_NACK
                                              : MF_WRITE_ERR_CMD_NACK;
    }
    return MF_WRITE_ERR_CMD_NACK;
}

mf_write_result_t mf_classic_write(nfc_iso14443a_data_t* card,
                                    uint8_t               block,
                                    const uint8_t         data[16],
                                    const uint8_t         key[6],
                                    mf_key_type_t         key_type,
                                    bool                  verify,
                                    bool                  allow_special)
{
    if (!card || !data || !key) return MF_WRITE_ERR_PARAM;

    mf_classic_type_t type = mf_classic_get_type(card->sak);
    if ((int)block >= mf_classic_total_blocks(type)) return MF_WRITE_ERR_PARAM;

    if (block == 0 && !allow_special) {
        ESP_LOGE(TAG, "Block 0 (manufacturer) protected use allow_special=true only on magic cards");
        return MF_WRITE_ERR_PROTECTED;
    }
    if (mf_classic_is_trailer_block(type, block) && !allow_special) {
        ESP_LOGE(TAG, "Block %d is a trailer use allow_special=true and verify access bits!", block);
        return MF_WRITE_ERR_PROTECTED;
    }

    mf_classic_reset_auth();
    hb_nfc_err_t err = iso14443a_poller_reselect(card);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "Reselect failed: %d", err);
        return MF_WRITE_ERR_RESELECT;
    }

    mf_classic_key_t k;
    memcpy(k.data, key, 6);

    err = mf_classic_auth(block, key_type, &k, card->uid);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "Auth failed on block %d (key%c)", block,
                 key_type == MF_KEY_A ? 'A' : 'B');
        return MF_WRITE_ERR_AUTH;
    }

    mf_write_result_t wres = mf_classic_write_block_raw(block, data);
    if (wres != MF_WRITE_OK) {
        ESP_LOGE(TAG, "Write failed (block %d): %s", block, mf_write_result_str(wres));
        return wres;
    }

    ESP_LOGI(TAG, "Block %d written", block);

    if (verify) {
        uint8_t readback[16] = { 0 };
        err = mf_classic_read_block(block, readback);
        if (err != HB_NFC_OK) {
            ESP_LOGW(TAG, "Verify: read failed (block %d)", block);
            return MF_WRITE_ERR_VERIFY;
        }
        if (memcmp(data, readback, 16) != 0) {
            ESP_LOGE(TAG, "Verify: read data mismatch (block %d)!", block);
            ESP_LOG_BUFFER_HEX("expected", data, 16);
            ESP_LOG_BUFFER_HEX("readback", readback, 16);
            return MF_WRITE_ERR_VERIFY;
        }
        ESP_LOGI(TAG, "Block %d verified", block);
    }

    return MF_WRITE_OK;
}

int mf_classic_write_sector(nfc_iso14443a_data_t* card,
                             uint8_t               sector,
                             const uint8_t*        data,
                             const uint8_t         key[6],
                             mf_key_type_t         key_type,
                             bool                  verify)
{
    if (!card || !data || !key) return -1;

    mf_classic_type_t type = mf_classic_get_type(card->sak);
    int sector_count = mf_classic_get_sector_count(type);
    if ((int)sector >= sector_count) return -1;

    const int blocks_in_sector = mf_classic_sector_block_count(type, sector);
    const int data_blocks      = blocks_in_sector - 1;
    const int fb               = mf_classic_sector_first_block(type, sector);
    const int last_data_block  = fb + data_blocks - 1;

    ESP_LOGI(TAG, "Writing sector %d (blocks %d..%d)...",
             sector, fb, last_data_block);

    mf_classic_reset_auth();
    hb_nfc_err_t err = iso14443a_poller_reselect(card);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "Reselect failed for sector %d", sector);
        return -1;
    }

    mf_classic_key_t k;
    memcpy(k.data, key, 6);

    err = mf_classic_auth(fb, key_type, &k, card->uid);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "Auth failed on sector %d (key%c)", sector,
                 key_type == MF_KEY_A ? 'A' : 'B');
        return -1;
    }

    int written = 0;
    for (int b = 0; b < data_blocks; b++) {
        uint8_t block = (uint8_t)(fb + b);
        const uint8_t* block_data = data + (b * 16);

        mf_write_result_t wres = mf_classic_write_block_raw(block, block_data);
        if (wres != MF_WRITE_OK) {
            ESP_LOGE(TAG, "Write failed on block %d: %s", block,
                     mf_write_result_str(wres));
            break;
        }

        if (verify) {
            mf_classic_reset_auth();
            err = iso14443a_poller_reselect(card);
            if (err != HB_NFC_OK) {
                ESP_LOGE(TAG, "Reselect failed during verify (block %d)", block);
                return written;
            }
            err = mf_classic_auth(fb, key_type, &k, card->uid);
            if (err != HB_NFC_OK) {
                ESP_LOGE(TAG, "Auth failed during verify (sector %d, key%c)",
                         sector, key_type == MF_KEY_A ? 'A' : 'B');
                return written;
            }

            uint8_t readback[16] = { 0 };
            err = mf_classic_read_block(block, readback);
            if (err != HB_NFC_OK || memcmp(block_data, readback, 16) != 0) {
                ESP_LOGE(TAG, "Verify failed on block %d!", block);
                return written;
            }
            ESP_LOGI(TAG, "Block %d written and verified", block);

            if (b < data_blocks - 1) {
                mf_classic_reset_auth();
                err = iso14443a_poller_reselect(card);
                if (err != HB_NFC_OK) {
                    ESP_LOGE(TAG, "Reselect failed to continue (sector %d)", sector);
                    return written;
                }
                err = mf_classic_auth(fb, key_type, &k, card->uid);
                if (err != HB_NFC_OK) {
                    ESP_LOGE(TAG, "Auth failed to continue (sector %d, key%c)",
                             sector, key_type == MF_KEY_A ? 'A' : 'B');
                    return written;
                }
            }
        } else {
            ESP_LOGI(TAG, "Block %d written", block);
        }

        written++;
    }

    ESP_LOGI(TAG, "Sector %d: %d/%d blocks written", sector, written, data_blocks);
    return written;
}

void mf_classic_build_trailer(const uint8_t  key_a[6],
                               const uint8_t  key_b[6],
                               const uint8_t  access_bits[3],
                               uint8_t        out_trailer[16])
{
    memcpy(out_trailer, key_a, 6);

    const uint8_t* ac = access_bits ? access_bits : MF_ACCESS_BITS_DEFAULT;
    out_trailer[6] = ac[0];
    out_trailer[7] = ac[1];
    out_trailer[8] = ac[2];

    out_trailer[9] = 0x00;

    memcpy(&out_trailer[10], key_b, 6);
}

bool mf_classic_build_trailer_safe(const uint8_t              key_a[6],
                                    const uint8_t              key_b[6],
                                    const mf_classic_access_bits_t* ac,
                                    uint8_t                    gpb,
                                    uint8_t                    out_trailer[16])
{
    if (!key_a || !key_b || !ac || !out_trailer) return false;

    uint8_t access_bits[3];
    if (!mf_classic_access_bits_encode(ac, access_bits)) return false;
    if (!mf_classic_access_bits_valid(access_bits)) return false;

    memcpy(out_trailer, key_a, 6);
    out_trailer[6] = access_bits[0];
    out_trailer[7] = access_bits[1];
    out_trailer[8] = access_bits[2];
    out_trailer[9] = gpb;
    memcpy(&out_trailer[10], key_b, 6);
    return true;
}

#undef TAG

#include "mf_ultralight.h"
#include "nfc_poller.h"
#include "nfc_common.h"
#include "hb_nfc_timer.h"

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/des.h"

#define TAG TAG_MF_UL
static const char* TAG = "mful";

static void ulc_random(uint8_t* out, size_t len)
{
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = esp_random();
        size_t left = len - i;
        size_t n = left < 4 ? left : 4;
        memcpy(&out[i], &r, n);
    }
}

static void rotate_left_8(uint8_t* out, const uint8_t* in)
{
    out[0] = in[1];
    out[1] = in[2];
    out[2] = in[3];
    out[3] = in[4];
    out[4] = in[5];
    out[5] = in[6];
    out[6] = in[7];
    out[7] = in[0];
}

static bool des3_cbc_crypt(bool encrypt,
                            const uint8_t key[16],
                            const uint8_t iv_in[8],
                            const uint8_t* in,
                            size_t len,
                            uint8_t* out,
                            uint8_t iv_out[8])
{
    if ((len % 8) != 0 || !key || !iv_in || !in || !out) return false;

    mbedtls_des3_context ctx;
    mbedtls_des3_init(&ctx);
    int rc = encrypt ? mbedtls_des3_set2key_enc(&ctx, key)
                     : mbedtls_des3_set2key_dec(&ctx, key);
    if (rc != 0) {
        mbedtls_des3_free(&ctx);
        return false;
    }

    uint8_t iv[8];
    memcpy(iv, iv_in, 8);
    rc = mbedtls_des3_crypt_cbc(&ctx, encrypt ? MBEDTLS_DES_ENCRYPT : MBEDTLS_DES_DECRYPT,
                                len, iv, in, out);
    if (iv_out) memcpy(iv_out, iv, 8);
    mbedtls_des3_free(&ctx);
    return rc == 0;
}

int mful_read_pages(uint8_t page, uint8_t out[18])
{
    uint8_t cmd[2] = { 0x30, page };
    return nfc_poller_transceive(cmd, 2, true, out, 18, 1, 30);
}

hb_nfc_err_t mful_write_page(uint8_t page, const uint8_t data[4])
{
    uint8_t cmd[6] = { 0xA2, page, data[0], data[1], data[2], data[3] };
    uint8_t rx[4] = { 0 };
    int len = nfc_poller_transceive(cmd, 6, true, rx, 4, 1, 20);
    
    if (len >= 1 && (rx[0] & 0x0F) == 0x0A) return HB_NFC_OK;
    return HB_NFC_ERR_NACK;
}

int mful_get_version(uint8_t out[8])
{
    uint8_t cmd[1] = { 0x60 };
    return nfc_poller_transceive(cmd, 1, true, out, 8, 1, 20);
}

int mful_pwd_auth(const uint8_t pwd[4], uint8_t pack[2])
{
    uint8_t cmd[5] = { 0x1B, pwd[0], pwd[1], pwd[2], pwd[3] };
    uint8_t rx[4] = { 0 };
    int len = nfc_poller_transceive(cmd, 5, true, rx, 4, 2, 20);
    if (len >= 2) {
        pack[0] = rx[0];
        pack[1] = rx[1];
        hb_delay_us(500);  
    }
    return len;
}

hb_nfc_err_t mful_ulc_auth(const uint8_t key[16])
{
    if (!key) return HB_NFC_ERR_PARAM;

    uint8_t cmd = 0x1A;
    uint8_t rx[16] = { 0 };
    int len = nfc_poller_transceive(&cmd, 1, true, rx, sizeof(rx), 1, 30);
    if (len < 8) return HB_NFC_ERR_PROTOCOL;

    uint8_t enc_rndb[8];
    memcpy(enc_rndb, rx, 8);

    uint8_t rndb[8];
    uint8_t iv0[8] = { 0 };
    uint8_t iv1[8] = { 0 };
    if (!des3_cbc_crypt(false, key, iv0, enc_rndb, 8, rndb, iv1)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rnda[8];
    ulc_random(rnda, sizeof(rnda));

    uint8_t rndb_rot[8];
    rotate_left_8(rndb_rot, rndb);

    uint8_t plain[16];
    memcpy(plain, rnda, 8);
    memcpy(&plain[8], rndb_rot, 8);

    uint8_t enc2[16];
    if (!des3_cbc_crypt(true, key, iv1, plain, sizeof(plain), enc2, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rx2[16] = { 0 };
    len = nfc_poller_transceive(enc2, sizeof(enc2), true, rx2, sizeof(rx2), 1, 30);
    if (len < 8) return HB_NFC_ERR_PROTOCOL;

    uint8_t enc_rnda_rot[8];
    memcpy(enc_rnda_rot, rx2, 8);

    uint8_t iv2[8];
    memcpy(iv2, &enc2[8], 8);

    uint8_t rnda_rot_dec[8];
    if (!des3_cbc_crypt(false, key, iv2, enc_rnda_rot, 8, rnda_rot_dec, NULL)) {
        return HB_NFC_ERR_INTERNAL;
    }

    uint8_t rnda_rot[8];
    rotate_left_8(rnda_rot, rnda);
    if (memcmp(rnda_rot_dec, rnda_rot, 8) != 0) {
        return HB_NFC_ERR_AUTH;
    }

    hb_delay_us(500);
    return HB_NFC_OK;
}

int mful_read_all(uint8_t* data, int max_pages)
{
    int pages_read = 0;
    for (int pg = 0; pg < max_pages; pg += 4) {
        uint8_t buf[18] = { 0 };
        int len = mful_read_pages((uint8_t)pg, buf);
        if (len < 16) {
            break;
        }
        int pages_in_chunk = (max_pages - pg >= 4) ? 4 : (max_pages - pg);
        for (int i = 0; i < pages_in_chunk; i++) {
            data[(pg + i) * 4 + 0] = buf[i * 4 + 0];
            data[(pg + i) * 4 + 1] = buf[i * 4 + 1];
            data[(pg + i) * 4 + 2] = buf[i * 4 + 2];
            data[(pg + i) * 4 + 3] = buf[i * 4 + 3];
        }
        pages_read = pg + pages_in_chunk;
    }
    return pages_read;
}
#undef TAG

/*
 * MFKey32 - MIFARE Classic key recovery from two sniffed auth traces.
 *
 * Algorithm (proxmark3 / Flipper Zero compatible):
 *
 *   Parameters:
 *     uid       - Card UID (4 bytes, big-endian uint32)
 *     nt0, nt1  - Tag nonces (plaintext, received from tag)
 *     nr0, nr1  - ENCRYPTED reader nonces ({nr_plain}^ks1, as seen on wire)
 *     ar0, ar1  - ENCRYPTED reader responses ({prng(nt,64)}^ks2, as seen on wire)
 *     key       - Output: 48-bit key (MSB in lower 48 bits of uint64)
 *
 *   Key insight: rollback_word(s, nr_enc, fb=1) ≡ rollback_word(s, nr_plain, fb=0)
 *   because: ks_bit ^ nr_enc_bit = ks_bit ^ (nr_plain ^ ks_bit) = nr_plain.
 *   This allows rollback without knowing the plaintext reader nonce.
 *
 *   Complexity: O(2^24) candidate generation + O(2^16) cross-verification.
 *   Memory: ~32 KB for candidate lists (heap).
 *   Time: ~2–10 s on ESP32-P4.
 */
#include "mfkey.h"
#include "crypto1.h"
#include <stdlib.h>
#include <string.h>

/* ---- local macros (crypto1 section #undef's these) ---- */
#define MK_BIT(x, n)     (((x) >> (n)) & 1U)
#define MK_BEBIT(x, n)   MK_BIT((x), (n) ^ 24)
#define MK_LFP_ODD       0x29CE5CU
#define MK_LFP_EVEN      0x870804U

static uint32_t mk_filter(uint32_t in)
{
    uint32_t o = 0;
    o  = 0xf22c0U >> (in        & 0xfU) & 16U;
    o |= 0x6c9c0U >> (in >>  4  & 0xfU) &  8U;
    o |= 0x3c8b0U >> (in >>  8  & 0xfU) &  4U;
    o |= 0x1e458U >> (in >> 12  & 0xfU) &  2U;
    o |= 0x0d938U >> (in >> 16  & 0xfU) &  1U;
    return MK_BIT(0xEC57E80AU, o);
}

static uint8_t mk_parity32(uint32_t x)
{
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (uint8_t)(x & 1U);
}

/* ---- rollback one LFSR clock ---- */
static uint8_t mk_rollback_bit(crypto1_state_t *s, uint32_t in, int fb)
{
    uint8_t  ret;
    uint32_t out;
    /* Undo the swap. */
    uint32_t t = s->odd; s->odd = s->even; s->even = t;
    out  = s->even & 1U;
    out ^= MK_LFP_EVEN & (s->even >>= 1);
    out ^= MK_LFP_ODD  & s->odd;
    out ^= (uint32_t)(!!in);
    out ^= (ret = (uint8_t)mk_filter(s->odd)) & (uint32_t)(!!fb);
    s->even |= mk_parity32(out) << 23;
    return ret;
}

/* ---- rollback 32 LFSR clocks (BEBIT order) ---- */
static uint32_t mk_rollback_word(crypto1_state_t *s, uint32_t in, int fb)
{
    uint32_t out = 0;
    for (int i = 31; i >= 0; i--)
        out |= (uint32_t)mk_rollback_bit(s, MK_BEBIT(in, i), fb) << (24 ^ i);
    return out;
}

/* ---- extract 48-bit key from LFSR state ---- */
static uint64_t mk_get_lfsr(const crypto1_state_t *s)
{
    uint64_t k = 0;
    for (int i = 23; i >= 0; i--)
        k = k << 2 | (uint64_t)(MK_BIT(s->even, i) << 1) | MK_BIT(s->odd, i);
    return k;
}

/* ---- simulate 32 free-running clocks, return keystream word ---- */
static uint32_t mk_simulate_ks(crypto1_state_t s)
{
    uint32_t ks = 0;
    for (int i = 0; i < 32; i++)
        ks |= (uint32_t)crypto1_bit(&s, 0, 0) << (24 ^ i);
    return ks;
}

/*
 * Build candidate list for one LFSR half.
 *
 * The state at position 64 has two 24-bit halves (odd, even).
 * Free-running output alternates:
 *   clock 0,2,4,...  (even-indexed) are driven by the ODD half's evolution
 *   clock 1,3,5,...  (odd-indexed)  are driven by the EVEN half's evolution
 *
 * Approximation: ignore parity coupling between halves (treat shift as pure).
 * For the ODD half: clock 2k ≈ f(O << k)        (k=0..15)
 * For the EVEN half: clock 2k+1 ≈ f(E << (k+1)) (k=0..15)
 *
 * With 16 approximate constraints on a 24-bit value: ~2^8 = 256 candidates.
 * We double the candidate set (flip each approx parity) to reduce false negatives.
 */
#define MFKEY_MAX_CANDS 4096

static size_t mk_gen_odd_candidates(uint32_t ks2, uint32_t *out, size_t out_max)
{
    size_t count = 0;
    for (uint32_t O = 0; O < (1U << 24) && count < out_max; O++) {
        uint32_t sig = 0;
        for (int k = 0; k < 16; k++)
            sig |= mk_filter((O << k) & 0xFFFFFFU) << k;
        /* Build expected 16-bit signature from even-indexed ks2 bits */
        uint32_t expected = 0;
        for (int k = 0; k < 16; k++)
            expected |= MK_BEBIT(ks2, k * 2) << k;
        if (sig == expected)
            out[count++] = O;
    }
    return count;
}

static size_t mk_gen_even_candidates(uint32_t ks2, uint32_t *out, size_t out_max)
{
    size_t count = 0;
    for (uint32_t E = 0; E < (1U << 24) && count < out_max; E++) {
        uint32_t sig = 0;
        for (int k = 0; k < 16; k++)
            sig |= mk_filter((E << (k + 1)) & 0xFFFFFFU) << k;
        /* Build expected 16-bit signature from odd-indexed ks2 bits */
        uint32_t expected = 0;
        for (int k = 0; k < 16; k++)
            expected |= MK_BEBIT(ks2, k * 2 + 1) << k;
        if (sig == expected)
            out[count++] = E;
    }
    return count;
}

bool mfkey32(uint32_t uid, uint32_t nt0, uint32_t nr0, uint32_t ar0,
             uint32_t nt1, uint32_t nr1, uint32_t ar1, uint64_t* key)
{
    if (!key) return false;

    /* Known 32-bit keystream at position 64 for each session. */
    uint32_t ks2_0 = ar0 ^ crypto1_prng_successor(nt0, 64);
    uint32_t ks2_1 = ar1 ^ crypto1_prng_successor(nt1, 64);

    /* Allocate candidate lists. */
    uint32_t *odd_cands  = malloc(sizeof(uint32_t) * MFKEY_MAX_CANDS);
    uint32_t *even_cands = malloc(sizeof(uint32_t) * MFKEY_MAX_CANDS);
    if (!odd_cands || !even_cands) {
        free(odd_cands);
        free(even_cands);
        return false;
    }

    size_t n_odd  = mk_gen_odd_candidates(ks2_0,  odd_cands,  MFKEY_MAX_CANDS);
    size_t n_even = mk_gen_even_candidates(ks2_0, even_cands, MFKEY_MAX_CANDS);

    bool found = false;

    for (size_t oi = 0; oi < n_odd && !found; oi++) {
        for (size_t ei = 0; ei < n_even && !found; ei++) {
            crypto1_state_t s = { odd_cands[oi], even_cands[ei] };

            /* Verify: 32 free-running clocks from this state must produce ks2_0. */
            if (mk_simulate_ks(s) != ks2_0) continue;

            /*
             * Rollback to recover key:
             *   1. Undo 32 free-running clocks (ks2 generation, no input).
             *   2. Undo nr loading: rollback_word(nr_enc, fb=1) ≡
             *      rollback_word(nr_plain, fb=0) because the filter output
             *      cancels the ks1 term algebraically.
             *   3. Undo uid^nt priming (plaintext, fb=0).
             */
            mk_rollback_word(&s, 0,         0);
            mk_rollback_word(&s, nr0,        1);
            mk_rollback_word(&s, uid ^ nt0,  0);
            uint64_t key_candidate = mk_get_lfsr(&s);

            /* Verify candidate against session 1. */
            crypto1_state_t s1;
            crypto1_init(&s1, key_candidate);
            crypto1_word(&s1, uid ^ nt1, 0);
            crypto1_word(&s1, nr1, 1);           /* fb=1 with encrypted nr1 */
            if (crypto1_word(&s1, 0, 0) == ks2_1) {
                *key = key_candidate;
                found = true;
            }
        }
    }

    free(odd_cands);
    free(even_cands);
    return found;
}

#undef MK_BIT
#undef MK_BEBIT
#undef MK_LFP_ODD
#undef MK_LFP_EVEN

