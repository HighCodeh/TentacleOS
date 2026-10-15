// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
/**
 * @file mf_classic.c
 * @brief MIFARE Classic authentication, read, and write operations.
 * Reference: NXP MF1S50YYX_V1 — MIFARE Classic 1K/4K datasheet.
 */
#include "mf_classic.h"

#include <string.h>

#include "esp_log.h"

#include "crypto1.h"
#include "iso14443a.h"
#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"

static const char *TAG = "mf_classic";

/* ISO14443A register flags for raw parity control. */
#define ISOA_NO_TX_PAR (1U << 7)
#define ISOA_NO_RX_PAR (1U << 6)

/* MIFARE Classic command codes. Reference: MF1S50YYX_V1 §10. */
#define MF_CMD_READ  0x30
#define MF_CMD_WRITE 0xA0

/* Frame and key sizes. */
#define MF_BLOCK_SIZE      16
#define MF_KEY_LEN         6
#define MF_MAX_FRAME_SIZE  32
#define MF_ACK_NIBBLE_MASK 0x0F
#define MF_ACK_VALUE       0x0A

/* SAK values identifying card type. Reference: ISO/IEC 14443-3 Table 6. */
#define MF_SAK_MINI   0x09
#define MF_SAK_1K     0x08
#define MF_SAK_1K_INF 0x88
#define MF_SAK_4K     0x18

/* Sector and block counts per card type. Reference: MF1S50YYX_V1 §2. */
#define MF_SECTORS_MINI 5
#define MF_SECTORS_1K   16
#define MF_SECTORS_4K   40
#define MF_BLOCKS_MINI  20
#define MF_BLOCKS_1K    64
#define MF_BLOCKS_4K    256

/* 4K layout: first 32 sectors have 4 blocks; remaining 8 have 16 blocks. */
#define MF_SMALL_BLOCK_COUNT   4
#define MF_4K_BIG_SECTOR_START 32
#define MF_4K_BIG_BLOCK_COUNT  16
#define MF_4K_BIG_BLOCK_BASE   128

/* Trailer block layout: GPB is at byte 9. Reference: MF1S50YYX_V1 §8.6. */
#define MF_TRAILER_GPB_OFFSET 9

/* Protocol timeouts in milliseconds. */
#define MF_AUTH_TIMEOUT_MS  20
#define MF_READ_TIMEOUT_MS  30
#define MF_WRITE_TIMEOUT_MS 20

/* PRNG advance for reader response (64 - 32 free-running clocks). */
#define MF_PRNG_NT_ADVANCE 32

/* Bits per byte including parity (9th bit per transmitted byte). */
#define MF_BITS_WITH_PARITY 9

/* Number of bits in a standard byte. */
#define MF_BITS_PER_BYTE 8

/* Number of bytes in a 32-bit word (used for NR/AT fields). */
#define MF_WORD_BYTES 4

/* ACK nibble bit count (4-bit ACK response). */
#define MF_ACK_BITS 4

static crypto1_state_t s_crypto;
static bool s_auth = false;
static uint32_t s_last_nt = 0;
static mf_write_phase_t s_last_write_phase = MF_WRITE_PHASE_NONE;

/* XOR-based 32-bit LFSR PRNG seed (arbitrary non-zero value). */
static uint32_t s_nr_state = 0xA5A5A5A5U;

static void bit_set(uint8_t *buf, size_t bitpos, uint8_t v) {
  if (v)
    buf[bitpos >> 3] |= (uint8_t)(1U << (bitpos & 7U));
  else
    buf[bitpos >> 3] &= (uint8_t)~(1U << (bitpos & 7U));
}

static uint8_t bit_get(const uint8_t *buf, size_t bitpos) {
  return (uint8_t)((buf[bitpos >> 3] >> (bitpos & 7U)) & 1U);
}

