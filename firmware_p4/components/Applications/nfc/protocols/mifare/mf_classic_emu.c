/**
 * @file mf_classic_emu.c
 * @brief MIFARE Classic card emulation.
 */

#include "mf_classic_emu.h"
#include "crypto1.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_gpio.h"
#include "hb_nfc_timer.h"
#include "iso14443a.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "mfc_emu";

static struct {
    mfc_emu_card_data_t card;
    mfc_emu_state_t     state;
    mfc_emu_stats_t     stats;

    crypto1_state_t     crypto;
    bool                crypto_active;
    uint32_t            auth_nt;
    int                 auth_sector;
    mf_key_type_t       auth_key_type;

    uint32_t            prng_state;

    uint8_t             pending_block;
    uint8_t             pending_cmd;
    int32_t             pending_value;

    mfc_emu_event_cb_t  cb;
    void*               cb_ctx;

    int64_t             last_activity_us;
    bool                initialized;
} s_emu = { 0 };

static mfc_emu_state_t handle_auth(uint8_t auth_cmd, uint8_t block_num);
static mfc_emu_state_t handle_read(uint8_t block_num);
static mfc_emu_state_t handle_write_phase1(uint8_t block_num);
static mfc_emu_state_t handle_write_phase2(const uint8_t* data, int len);
static mfc_emu_state_t handle_value_op_phase1(uint8_t cmd, uint8_t block_num);
static mfc_emu_state_t handle_value_op_phase2(const uint8_t* data, int len);
static mfc_emu_state_t handle_transfer(uint8_t block_num);
static mfc_emu_state_t handle_halt(void);
static void emit_event(mfc_emu_event_type_t type);
static hb_nfc_err_t load_pt_memory(void);

static uint32_t emu_prng_next(void)
{
    s_emu.prng_state = crypto1_prng_successor(s_emu.prng_state, 1);
    return s_emu.prng_state;
}

static uint32_t bytes_to_u32_be(const uint8_t* b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static uint32_t bytes_to_u32_le(const uint8_t* b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline uint8_t bit_get(const uint8_t* buf, size_t bitpos)
{
    return (uint8_t)((buf[bitpos >> 3] >> (bitpos & 7U)) & 1U);
}

static inline uint8_t bit_get_msb(const uint8_t* buf, size_t bitpos)
{
    return (uint8_t)((buf[bitpos >> 3] >> (7U - (bitpos & 7U))) & 1U);
}

static inline uint8_t bit_reverse8(uint8_t v)
{
    v = (uint8_t)(((v & 0xF0U) >> 4) | ((v & 0x0FU) << 4));
    v = (uint8_t)(((v & 0xCCU) >> 2) | ((v & 0x33U) << 2));
    v = (uint8_t)(((v & 0xAAU) >> 1) | ((v & 0x55U) << 1));
    return v;
}

static int unpack_9bit_bytes(const uint8_t* packed, size_t packed_len,
                             uint8_t* out, size_t out_len)
{
    size_t bitpos = 0;
    size_t bits_avail = packed_len * 8U;
    for (size_t i = 0; i < out_len; i++) {
        if (bitpos + 9U > bits_avail) return -1;
        uint8_t b = 0;
        for (int bit = 0; bit < 8; bit++) {
            b |= (uint8_t)(bit_get(packed, bitpos++) << bit);
        }
        (void)bit_get(packed, bitpos++); /* parity bit (encrypted) */
        out[i] = b;
    }
    return (int)out_len;
}

static int unpack_9bit_bytes_msb(const uint8_t* packed, size_t packed_len,
                                 uint8_t* out, size_t out_len)
{
    size_t bitpos = 0;
    size_t bits_avail = packed_len * 8U;
    for (size_t i = 0; i < out_len; i++) {
        if (bitpos + 9U > bits_avail) return -1;
        uint8_t b = 0;
        for (int bit = 0; bit < 8; bit++) {
            b |= (uint8_t)(bit_get_msb(packed, bitpos++) << bit);
        }
        (void)bit_get_msb(packed, bitpos++); /* parity bit (encrypted) */
        out[i] = b;
    }
    return (int)out_len;
}

static bool auth_try_decode(const uint8_t nr_ar_enc[8],
                            uint32_t ar_expected,
                            const crypto1_state_t* st_in,
                            crypto1_state_t* st_out,
                            uint32_t* ar_be_out,
                            uint32_t* ar_le_out)
{
    crypto1_state_t st_be = *st_in;
    crypto1_state_t st_le = *st_in;

    uint32_t nr_enc_be = bytes_to_u32_be(nr_ar_enc);
    uint32_t nr_enc_le = bytes_to_u32_le(nr_ar_enc);
    (void)crypto1_word(&st_be, nr_enc_be, 1);
    (void)crypto1_word(&st_le, nr_enc_le, 1);

    uint32_t ar_enc_be = bytes_to_u32_be(&nr_ar_enc[4]);
    uint32_t ar_enc_le = bytes_to_u32_le(&nr_ar_enc[4]);

    uint32_t ar_be = ar_enc_be ^ crypto1_word(&st_be, 0, 0);
    uint32_t ar_le = ar_enc_le ^ crypto1_word(&st_le, 0, 0);

    if (ar_be_out) *ar_be_out = ar_be;
    if (ar_le_out) *ar_le_out = ar_le;

    if (ar_be == ar_expected) {
        *st_out = st_be;
        return true;
    }
    if (ar_le == ar_expected) {
        *st_out = st_le;
        return true;
    }
    return false;
}

static void u32_to_bytes_be(uint32_t v, uint8_t* b)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)(v);
}

static uint32_t get_cuid(void)
{
    const uint8_t* uid = s_emu.card.uid;
    if (s_emu.card.uid_len == 4) return bytes_to_u32_be(uid);
    return bytes_to_u32_be(&uid[3]);
}

static bool get_key_for_sector(int sector, mf_key_type_t key_type, uint64_t* key_out)
{
    if (sector < 0 || sector >= s_emu.card.sector_count) return false;

    const uint8_t* kdata;
    bool known;

    if (key_type == MF_KEY_A) {
        kdata = s_emu.card.keys[sector].key_a;
        known = s_emu.card.keys[sector].key_a_known;
    } else {
        kdata = s_emu.card.keys[sector].key_b;
        known = s_emu.card.keys[sector].key_b_known;
    }

    if (!known) return false;

    *key_out = ((uint64_t)kdata[0] << 40) | ((uint64_t)kdata[1] << 32) |
               ((uint64_t)kdata[2] << 24) | ((uint64_t)kdata[3] << 16) |
               ((uint64_t)kdata[4] << 8)  | (uint64_t)kdata[5];
    return true;
}

static int block_to_sector(int block)
{
    if (block < 128) return block / 4;
    return 32 + (block - 128) / 16;
}

static int sector_first_block(int sector)
{
    if (sector < 32) return sector * 4;
    return 128 + (sector - 32) * 16;
}

static int sector_block_count(int sector)
{
    return (sector < 32) ? 4 : 16;
}

static int sector_trailer_block(int sector)
{
    return sector_first_block(sector) + sector_block_count(sector) - 1;
}

static int block_index_in_sector(int block)
{
    int sector = block_to_sector(block);
    return block - sector_first_block(sector);
}

bool mfc_emu_get_access_bits(const uint8_t trailer[16], int block_in_sector,
                              uint8_t* c1, uint8_t* c2, uint8_t* c3)
{
    int grp = block_in_sector;
    if (grp > 3) grp = block_in_sector / 5;
    if (grp > 3) grp = 3;

    uint8_t b6 = trailer[6], b7 = trailer[7], b8 = trailer[8];

    *c1 = (b7 >> (4 + grp)) & 1;
    *c2 = (b8 >> grp) & 1;
    *c3 = (b8 >> (4 + grp)) & 1;

    uint8_t c1_inv = (~b6 >> grp) & 1;
    uint8_t c2_inv = (~b6 >> (4 + grp)) & 1;
    uint8_t c3_inv = (~b7 >> grp) & 1;

    return (*c1 == c1_inv) && (*c2 == c2_inv) && (*c3 == c3_inv);
}

bool mfc_emu_can_read(const uint8_t trailer[16], int block_in_sector,
                       mf_key_type_t auth_key_type)
{
    uint8_t c1, c2, c3;
    if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
        return false;

    uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
    bool is_b = (auth_key_type == MF_KEY_B);
    int grp = block_in_sector;
    if (grp == 3 || grp == 15) return true;

    switch (ac) {
    case 0: case 1: case 2: case 3: case 4: return true;
    case 5: case 6: return is_b;
    default: return false;
    }
}

bool mfc_emu_can_write(const uint8_t trailer[16], int block_in_sector,
                        mf_key_type_t auth_key_type)
{
    uint8_t c1, c2, c3;
    if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
        return false;

    uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
    bool is_b = (auth_key_type == MF_KEY_B);

    switch (ac) {
    case 0:              return true;
    case 3: case 4: case 6: return is_b;
    default:             return false;
    }
}

bool mfc_emu_can_increment(const uint8_t trailer[16], int block_in_sector,
                            mf_key_type_t auth_key_type)
{
    uint8_t c1, c2, c3;
    if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
        return false;

    uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
    bool is_b = (auth_key_type == MF_KEY_B);

    switch (ac) {
    case 0:  return true;
    case 6:  return is_b;
    default: return false;
    }
}

bool mfc_emu_can_decrement(const uint8_t trailer[16], int block_in_sector,
                            mf_key_type_t auth_key_type)
{
    uint8_t c1, c2, c3;
    if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
        return false;

    uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
    (void)auth_key_type;

    switch (ac) {
    case 0: case 1: case 6: return true;
    default:                return false;
    }
}

static const uint8_t* get_trailer_for_block(uint8_t block_num)
{
    int sector = block_to_sector(block_num);
    int tb = sector_trailer_block(sector);
    if (tb >= 0 && tb < s_emu.card.total_blocks)
        return s_emu.card.blocks[tb];
    return NULL;
}

static hb_nfc_err_t target_tx_raw(const uint8_t* data, size_t len_bytes, uint8_t extra_bits)
{
    hb_spi_direct_cmd(0xDB);                        /* Clear FIFO (0xDB, not 0xC2!) */
    hb_spi_reg_modify(REG_OP_CTRL, 0x08, 0x08);    /* tx_en on */
    st25r_set_tx_bytes((uint16_t)len_bytes, extra_bits);
    st25r_fifo_load(data, len_bytes);
    st25r_irq_read();
    hb_spi_direct_cmd(CMD_TX_WO_CRC);

    if (!st25r_irq_wait_txe()) {
        ESP_LOGW(TAG, "TX raw timeout");
        hb_spi_reg_modify(REG_OP_CTRL, 0x08, 0x00);
        return HB_NFC_ERR_TX_TIMEOUT;
    }

    hb_spi_reg_modify(REG_OP_CTRL, 0x08, 0x00);    /* tx_en off */
    return HB_NFC_OK;
}

/* Interrupt-based RX: waits for RXS/RXE instead of polling FIFO count.
 * FIFO resets at RXS (datasheet), so stale TX data is auto-cleared. */
static int target_rx_irq(uint8_t* buf, size_t buf_max, int timeout_ms)
{
    int polls = timeout_ms * 10;
    bool rxs_seen = false;

    for (int i = 0; i < polls; i++) {
        if (hb_gpio_irq_level()) {
            st25r_irq_status_t s = st25r_irq_read();

            if (s.main & IRQ_MAIN_RXE) {
                uint16_t count = st25r_fifo_count();
                int to_read = (int)count;
                if (to_read > (int)buf_max) to_read = (int)buf_max;
                if (to_read > 0) st25r_fifo_read(buf, (size_t)to_read);
                return to_read;
            }

            if (s.main & IRQ_MAIN_RXS) rxs_seen = true;

            if (s.error) {
                uint16_t count = st25r_fifo_count();
                int to_read = (int)count;
                if (to_read > (int)buf_max) to_read = (int)buf_max;
                if (to_read > 0) st25r_fifo_read(buf, (size_t)to_read);
                return to_read > 0 ? to_read : -1;
            }

            if (s.collision) return -1;
        }
        hb_delay_us(100);

        if ((i % 50) == 49) vTaskDelay(1);
    }

    if (rxs_seen) {
        uint16_t count = st25r_fifo_count();
        int to_read = (int)count;
        if (to_read > (int)buf_max) to_read = (int)buf_max;
        if (to_read > 0) st25r_fifo_read(buf, (size_t)to_read);
        return to_read;
    }

    return 0;
}

static int fifo_rx(uint8_t* buf, size_t buf_max)
{
    uint8_t fs1 = 0;
    hb_spi_reg_read(REG_FIFO_STATUS1, &fs1);
    int n = fs1 & 0x7F;
    if (n <= 0) return 0;
    if (n > (int)buf_max) n = (int)buf_max;
    st25r_fifo_read(buf, (size_t)n);
    return n;
}