static uint32_t bytes_to_num_be(const uint8_t b[4]) {
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static uint64_t key_to_u64_be(const mf_classic_key_t *key) {
  uint64_t k = 0;
  for (uint8_t i = 0; i < MF_KEY_LEN; i++) {
    k = (k << MF_BITS_PER_BYTE) | key->data[i];
  }
  return k;
}

static uint32_t mf_rand32(void) {
  uint32_t x = s_nr_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  if (x == 0)
    x = 1U;
  s_nr_state = x;
  return x;
}

void mf_classic_reset_auth(void) {
  s_auth = false;
}

static hb_nfc_err_t transceive_encrypted(crypto1_state_t *st,
                                         const uint8_t *tx,
                                         size_t tx_len,
                                         uint8_t *rx,
                                         size_t rx_len,
                                         int timeout_ms) {
  if (st == NULL || tx == NULL || tx_len == 0 || rx == NULL || rx_len == 0)
    return HB_NFC_ERR_PARAM;

  const size_t tx_bits = tx_len * MF_BITS_WITH_PARITY;
  const size_t rx_bits = rx_len * MF_BITS_WITH_PARITY;
  const size_t tx_bytes = (tx_bits + 7U) / 8U;
  const size_t rx_bytes = (rx_bits + 7U) / 8U;
  if (tx_bytes > MF_MAX_FRAME_SIZE || rx_bytes > MF_MAX_FRAME_SIZE)
    return HB_NFC_ERR_PARAM;

  uint8_t tx_buf[MF_MAX_FRAME_SIZE] = {0};
  uint8_t rx_buf[MF_MAX_FRAME_SIZE] = {0};

  size_t bitpos = 0;
  for (size_t i = 0; i < tx_len; i++) {
    uint8_t plain = tx[i];
    uint8_t ks = crypto1_byte(st, 0, 0);
    uint8_t enc = plain ^ ks;

    for (uint8_t bit = 0; bit < MF_BITS_PER_BYTE; bit++) {
      bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
    }

    uint8_t par_ks = crypto1_filter_output(st);
    uint8_t par = crypto1_odd_parity8(plain) ^ par_ks;
    bit_set(tx_buf, bitpos++, par);
  }

  uint8_t iso = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &iso);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, (uint8_t)(iso | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

  hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL);
  st25r3916_fifo_clear();
  if (st25r3916_irq_clear() != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    return HB_NFC_ERR_INTERNAL;
  }
  st25r3916_fifo_set_tx_bytes((uint16_t)(tx_bits / 8U), (uint8_t)(tx_bits % 8U));
  st25r3916_fifo_load(tx_buf, tx_bytes);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC);

  if (st25r3916_irq_wait_txe() != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    return HB_NFC_ERR_TX_TIMEOUT;
  }

  uint16_t count = 0;
  (void)st25r3916_fifo_wait(rx_bytes, timeout_ms, &count);
  if (count < rx_bytes) {
    if (count > 0) {
      size_t to_read = (count < rx_bytes) ? count : rx_bytes;
      st25r3916_fifo_read(rx_buf, to_read);
      nfc_log_hex(" MF RX partial:", rx_buf, to_read);
    }
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    return HB_NFC_ERR_TIMEOUT;
  }

  st25r3916_fifo_read(rx_buf, rx_bytes);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);

  bitpos = 0;
  for (size_t i = 0; i < rx_len; i++) {
    uint8_t enc_byte = 0;
    for (uint8_t bit = 0; bit < MF_BITS_PER_BYTE; bit++) {
      enc_byte |= (uint8_t)(bit_get(rx_buf, bitpos++) << bit);
    }

    uint8_t ks = crypto1_byte(st, 0, 0);
    uint8_t plain = enc_byte ^ ks;

    uint8_t enc_par = bit_get(rx_buf, bitpos++);
    uint8_t par_ks = crypto1_filter_output(st);
    uint8_t dec_par = enc_par ^ par_ks;
    if (dec_par != crypto1_odd_parity8(plain)) {
      ESP_LOGW(TAG,
               "Parity error byte %u: got %u exp %u",
               (unsigned)i,
               dec_par,
               crypto1_odd_parity8(plain));
      return HB_NFC_ERR_PROTOCOL;
    }

    rx[i] = plain;
  }

  return HB_NFC_OK;
}

static hb_nfc_err_t tx_encrypted_with_ack(crypto1_state_t *st,
                                          const uint8_t *tx,
                                          size_t tx_len,
                                          uint8_t *out_ack_nibble,
                                          int timeout_ms) {
  if (st == NULL || tx == NULL || tx_len == 0)
    return HB_NFC_ERR_PARAM;

  const size_t tx_bits = tx_len * MF_BITS_WITH_PARITY;
  const size_t tx_bytes = (tx_bits + 7U) / 8U;
  if (tx_bytes > MF_MAX_FRAME_SIZE)
    return HB_NFC_ERR_PARAM;

  uint8_t tx_buf[MF_MAX_FRAME_SIZE] = {0};

  size_t bitpos = 0;
  for (size_t i = 0; i < tx_len; i++) {
    uint8_t plain = tx[i];
    uint8_t ks = crypto1_byte(st, 0, 0);
    uint8_t enc = plain ^ ks;

    for (uint8_t bit = 0; bit < MF_BITS_PER_BYTE; bit++) {
      bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
    }

    uint8_t par_ks = crypto1_filter_output(st);
    uint8_t par = crypto1_odd_parity8(plain) ^ par_ks;
    bit_set(tx_buf, bitpos++, par);
  }

  uint8_t iso = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &iso);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, (uint8_t)(iso | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

  hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL);
  st25r3916_fifo_clear();
  if (st25r3916_irq_clear() != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    return HB_NFC_ERR_INTERNAL;
  }
  st25r3916_fifo_set_tx_bytes((uint16_t)(tx_bits / 8U), (uint8_t)(tx_bits % 8U));
  st25r3916_fifo_load(tx_buf, tx_bytes);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC);

  if (st25r3916_irq_wait_txe() != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    return HB_NFC_ERR_TX_TIMEOUT;
  }

  uint16_t count = 0;
  (void)st25r3916_fifo_wait(1, timeout_ms, &count);
  if (count < 1) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    return HB_NFC_ERR_TIMEOUT;
  }

  uint8_t enc_ack = 0;
  st25r3916_fifo_read(&enc_ack, 1);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);

  uint8_t plain_ack = 0;
  for (uint8_t i = 0; i < MF_ACK_BITS; i++) {
    uint8_t ks_bit = crypto1_bit(st, 0, 0);
    uint8_t enc_bit = (enc_ack >> i) & 1U;
    plain_ack |= (uint8_t)((enc_bit ^ ks_bit) << i);
  }

  if (out_ack_nibble != NULL)
    *out_ack_nibble = (uint8_t)(plain_ack & 0x0F);
  return HB_NFC_OK;
}