/* Legacy FIFO-polling RX (kept for non-auth commands) */
static int target_rx_blocking(uint8_t* buf, size_t buf_max, int timeout_ms)
{
    int polls = timeout_ms * 2;
    for (int i = 0; i < polls; i++) {
        uint16_t count = st25r_fifo_count();
        if (count > 0) {
            hb_delay_us(2000);
            count = st25r_fifo_count();
            int to_read = (int)count;
            if (to_read > (int)buf_max) to_read = (int)buf_max;
            st25r_fifo_read(buf, (size_t)to_read);
            return to_read;
        }

        uint8_t main_irq = 0;
        hb_spi_reg_read(REG_MAIN_INT, &main_irq);
        if (main_irq & IRQ_MAIN_COL) return -1;

        hb_delay_us(500);
    }
    return 0;
}

static void crypto1_encrypt_with_parity(const uint8_t* plain, size_t len,
                                         uint8_t* packed, size_t* packed_bits)
{
    memset(packed, 0, (len * 9 + 7) / 8 + 1);
    size_t bit_pos = 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t ks      = crypto1_byte(&s_emu.crypto, 0, 0);
        uint8_t enc_byte = plain[i] ^ ks;
        uint8_t par_ks  = crypto1_filter_output(&s_emu.crypto);
        uint8_t enc_par = crypto1_odd_parity8(plain[i]) ^ par_ks;

        for (int b = 0; b < 8; b++) {
            if ((enc_byte >> b) & 1)
                packed[bit_pos >> 3] |= (uint8_t)(1U << (bit_pos & 7));
            bit_pos++;
        }
        if (enc_par & 1)
            packed[bit_pos >> 3] |= (uint8_t)(1U << (bit_pos & 7));
        bit_pos++;
    }
    *packed_bits = bit_pos;
}

static hb_nfc_err_t target_tx_encrypted(const uint8_t* plain, size_t len)
{
    uint8_t packed[24] = { 0 };
    size_t  total_bits = 0;

    if (len > 18) return HB_NFC_ERR_PARAM;
    crypto1_encrypt_with_parity(plain, len, packed, &total_bits);

    size_t  full_bytes = total_bits / 8;
    uint8_t extra_bits = (uint8_t)(total_bits % 8);
    return target_tx_raw(packed, full_bytes + (extra_bits ? 1 : 0), extra_bits);
}

static hb_nfc_err_t target_tx_ack_encrypted(uint8_t ack_nack)
{
    uint8_t enc = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t ks_bit    = crypto1_bit(&s_emu.crypto, 0, 0);
        uint8_t plain_bit = (ack_nack >> i) & 1;
        enc |= (uint8_t)((plain_bit ^ ks_bit) << i);
    }

    hb_spi_direct_cmd(0xDB);
    hb_spi_reg_modify(REG_OP_CTRL, 0x08, 0x08);
    st25r_fifo_load(&enc, 1);
    st25r_set_tx_bytes(0, 4);
    st25r_irq_read();
    hb_spi_direct_cmd(CMD_TX_WO_CRC);

    if (!st25r_irq_wait_txe()) {
        ESP_LOGW(TAG, "ACK TX timeout");
        hb_spi_reg_modify(REG_OP_CTRL, 0x08, 0x00);
        return HB_NFC_ERR_TX_TIMEOUT;
    }
    hb_spi_reg_modify(REG_OP_CTRL, 0x08, 0x00);
    return HB_NFC_OK;
}

static mfc_emu_event_t s_evt;

static void emit_event(mfc_emu_event_type_t type)
{
    if (!s_emu.cb) return;
    s_evt.type = type;
    s_emu.cb(&s_evt, s_emu.cb_ctx);
}

static void reset_crypto_state(void)
{
    s_emu.crypto_active = false;
    crypto1_reset(&s_emu.crypto);
    hb_spi_reg_modify(REG_ISO14443A,
                      ISO14443A_NO_TX_PAR | ISO14443A_NO_RX_PAR, 0);
}