hb_nfc_err_t mf_classic_auth(uint8_t block,
                             mf_key_type_t key_type,
                             const mf_classic_key_t *key,
                             const uint8_t uid[4]) {
  if (key == NULL || uid == NULL)
    return HB_NFC_ERR_PARAM;
  s_auth = false;

  uint8_t cmd[2] = {(uint8_t)key_type, block};
  uint8_t nt_raw[MF_WORD_BYTES] = {0};
  int len = nfc_poller_transceive(
      cmd, 2, true, nt_raw, sizeof(nt_raw), MF_WORD_BYTES, MF_AUTH_TIMEOUT_MS);
  if (len < (int)MF_WORD_BYTES) {
    ESP_LOGW(TAG, "Auth: no nonce (len=%d)", len);
    return HB_NFC_ERR_AUTH;
  }
  nfc_log_hex(" Auth nt:", nt_raw, MF_WORD_BYTES);

  uint64_t k48 = key_to_u64_be(key);
  uint32_t cuid = bytes_to_num_be(uid);
  uint32_t nt_be = bytes_to_num_be(nt_raw);
  s_last_nt = nt_be;

  crypto1_init(&s_crypto, k48);
  crypto1_word(&s_crypto, nt_be ^ cuid, 0);

  uint8_t nr[MF_WORD_BYTES];
  uint32_t nr32 = mf_rand32();
  nr[0] = (uint8_t)((nr32 >> 24) & 0xFF);
  nr[1] = (uint8_t)((nr32 >> 16) & 0xFF);
  nr[2] = (uint8_t)((nr32 >> 8) & 0xFF);
  nr[3] = (uint8_t)(nr32 & 0xFF);

  uint8_t tx_buf[MF_MAX_FRAME_SIZE] = {0};
  size_t bitpos = 0;

  for (uint8_t i = 0; i < MF_WORD_BYTES; i++) {
    uint8_t ks = crypto1_byte(&s_crypto, nr[i], 0);
    uint8_t enc = ks ^ nr[i];

    for (uint8_t bit = 0; bit < MF_BITS_PER_BYTE; bit++) {
      bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
    }

    uint8_t par_ks = crypto1_filter_output(&s_crypto);
    uint8_t par = crypto1_odd_parity8(nr[i]) ^ par_ks;
    bit_set(tx_buf, bitpos++, par);
  }

  uint32_t nt_succ = crypto1_prng_successor(nt_be, MF_PRNG_NT_ADVANCE);
  for (uint8_t i = 0; i < MF_WORD_BYTES; i++) {
    nt_succ = crypto1_prng_successor(nt_succ, MF_BITS_PER_BYTE);
    uint8_t ar_byte = (uint8_t)(nt_succ & 0xFF);

    uint8_t ks = crypto1_byte(&s_crypto, 0, 0);
    uint8_t enc = ks ^ ar_byte;

    for (uint8_t bit = 0; bit < MF_BITS_PER_BYTE; bit++) {
      bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
    }

    uint8_t par_ks = crypto1_filter_output(&s_crypto);
    uint8_t par = crypto1_odd_parity8(ar_byte) ^ par_ks;
    bit_set(tx_buf, bitpos++, par);
  }

  /* NR (4 bytes) + AR (4 bytes), each with parity → 8 * 9 bits total. */
  const size_t tx_total_bits = (MF_WORD_BYTES + MF_WORD_BYTES) * MF_BITS_WITH_PARITY;
  const size_t tx_total_bytes = (tx_total_bits + 7U) / 8U;
  /* AT response: 4 bytes with parity. */
  const size_t rx_total_bits = MF_WORD_BYTES * MF_BITS_WITH_PARITY;
  const size_t rx_total_bytes = (rx_total_bits + 7U) / 8U;

  uint8_t rx_buf[MF_MAX_FRAME_SIZE] = {0};

  uint8_t iso = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &iso);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, (uint8_t)(iso | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

  hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL);
  st25r3916_fifo_clear();
  if (st25r3916_irq_clear() != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    return HB_NFC_ERR_INTERNAL;
  }
  st25r3916_fifo_set_tx_bytes((uint16_t)(tx_total_bits / 8U), (uint8_t)(tx_total_bits % 8U));
  st25r3916_fifo_load(tx_buf, tx_total_bytes);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC);

  if (st25r3916_irq_wait_txe() != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    ESP_LOGW(TAG, "Auth: TX timeout");
    return HB_NFC_ERR_TX_TIMEOUT;
  }

  uint16_t count = 0;
  (void)st25r3916_fifo_wait(rx_total_bytes, MF_AUTH_TIMEOUT_MS, &count);
  if (count < rx_total_bytes) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);
    ESP_LOGW(TAG, "Auth: RX timeout (got %u need %u)", count, (unsigned)rx_total_bytes);
    return HB_NFC_ERR_TIMEOUT;
  }

  st25r3916_fifo_read(rx_buf, rx_total_bytes);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso);

  uint8_t at_dec[MF_WORD_BYTES] = {0};
  bitpos = 0;
  for (uint8_t i = 0; i < MF_WORD_BYTES; i++) {
    uint8_t enc_byte = 0;
    for (uint8_t bit = 0; bit < MF_BITS_PER_BYTE; bit++) {
      enc_byte |= (uint8_t)(bit_get(rx_buf, bitpos++) << bit);
    }
    uint8_t ks = crypto1_byte(&s_crypto, 0, 0);
    at_dec[i] = enc_byte ^ ks;
    bitpos++;
  }

  nfc_log_hex(" Auth at_dec:", at_dec, MF_WORD_BYTES);

  s_auth = true;
  ESP_LOGI(TAG, "Auth SUCCESS on block %d", block);
  return HB_NFC_OK;
}

hb_nfc_err_t mf_classic_read_block(uint8_t block, uint8_t data[16]) {
  if (data == NULL)
    return HB_NFC_ERR_PARAM;
  if (!s_auth)
    return HB_NFC_ERR_AUTH;

  uint8_t cmd[4] = {MF_CMD_READ, block, 0, 0};
  iso14443a_crc(cmd, 2, &cmd[2]);

  uint8_t rx[18] = {0};
  hb_nfc_err_t err =
      transceive_encrypted(&s_crypto, cmd, sizeof(cmd), rx, sizeof(rx), MF_READ_TIMEOUT_MS);
  if (err != HB_NFC_OK) {
    ESP_LOGW(TAG, "Read block %d: transceive failed (%s)", block, hb_nfc_err_str(err));
    s_auth = false;
    return err;
  }

  if (!iso14443a_check_crc(rx, sizeof(rx))) {
    ESP_LOGW(TAG, "Read block %d: CRC error", block);
    nfc_log_hex("  rx:", rx, sizeof(rx));
    s_auth = false;
    return HB_NFC_ERR_CRC;
  }

  memcpy(data, rx, MF_BLOCK_SIZE);
  return HB_NFC_OK;
}

hb_nfc_err_t mf_classic_write_block(uint8_t block, const uint8_t data[16]) {
  if (data == NULL)
    return HB_NFC_ERR_PARAM;
  if (!s_auth)
    return HB_NFC_ERR_AUTH;

  uint8_t cmd[4] = {MF_CMD_WRITE, block, 0, 0};
  iso14443a_crc(cmd, 2, &cmd[2]);

  uint8_t ack = 0;
  s_last_write_phase = MF_WRITE_PHASE_CMD;
  hb_nfc_err_t err = tx_encrypted_with_ack(&s_crypto, cmd, sizeof(cmd), &ack, MF_WRITE_TIMEOUT_MS);
  if (err != HB_NFC_OK) {
    s_auth = false;
    return err;
  }
  if ((ack & MF_ACK_NIBBLE_MASK) != MF_ACK_VALUE) {
    ESP_LOGW(TAG, "Write cmd NACK (block %d): 0x%02X", block, ack);
    s_auth = false;
    return HB_NFC_ERR_NACK;
  }

  uint8_t frame[18];
  memcpy(frame, data, MF_BLOCK_SIZE);
  iso14443a_crc(frame, MF_BLOCK_SIZE, &frame[MF_BLOCK_SIZE]);

  s_last_write_phase = MF_WRITE_PHASE_DATA;
  err = tx_encrypted_with_ack(&s_crypto, frame, sizeof(frame), &ack, MF_WRITE_TIMEOUT_MS);
  if (err != HB_NFC_OK) {
    s_auth = false;
    return err;
  }
  if ((ack & MF_ACK_NIBBLE_MASK) != MF_ACK_VALUE) {
    ESP_LOGW(TAG, "Write data NACK (block %d): 0x%02X", block, ack);
    s_auth = false;
    return HB_NFC_ERR_NACK;
  }

  return HB_NFC_OK;
}

mf_classic_type_t mf_classic_get_type(uint8_t sak) {
  switch (sak) {
    case MF_SAK_MINI:
      return MF_CLASSIC_MINI;
    case MF_SAK_1K:
      return MF_CLASSIC_1K;
    case MF_SAK_1K_INF:
      return MF_CLASSIC_1K;
    case MF_SAK_4K:
      return MF_CLASSIC_4K;
    default:
      return MF_CLASSIC_1K;
  }
}

int mf_classic_get_sector_count(mf_classic_type_t type) {
  switch (type) {
    case MF_CLASSIC_MINI:
      return MF_SECTORS_MINI;
    case MF_CLASSIC_1K:
      return MF_SECTORS_1K;
    case MF_CLASSIC_4K:
      return MF_SECTORS_4K;
    default:
      return MF_SECTORS_1K;
  }
}

uint32_t mf_classic_get_last_nt(void) {
  return s_last_nt;
}

mf_write_phase_t mf_classic_get_last_write_phase(void) {
  return s_last_write_phase;
}

void mf_classic_get_crypto_state(crypto1_state_t *out) {
  if (out == NULL)
    return;
  memcpy(out, &s_crypto, sizeof(*out));
}