static mfc_emu_state_t handle_auth(uint8_t auth_cmd, uint8_t block_num)
{
    s_emu.stats.total_auths++;

    mf_key_type_t key_type = (auth_cmd == MFC_CMD_AUTH_KEY_A) ? MF_KEY_A : MF_KEY_B;
    int sector = block_to_sector(block_num);

    ESP_LOGI(TAG, "AUTH Key%c block=%d sector=%d",
             key_type == MF_KEY_A ? 'A' : 'B', block_num, sector);

    if (s_emu.crypto_active) reset_crypto_state();

    uint64_t key;
    if (!get_key_for_sector(sector, key_type, &key)) {
        ESP_LOGW(TAG, "Key not found for sector %d", sector);
        s_emu.stats.failed_auths++;
        s_evt.auth.sector   = sector;
        s_evt.auth.key_type = key_type;
        emit_event(MFC_EMU_EVT_AUTH_FAIL);
        return MFC_EMU_STATE_ACTIVATED;
    }

    uint32_t nt = emu_prng_next();
    s_emu.auth_nt       = nt;
    s_emu.auth_sector   = sector;
    s_emu.auth_key_type = key_type;

    uint8_t nt_bytes[4];
    u32_to_bytes_be(nt, nt_bytes);

    hb_nfc_err_t err = target_tx_raw(nt_bytes, 4, 0);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "Failed to send nt");
        s_emu.stats.failed_auths++;
        return MFC_EMU_STATE_ERROR;
    }

    uint32_t cuid = get_cuid();
    crypto1_init(&s_emu.crypto, key);
    crypto1_word(&s_emu.crypto, nt ^ cuid, 0);
    s_emu.crypto_active = true;

    uint8_t nr_ar_raw[16] = { 0 };
    int len = target_rx_irq(nr_ar_raw, sizeof(nr_ar_raw), 50);

    uint8_t* nr_ar_packed = nr_ar_raw;

    if (len < 8) {
        ESP_LOGW(TAG, "No {nr}{ar} received (got %d bytes)", len);
        if (len > 0) {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, nr_ar_raw, (size_t)len, ESP_LOG_WARN);
        }
        reset_crypto_state();
        s_emu.stats.failed_auths++;
        return MFC_EMU_STATE_ACTIVATED;
    }

    uint8_t nr_ar_enc[8] = { 0 };
    uint32_t ar_expected = crypto1_prng_successor(nt, 64);

    crypto1_state_t st_match = s_emu.crypto;
    uint32_t ar_be = 0, ar_le = 0;
    const char* mode = NULL;
    bool matched = false;

    if (len >= 9) {
        size_t use_len = (len > 9) ? 9U : (size_t)len;
        if (unpack_9bit_bytes(nr_ar_packed, use_len, nr_ar_enc, sizeof(nr_ar_enc)) >= 0) {
            if (auth_try_decode(nr_ar_enc, ar_expected, &s_emu.crypto,
                                &st_match, &ar_be, &ar_le)) {
                matched = true;
                mode = "9b-lsb";
            }
        }
    }

    if (!matched && len >= 9) {
        size_t use_len = (len > 9) ? 9U : (size_t)len;
        if (unpack_9bit_bytes_msb(nr_ar_packed, use_len, nr_ar_enc, sizeof(nr_ar_enc)) >= 0) {
            if (auth_try_decode(nr_ar_enc, ar_expected, &s_emu.crypto,
                                &st_match, &ar_be, &ar_le)) {
                matched = true;
                mode = "9b-msb";
            }
        }
    }

    if (!matched) {
        memcpy(nr_ar_enc, nr_ar_packed, 8);
        if (auth_try_decode(nr_ar_enc, ar_expected, &s_emu.crypto,
                            &st_match, &ar_be, &ar_le)) {
            matched = true;
            mode = "8b-raw";
        }
    }

    if (!matched) {
        for (int i = 0; i < 8; i++) nr_ar_enc[i] = bit_reverse8(nr_ar_packed[i]);
        if (auth_try_decode(nr_ar_enc, ar_expected, &s_emu.crypto,
                            &st_match, &ar_be, &ar_le)) {
            matched = true;
            mode = "8b-rev";
        }
    }

    if (!matched) {
        ESP_LOGW(TAG, "AUTH FAIL: ar_be=0x%08lX ar_le=0x%08lX expected=0x%08lX len=%d",
                 (unsigned long)ar_be, (unsigned long)ar_le,
                 (unsigned long)ar_expected, len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, nr_ar_packed, (size_t)len, ESP_LOG_WARN);
        reset_crypto_state();
        s_emu.stats.failed_auths++;
        s_evt.auth.sector   = sector;
        s_evt.auth.key_type = key_type;
        emit_event(MFC_EMU_EVT_AUTH_FAIL);
        return MFC_EMU_STATE_ACTIVATED;
    }

    s_emu.crypto = st_match;
    if (mode) ESP_LOGD(TAG, "AUTH decode ok (mode=%s)", mode);

    uint32_t at = crypto1_prng_successor(nt, 96);
    uint8_t at_bytes[4];
    u32_to_bytes_be(at, at_bytes);

    hb_spi_reg_modify(REG_ISO14443A,
                      ISO14443A_NO_TX_PAR,
                      ISO14443A_NO_TX_PAR);

    err = target_tx_encrypted(at_bytes, 4);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "Failed to send at");
        reset_crypto_state();
        s_emu.stats.failed_auths++;
        return MFC_EMU_STATE_ERROR;
    }

    s_emu.stats.successful_auths++;
    s_emu.last_activity_us = esp_timer_get_time();

    ESP_LOGI(TAG, "AUTH OK: sector %d Key%c",
             sector, key_type == MF_KEY_A ? 'A' : 'B');

    s_evt.auth.sector   = sector;
    s_evt.auth.key_type = key_type;
    emit_event(MFC_EMU_EVT_AUTH_SUCCESS);

    return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_read(uint8_t block_num)
{
    if (block_num >= s_emu.card.total_blocks) {
        ESP_LOGW(TAG, "READ invalid block %d", block_num);
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    int sector = block_to_sector(block_num);
    if (sector != s_emu.auth_sector) {
        ESP_LOGW(TAG, "READ block %d not in auth sector %d", block_num, s_emu.auth_sector);
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    const uint8_t* trailer = get_trailer_for_block(block_num);
    if (trailer) {
        int bidx = block_index_in_sector(block_num);
        if (!mfc_emu_can_read(trailer, bidx, s_emu.auth_key_type)) {
            ESP_LOGW(TAG, "READ block %d denied by AC", block_num);
            target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
            s_emu.stats.nacks_sent++;
            return MFC_EMU_STATE_AUTHENTICATED;
        }
    }

    ESP_LOGI(TAG, "READ block %d (sector %d)", block_num, sector);

    uint8_t resp[18];
    memcpy(resp, s_emu.card.blocks[block_num], 16);

    int tb = sector_trailer_block(sector);
    if (block_num == tb) {
        memset(resp, 0x00, 6);
        uint8_t c1, c2, c3;
        mfc_emu_get_access_bits(s_emu.card.blocks[tb], 3, &c1, &c2, &c3);
        uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
        if (ac > 2) memset(&resp[10], 0x00, 6);
    }

    iso14443a_crc(resp, 16, &resp[16]);

    hb_nfc_err_t err = target_tx_encrypted(resp, 18);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "READ TX failed");
        return MFC_EMU_STATE_ERROR;
    }

    s_emu.stats.reads_served++;
    s_emu.last_activity_us = esp_timer_get_time();

    s_evt.read.block = block_num;
    emit_event(MFC_EMU_EVT_READ);

    return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_write_phase1(uint8_t block_num)
{
    if (block_num >= s_emu.card.total_blocks || block_num == 0) {
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    int sector = block_to_sector(block_num);
    if (sector != s_emu.auth_sector) {
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    const uint8_t* trailer = get_trailer_for_block(block_num);
    if (trailer) {
        int bidx = block_index_in_sector(block_num);
        if (!mfc_emu_can_write(trailer, bidx, s_emu.auth_key_type)) {
            ESP_LOGW(TAG, "WRITE block %d denied by AC", block_num);
            target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
            s_emu.stats.writes_blocked++;
            s_evt.write.block = block_num;
            emit_event(MFC_EMU_EVT_WRITE_BLOCKED);
            return MFC_EMU_STATE_AUTHENTICATED;
        }
    }

    ESP_LOGI(TAG, "WRITE phase 1: block %d - ACK", block_num);

    hb_nfc_err_t err = target_tx_ack_encrypted(MFC_ACK);
    if (err != HB_NFC_OK) return MFC_EMU_STATE_ERROR;

    s_emu.pending_block = block_num;
    s_emu.pending_cmd   = MFC_CMD_WRITE;

    return MFC_EMU_STATE_WRITE_PENDING;
}

static mfc_emu_state_t handle_write_phase2(const uint8_t* data, int len)
{
    if (len < 18) {
        target_tx_ack_encrypted(MFC_NACK_PARITY_CRC);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    uint8_t crc[2];
    iso14443a_crc(data, 16, crc);
    if (data[16] != crc[0] || data[17] != crc[1]) {
        ESP_LOGW(TAG, "WRITE phase 2: CRC mismatch");
        target_tx_ack_encrypted(MFC_NACK_PARITY_CRC);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    uint8_t block_num = s_emu.pending_block;
    ESP_LOGI(TAG, "WRITE phase 2: block %d", block_num);

    memcpy(s_emu.card.blocks[block_num], data, 16);

    int sector = block_to_sector(block_num);
    if (block_num == sector_trailer_block(sector)) {
        memcpy(s_emu.card.keys[sector].key_a, data, 6);
        s_emu.card.keys[sector].key_a_known = true;
        memcpy(s_emu.card.keys[sector].key_b, &data[10], 6);
        s_emu.card.keys[sector].key_b_known = true;
    }

    hb_nfc_err_t err = target_tx_ack_encrypted(MFC_ACK);
    if (err != HB_NFC_OK) return MFC_EMU_STATE_ERROR;

    s_emu.stats.writes_served++;
    s_emu.last_activity_us = esp_timer_get_time();

    s_evt.write.block = block_num;
    emit_event(MFC_EMU_EVT_WRITE);

    return MFC_EMU_STATE_AUTHENTICATED;
}

static bool is_value_block_format(const uint8_t* data)
{
    uint32_t v1 = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    uint32_t v2 = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    uint32_t v3 = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);

    if (v1 != v3) return false;
    if ((v1 ^ v2) != 0xFFFFFFFF) return false;
    if (data[12] != data[14]) return false;
    if ((data[12] ^ data[13]) != 0xFF) return false;
    if ((data[14] ^ data[15]) != 0xFF) return false;
    return true;
}

static int32_t read_value_from_block(const uint8_t* data)
{
    return (int32_t)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

static void write_value_to_block(uint8_t* data, int32_t value, uint8_t addr)
{
    uint32_t v  = (uint32_t)value;
    uint32_t nv = ~v;

    data[0] = v & 0xFF;  data[1] = (v >> 8) & 0xFF;
    data[2] = (v >> 16) & 0xFF; data[3] = (v >> 24) & 0xFF;
    data[4] = nv & 0xFF; data[5] = (nv >> 8) & 0xFF;
    data[6] = (nv >> 16) & 0xFF; data[7] = (nv >> 24) & 0xFF;
    data[8] = v & 0xFF;  data[9] = (v >> 8) & 0xFF;
    data[10] = (v >> 16) & 0xFF; data[11] = (v >> 24) & 0xFF;
    data[12] = addr; data[13] = ~addr;
    data[14] = addr; data[15] = ~addr;
}

static mfc_emu_state_t handle_value_op_phase1(uint8_t cmd, uint8_t block_num)
{
    if (block_num >= s_emu.card.total_blocks) {
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    int sector = block_to_sector(block_num);
    if (sector != s_emu.auth_sector ||
        block_num == (uint8_t)sector_trailer_block(sector)) {
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    const uint8_t* trailer = get_trailer_for_block(block_num);
    if (trailer) {
        int  bidx    = block_index_in_sector(block_num);
        bool allowed = (cmd == MFC_CMD_INCREMENT)
                       ? mfc_emu_can_increment(trailer, bidx, s_emu.auth_key_type)
                       : mfc_emu_can_decrement(trailer, bidx, s_emu.auth_key_type);
        if (!allowed) {
            target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
            s_emu.stats.nacks_sent++;
            return MFC_EMU_STATE_AUTHENTICATED;
        }
    }

    if (!is_value_block_format(s_emu.card.blocks[block_num])) {
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    hb_nfc_err_t err = target_tx_ack_encrypted(MFC_ACK);
    if (err != HB_NFC_OK) return MFC_EMU_STATE_ERROR;

    s_emu.pending_block = block_num;
    s_emu.pending_cmd   = cmd;

    return MFC_EMU_STATE_VALUE_PENDING;
}

static mfc_emu_state_t handle_value_op_phase2(const uint8_t* data, int len)
{
    if (len < 4) return MFC_EMU_STATE_AUTHENTICATED;

    int32_t operand = (int32_t)(data[0] | (data[1] << 8) |
                                (data[2] << 16) | (data[3] << 24));
    int32_t current = read_value_from_block(s_emu.card.blocks[s_emu.pending_block]);

    switch (s_emu.pending_cmd) {
    case MFC_CMD_INCREMENT:
        s_emu.pending_value = current + operand;
        ESP_LOGI(TAG, "INC: %ld + %ld = %ld", (long)current, (long)operand,
                 (long)s_emu.pending_value);
        break;
    case MFC_CMD_DECREMENT:
        s_emu.pending_value = current - operand;
        ESP_LOGI(TAG, "DEC: %ld - %ld = %ld", (long)current, (long)operand,
                 (long)s_emu.pending_value);
        break;
    case MFC_CMD_RESTORE:
        s_emu.pending_value = current;
        break;
    default:
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    s_emu.stats.value_ops++;
    s_evt.value_op.cmd   = s_emu.pending_cmd;
    s_evt.value_op.block = s_emu.pending_block;
    s_evt.value_op.value = s_emu.pending_value;
    emit_event(MFC_EMU_EVT_VALUE_OP);

    return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_transfer(uint8_t block_num)
{
    if (block_num >= s_emu.card.total_blocks) {
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    int sector = block_to_sector(block_num);
    if (sector != s_emu.auth_sector) {
        target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
        s_emu.stats.nacks_sent++;
        return MFC_EMU_STATE_AUTHENTICATED;
    }

    const uint8_t* trailer = get_trailer_for_block(block_num);
    if (trailer) {
        int bidx = block_index_in_sector(block_num);
        if (!mfc_emu_can_write(trailer, bidx, s_emu.auth_key_type)) {
            target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
            s_emu.stats.nacks_sent++;
            return MFC_EMU_STATE_AUTHENTICATED;
        }
    }

    uint8_t addr = s_emu.card.blocks[s_emu.pending_block][12];
    write_value_to_block(s_emu.card.blocks[block_num], s_emu.pending_value, addr);

    ESP_LOGI(TAG, "TRANSFER: value %ld -> block %d",
             (long)s_emu.pending_value, block_num);

    hb_nfc_err_t err = target_tx_ack_encrypted(MFC_ACK);
    if (err != HB_NFC_OK) return MFC_EMU_STATE_ERROR;

    s_emu.last_activity_us = esp_timer_get_time();
    return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_halt(void)
{
    ESP_LOGI(TAG, "HALT received");
    reset_crypto_state();
    s_emu.stats.halts++;
    emit_event(MFC_EMU_EVT_HALT);
    return MFC_EMU_STATE_HALTED;
}

static hb_nfc_err_t load_pt_memory(void)
{
    uint8_t ptm[SPI_PT_MEM_A_LEN];
    memset(ptm, 0, sizeof(ptm));

    if (s_emu.card.uid_len == 4) {

        ptm[0]  = s_emu.card.uid[0];
        ptm[1]  = s_emu.card.uid[1];
        ptm[2]  = s_emu.card.uid[2];
        ptm[3]  = s_emu.card.uid[3];

        ptm[10] = s_emu.card.atqa[0];
        ptm[11] = s_emu.card.atqa[1];
        ptm[12] = s_emu.card.sak;

    }
    else if (s_emu.card.uid_len == 7) {

        ptm[0]  = s_emu.card.uid[0];
        ptm[1]  = s_emu.card.uid[1];
        ptm[2]  = s_emu.card.uid[2];
        ptm[3]  = s_emu.card.uid[3];
        ptm[4]  = s_emu.card.uid[4];
        ptm[5]  = s_emu.card.uid[5];
        ptm[6]  = s_emu.card.uid[6];

        ptm[10] = s_emu.card.atqa[0];
        ptm[11] = s_emu.card.atqa[1];
        ptm[12] = 0x04;
        ptm[13] = s_emu.card.sak;

    }

    ESP_LOGI(TAG, "PT Memory (write):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, ptm, SPI_PT_MEM_A_LEN, ESP_LOG_INFO);

    hb_nfc_err_t err = hb_spi_pt_mem_write(SPI_PT_MEM_A_WRITE, ptm, SPI_PT_MEM_A_LEN);
    if (err != HB_NFC_OK) {
        ESP_LOGE(TAG, "PT Memory write failed: %d", err);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(2));

    uint8_t rb[SPI_PT_MEM_A_LEN] = { 0 };
    hb_spi_pt_mem_read(rb, SPI_PT_MEM_A_LEN);
    ESP_LOGI(TAG, "PT Memory (readback):");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, rb, SPI_PT_MEM_A_LEN, ESP_LOG_INFO);

    if (memcmp(ptm, rb, SPI_PT_MEM_A_LEN) != 0) {
        ESP_LOGE(TAG, "PT memory mismatch: chip did not write correctly!");
        return HB_NFC_ERR_INTERNAL;
    }

    ESP_LOGI(TAG, "PT OK: UID=%02X%02X%02X%02X(%db) ATQA=%02X%02X SAK=%02X",
             s_emu.card.uid[0], s_emu.card.uid[1],
             s_emu.card.uid[2], s_emu.card.uid[3],
             s_emu.card.uid_len,
             ptm[10], ptm[11], s_emu.card.sak);
    return HB_NFC_OK;
}

hb_nfc_err_t mfc_emu_load_pt_memory(void) { return load_pt_memory(); }

hb_nfc_err_t mfc_emu_init(const mfc_emu_card_data_t* card)
{
    if (!card) return HB_NFC_ERR_PARAM;

    memcpy(&s_emu.card, card, sizeof(mfc_emu_card_data_t));
    memset(&s_emu.stats, 0, sizeof(mfc_emu_stats_t));
    s_emu.state         = MFC_EMU_STATE_IDLE;
    s_emu.crypto_active = false;
    s_emu.pending_value = 0;
    s_emu.cb            = NULL;
    s_emu.cb_ctx        = NULL;
    s_emu.prng_state    = get_cuid() ^ esp_random();
    s_emu.initialized   = true;

    ESP_LOGI(TAG, "Emulator init: UID=%02X%02X%02X%02X SAK=0x%02X sectors=%d",
             card->uid[0], card->uid[1], card->uid[2], card->uid[3],
             card->sak, card->sector_count);

    return HB_NFC_OK;
}

void mfc_emu_set_callback(mfc_emu_event_cb_t cb, void* ctx)
{
    s_emu.cb     = cb;
    s_emu.cb_ctx = ctx;
}

hb_nfc_err_t mfc_emu_configure_target(void)
{
    if (!s_emu.initialized) return HB_NFC_ERR_INTERNAL;

    ESP_LOGI(TAG, "=== Configuring ST25R3916 Target Mode ===");

    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));
    hb_spi_direct_cmd(CMD_SET_DEFAULT);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t ic_id = 0;
    hb_spi_reg_read(REG_IC_IDENTITY, &ic_id);
    ESP_LOGI(TAG, "IC Identity = 0x%02X", ic_id);
    if (ic_id == 0x00 || ic_id == 0xFF) {
        ESP_LOGE(TAG, "SPI not responding after reset!");
        return HB_NFC_ERR_INTERNAL;
    }

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_EN);
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t aux = 0;
    for (int i = 0; i < 200; i++) {
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
        if ((aux >> 4) & 1) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGI(TAG, "Oscillator: AUX=0x%02X osc_ok=%d", aux, (aux >> 4) & 1);

    hb_spi_direct_cmd(CMD_ADJUST_REGULATORS);
    vTaskDelay(pdMS_TO_TICKS(10));

    hb_spi_reg_write(REG_MODE, MODE_TARGET_NFCA);

    if (s_emu.card.uid_len == 7) {
        hb_spi_reg_write(REG_AUX_DEF, 0x10);
    } else {
        hb_spi_reg_write(REG_AUX_DEF, 0x00);
    }

    hb_spi_reg_write(REG_BIT_RATE,       0x00);
    hb_spi_reg_write(REG_ISO14443A,      0x00);
    hb_spi_reg_write(REG_PASSIVE_TARGET, 0x00);

    hb_spi_reg_write(REG_FIELD_THRESH_ACT,   0x33);
    hb_spi_reg_write(REG_FIELD_THRESH_DEACT, 0x22);

    hb_spi_reg_write(REG_PT_MOD, 0x60);

    hb_spi_reg_write(REG_MASK_RX_TIMER, 0x00);

    hb_nfc_err_t err = load_pt_memory();
    if (err != HB_NFC_OK) return err;

    st25r_irq_read();
    hb_spi_reg_write(REG_MASK_MAIN_INT,      0x00);
    hb_spi_reg_write(REG_MASK_TIMER_NFC_INT, 0x00);
    hb_spi_reg_write(REG_MASK_ERROR_WUP_INT, 0x00);
    hb_spi_reg_write(REG_MASK_TARGET_INT,    0x00);

    uint8_t mode_rb = 0, aux_rb = 0;
    hb_spi_reg_read(REG_MODE,    &mode_rb);
    hb_spi_reg_read(REG_AUX_DEF, &aux_rb);
    ESP_LOGI(TAG, "Verify: MODE=0x%02X AUX_DEF=0x%02X", mode_rb, aux_rb);

    if (mode_rb != MODE_TARGET_NFCA) {
        ESP_LOGE(TAG, "MODE readback wrong: 0x%02X (expected 0x%02X)",
                 mode_rb, MODE_TARGET_NFCA);
        return HB_NFC_ERR_INTERNAL;
    }

    ESP_LOGI(TAG, "=== Target configured ===");
    return HB_NFC_OK;
}

hb_nfc_err_t mfc_emu_start(void)
{
    if (!s_emu.initialized) return HB_NFC_ERR_INTERNAL;

    st25r_irq_read();
    hb_spi_reg_write(REG_OP_CTRL, 0xC3);
    vTaskDelay(pdMS_TO_TICKS(2));
    hb_spi_direct_cmd(0xC2);                         /* CMD_STOP */
    hb_spi_direct_cmd(CMD_GOTO_SENSE);
    vTaskDelay(pdMS_TO_TICKS(2));
    hb_spi_direct_cmd(0xDB);                         /* Clear FIFO */
    hb_spi_direct_cmd(0xD1);                         /* Unmask RX */

    uint8_t pt_sts = 0, op = 0, aux = 0;
    hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pt_sts);
    hb_spi_reg_read(REG_OP_CTRL, &op);
    hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
    ESP_LOGI(TAG, "Start: PT_STS=0x%02X AUX=0x%02X OP=0x%02X", pt_sts, aux, op);

    s_emu.state            = MFC_EMU_STATE_LISTEN;
    s_emu.last_activity_us = esp_timer_get_time();

    ESP_LOGI(TAG, "Emulator listening - bring the reader closer...");
    return HB_NFC_OK;
}

mfc_emu_state_t mfc_emu_run_step(void)
{
    if (!s_emu.initialized) return MFC_EMU_STATE_ERROR;

    switch (s_emu.state) {

    case MFC_EMU_STATE_LISTEN: {
        uint8_t tgt_irq   = 0;
        uint8_t main_irq  = 0;
        uint8_t timer_irq = 0;

        hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
        hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
        hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);

        uint8_t pts = 0;
        hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pts);
        uint8_t pta = pts & 0x0F;

        if (pta == 0x05 || pta == 0x0D || (tgt_irq & IRQ_TGT_SDD_C)) {
            ESP_LOGI(TAG, "=== SELECTED (pta=%d) ===", pta);
            s_emu.stats.cycles++;
            s_emu.crypto_active    = false;
            s_emu.last_activity_us = esp_timer_get_time();

            hb_spi_reg_modify(REG_ISO14443A,
                              ISO14443A_NO_TX_PAR | ISO14443A_NO_RX_PAR, 0);

            hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
            hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
            hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);

            emit_event(MFC_EMU_EVT_ACTIVATED);
            s_emu.state = MFC_EMU_STATE_ACTIVATED;
            return s_emu.state;
        }

        if (pta == 0x01 || pta == 0x02 || pta == 0x03 || (tgt_irq & IRQ_TGT_WU_A)) {
            ESP_LOGD(TAG, "Field detected (pta=%d)", pta);
            break;
        }

        if (!tgt_irq && !main_irq && !timer_irq) {
            static uint32_t s_idle = 0;
            if ((++s_idle % 600U) == 0U) {
                uint8_t aux_d = 0;
                hb_spi_reg_read(REG_AUX_DISPLAY, &aux_d);
                ESP_LOGI(TAG, "[LISTEN] AUX=0x%02X efd_o=%d osc=%d PT_STS=0x%02X",
                         aux_d, (aux_d >> 6) & 1, (aux_d >> 4) & 1, pts);
            }
        }
        break;
    }

    case MFC_EMU_STATE_ACTIVATED:
    case MFC_EMU_STATE_AUTHENTICATED: {
        uint8_t tgt_irq   = 0;
        uint8_t main_irq  = 0;
        uint8_t timer_irq = 0;

        hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
        hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
        hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);

        if (timer_irq & 0x08) {
            uint8_t aux = 0;
            hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
            if (((aux >> 6) & 1U) == 0U) {
                ESP_LOGW(TAG, "Field lost -> LISTEN");
                reset_crypto_state();
                s_emu.stats.field_losses++;
                emit_event(MFC_EMU_EVT_FIELD_LOST);
                hb_spi_direct_cmd(CMD_GOTO_SENSE);
                s_emu.state = MFC_EMU_STATE_LISTEN;
                return s_emu.state;
            }
        }

        if (!(main_irq & (IRQ_MAIN_RXE | IRQ_MAIN_FWL))) break;

        uint8_t enc[20] = { 0 };
        uint8_t cmd[20] = { 0 };
        int len = fifo_rx(enc, sizeof(enc));
        if (len <= 0) break;

        if (s_emu.state == MFC_EMU_STATE_AUTHENTICATED && s_emu.crypto_active) {
            for (int i = 0; i < len; i++) {
                uint8_t ks = crypto1_byte(&s_emu.crypto, 0, 0);
                cmd[i] = enc[i] ^ ks;
            }
        } else {
            memcpy(cmd, enc, len);
        }

        s_emu.last_activity_us = esp_timer_get_time();
        uint8_t cmd_byte = cmd[0];

        ESP_LOGI(TAG, "CMD: 0x%02X len=%d state=%s",
                 cmd_byte, len, mfc_emu_state_str(s_emu.state));

        if (cmd_byte == MFC_CMD_AUTH_KEY_A || cmd_byte == MFC_CMD_AUTH_KEY_B) {
            if (len >= 2) s_emu.state = handle_auth(cmd_byte, cmd[1]);
        }
        else if (s_emu.crypto_active) {
            switch (cmd_byte) {
            case MFC_CMD_READ:
                if (len >= 2) s_emu.state = handle_read(cmd[1]);
                break;
            case MFC_CMD_WRITE:
                if (len >= 2) s_emu.state = handle_write_phase1(cmd[1]);
                break;
            case MFC_CMD_INCREMENT:
            case MFC_CMD_DECREMENT:
            case MFC_CMD_RESTORE:
                if (len >= 2) s_emu.state = handle_value_op_phase1(cmd_byte, cmd[1]);
                break;
            case MFC_CMD_TRANSFER:
                if (len >= 2) s_emu.state = handle_transfer(cmd[1]);
                break;
            case MFC_CMD_HALT:
                s_emu.state = handle_halt();
                hb_spi_direct_cmd(CMD_GOTO_SENSE);
                break;
            default:
                ESP_LOGW(TAG, "Unknown CMD: 0x%02X len=%d", cmd_byte, len);
                s_emu.stats.unknown_cmds++;
                target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
                s_emu.stats.nacks_sent++;
                break;
            }
        }
        else if (cmd_byte == MFC_CMD_HALT) {
            s_emu.state = handle_halt();
            hb_spi_direct_cmd(CMD_GOTO_SENSE);
        }
        else {
            ESP_LOGW(TAG, "CMD 0x%02X without authentication", cmd_byte);
            s_emu.stats.unknown_cmds++;
        }
        break;
    }

    case MFC_EMU_STATE_WRITE_PENDING: {
        uint8_t tgt_irq = 0, main_irq = 0, timer_irq = 0;
        hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
        hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
        hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);

        if (timer_irq & 0x08) {
            uint8_t aux = 0;
            hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
            if (((aux >> 6) & 1U) == 0U) {
                reset_crypto_state();
                s_emu.stats.field_losses++;
                emit_event(MFC_EMU_EVT_FIELD_LOST);
                hb_spi_direct_cmd(CMD_GOTO_SENSE);
                s_emu.state = MFC_EMU_STATE_LISTEN;
                return s_emu.state;
            }
        }

        if (!(main_irq & (IRQ_MAIN_RXE | IRQ_MAIN_FWL))) break;

        uint8_t enc[20] = { 0 };
        int len = fifo_rx(enc, sizeof(enc));
        if (len <= 0) break;

        uint8_t plain[20] = { 0 };
        for (int i = 0; i < len; i++) {
            uint8_t ks = crypto1_byte(&s_emu.crypto, 0, 0);
            plain[i] = enc[i] ^ ks;
        }
        s_emu.state = handle_write_phase2(plain, len);
        break;
    }

    case MFC_EMU_STATE_VALUE_PENDING: {
        uint8_t tgt_irq = 0, main_irq = 0, timer_irq = 0;
        hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
        hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
        hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);

        if (timer_irq & 0x08) {
            uint8_t aux = 0;
            hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
            if (((aux >> 6) & 1U) == 0U) {
                reset_crypto_state();
                s_emu.stats.field_losses++;
                emit_event(MFC_EMU_EVT_FIELD_LOST);
                hb_spi_direct_cmd(CMD_GOTO_SENSE);
                s_emu.state = MFC_EMU_STATE_LISTEN;
                return s_emu.state;
            }
        }

        if (!(main_irq & (IRQ_MAIN_RXE | IRQ_MAIN_FWL))) break;

        uint8_t enc[8] = { 0 };
        int len = fifo_rx(enc, sizeof(enc));
        if (len <= 0) break;

        uint8_t plain[8] = { 0 };
        for (int i = 0; i < len; i++) {
            uint8_t ks = crypto1_byte(&s_emu.crypto, 0, 0);
            plain[i] = enc[i] ^ ks;
        }
        s_emu.state = handle_value_op_phase2(plain, len);
        break;
    }

    case MFC_EMU_STATE_HALTED:
    case MFC_EMU_STATE_ERROR: {
        reset_crypto_state();
        hb_spi_direct_cmd(CMD_GOTO_SENSE);
        s_emu.state = MFC_EMU_STATE_LISTEN;
        break;
    }

    default:
        break;
    }

    return s_emu.state;
}

hb_nfc_err_t mfc_emu_update_card(const mfc_emu_card_data_t* card)
{
    if (!card) return HB_NFC_ERR_PARAM;

    reset_crypto_state();
    memcpy(&s_emu.card, card, sizeof(mfc_emu_card_data_t));
    s_emu.prng_state = get_cuid() ^ esp_random();

    if (s_emu.state != MFC_EMU_STATE_IDLE)
        load_pt_memory();

    ESP_LOGI(TAG, "Card data updated: UID=%02X%02X%02X%02X",
             card->uid[0], card->uid[1], card->uid[2], card->uid[3]);

    return HB_NFC_OK;
}

void mfc_emu_stop(void)
{
    ESP_LOGI(TAG, "Emulator stopping...");
    reset_crypto_state();
    s_emu.state = MFC_EMU_STATE_IDLE;
    hb_spi_direct_cmd(CMD_STOP_ALL);
}

mfc_emu_stats_t mfc_emu_get_stats(void) { return s_emu.stats; }
mfc_emu_state_t mfc_emu_get_state(void) { return s_emu.state; }

const char* mfc_emu_state_str(mfc_emu_state_t state)
{
    switch (state) {
    case MFC_EMU_STATE_IDLE:          return "IDLE";
    case MFC_EMU_STATE_LISTEN:        return "LISTEN";
    case MFC_EMU_STATE_ACTIVATED:     return "ACTIVATED";
    case MFC_EMU_STATE_AUTH_SENT_NT:  return "AUTH_SENT_NT";
    case MFC_EMU_STATE_AUTHENTICATED: return "AUTHENTICATED";
    case MFC_EMU_STATE_WRITE_PENDING: return "WRITE_PENDING";
    case MFC_EMU_STATE_VALUE_PENDING: return "VALUE_PENDING";
    case MFC_EMU_STATE_HALTED:        return "HALTED";
    case MFC_EMU_STATE_ERROR:         return "ERROR";
    default:                          return "?";
    }
}

void mfc_emu_card_data_init(mfc_emu_card_data_t* cd,
                             const nfc_iso14443a_data_t* card,
                             mf_classic_type_t type)
{
    memset(cd, 0, sizeof(*cd));

    memcpy(cd->uid, card->uid, card->uid_len);
    cd->uid_len = card->uid_len;
    memcpy(cd->atqa, card->atqa, 2);
    cd->sak  = card->sak;
    if (cd->uid_len == 4 && cd->sak == 0x88) {
        ESP_LOGW(TAG, "SAK 0x88 with 4-byte UID; forcing 0x08 for emulation");
        cd->sak = 0x08;
    }
    cd->type = type;

    switch (type) {
    case MF_CLASSIC_MINI: cd->sector_count = 5;  cd->total_blocks = 20;  break;
    case MF_CLASSIC_1K:   cd->sector_count = 16; cd->total_blocks = 64;  break;
    case MF_CLASSIC_4K:   cd->sector_count = 40; cd->total_blocks = 256; break;
    default:              cd->sector_count = 16; cd->total_blocks = 64;  break;
    }
}
