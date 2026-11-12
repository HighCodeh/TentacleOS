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

#include "mf_classic_emu.h"
#include "rf_frontend.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

#define MFC_UID_SINGLE_BYTES    4
#define MFC_UID_DOUBLE_BYTES    7
#define MFC_COMMAND_BYTES       4U
#define MFC_RX_FRAME_ERROR_MASK 0x30U

static const char *TAG = "NFC_MF_CLASSIC_EMU";

#define IC_IDENTITY_NOT_RESPONDING_LO 0x00
#define IC_IDENTITY_NOT_RESPONDING_HI 0xFF

#define PTA_STATE_MASK       0x0F
#define PTA_STATE_IDLE       0x00
#define PTA_STATE_RF_DET     0x01
#define PTA_STATE_SENSE      0x02
#define PTA_STATE_RESOLUTION 0x03
#define PTA_STATE_SELECTED   0x05
#define PTA_STATE_ACTIVE     0x0D

#define NFC_SAK_CASCADE   0x88
#define MFC_AUX_DEF_NFC_N 0x10
#define MFC_PT_MOD_OOK    0x60

#define MFC_VALUE_FRAME_BYTES 6U
#define MFC_BLOCK_SIZE        16
#define MFC_KEY_SIZE          6
#define MFC_TRAILER_KEY_B_OFF 10
#define MFC_ACCESS_GROUPS     4
#define MFC_TRAILER_GROUP     3
#define MFC_ACCESS_OFFSET     6
#define MFC_ACCESS_BYTES      4
#define MFC_BIG_GROUP_BLOCKS  5
#define MFC_TX_MAX_BYTES      24
#define MFC_BITS_PER_BYTE     8
#define MFC_TX_WAIT_US        10000
#define MFC_FIFO_FAULT_MASK   0x30

#define MF_SMALL_BLOCK_COUNT   4
#define MF_4K_BIG_SECTOR_START 32
#define MF_4K_BIG_BLOCK_COUNT  16
#define MF_4K_BIG_BLOCK_BASE   128

#define MF_PRNG_AR_ADVANCE 64
#define MF_PRNG_AT_ADVANCE 96

#define MFC_TX_PARTIAL_BITS_MAX 7
#define FIFO_LEN_MASK           0x7F
#define ISO14443A_CT_BYTE       0x04

#define PT_IDX_ATQA_0 10
#define PT_IDX_ATQA_1 11
#define PT_IDX_SAK    12

#define MFC_OP_CTRL_TARGET     0xC3
#define MFC_FIELD_THRESH_ACT   0x13
#define MFC_FIELD_THRESH_DEACT 0x02
#define AUX_OSC_OK_BIT         4

#define MFC_EMU_POLL_DELAY_US   100
#define MFC_EMU_YIELD_INTERVAL  50
#define MFC_EMU_FIFO_SETTLE_US  2000
#define MFC_EMU_RX_POLL_US      500
#define MFC_EMU_DELAY_SHORT_MS  2
#define MFC_EMU_DELAY_MEDIUM_MS 5
#define MFC_EMU_DELAY_LONG_MS   10
#define MFC_EMU_OSC_TIMEOUT     200

static struct {
  mfc_emu_card_data_t card;
  mfc_emu_state_t state;
  mfc_emu_stats_t stats;

  crypto1_state_t crypto;
  bool crypto_active;
  uint32_t auth_nt;
  int auth_sector;
  mf_key_type_t auth_key_type;

  uint32_t prng_state;

  uint8_t pending_block;
  uint8_t pending_cmd;
  int32_t pending_value;
  bool has_pending_value;

  mfc_emu_event_cb_t cb;
  void *cb_ctx;

  int64_t last_activity_us;
  bool initialized;
} s_emu = {0};

static st25r3916_irq_status_t s_pending_irq;

static esp_err_t read_irq(st25r3916_irq_status_t *out_status) {
  esp_err_t err = st25r3916_irq_read(out_status);
  if (err != ESP_OK)
    return err;
  out_status->main |= s_pending_irq.main;
  out_status->timer |= s_pending_irq.timer;
  out_status->error |= s_pending_irq.error;
  out_status->target |= s_pending_irq.target;
  memset(&s_pending_irq, 0, sizeof(s_pending_irq));
  return ESP_OK;
}

static mfc_emu_state_t handle_auth(uint8_t auth_cmd, uint8_t block_num);
static mfc_emu_state_t handle_read(uint8_t block_num);
static mfc_emu_state_t handle_write_phase1(uint8_t block_num);
static mfc_emu_state_t handle_write_phase2(const uint8_t *data, int len);
static mfc_emu_state_t handle_value_op_phase1(uint8_t cmd, uint8_t block_num);
static mfc_emu_state_t handle_value_op_phase2(const uint8_t *data, int len);
static mfc_emu_state_t handle_transfer(uint8_t block_num);
static mfc_emu_state_t handle_halt(void);
static void emit_event(mfc_emu_event_type_t type);
static hb_nfc_err_t load_pt_memory(void);
static hb_nfc_err_t target_enter_listen(void);

static uint32_t emu_prng_next(void) {
  s_emu.prng_state = crypto1_prng_successor(s_emu.prng_state, 1);
  return s_emu.prng_state;
}

static uint32_t bytes_to_u32_be(const uint8_t *b) {
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static bool auth_try_decode(const uint8_t nr_ar_enc[8],
                            uint32_t ar_expected,
                            const crypto1_state_t *st_in,
                            crypto1_state_t *st_out) {
  crypto1_state_t candidate = *st_in;
  (void)crypto1_word(&candidate, bytes_to_u32_be(nr_ar_enc), 1);
  const uint32_t ar = bytes_to_u32_be(nr_ar_enc + 4) ^ crypto1_word(&candidate, 0, 0);
  if (ar != ar_expected)
    return false;
  *st_out = candidate;
  return true;
}

static void u32_to_bytes_be(uint32_t v, uint8_t *b) {
  b[0] = (uint8_t)(v >> 24);
  b[1] = (uint8_t)(v >> 16);
  b[2] = (uint8_t)(v >> 8);
  b[3] = (uint8_t)(v);
}

static uint32_t get_cuid(void) {
  const uint8_t *uid = s_emu.card.uid;
  if (s_emu.card.uid_len == 4)
    return bytes_to_u32_be(uid);
  return bytes_to_u32_be(&uid[3]);
}

static bool get_key_for_sector(int sector, mf_key_type_t key_type, uint64_t *key_out) {
  if (sector < 0 || sector >= s_emu.card.sector_count)
    return false;

  const uint8_t *kdata;
  bool known;

  if (key_type == MF_KEY_A) {
    kdata = s_emu.card.keys[sector].key_a;
    known = s_emu.card.keys[sector].key_a_known;
  } else {
    kdata = s_emu.card.keys[sector].key_b;
    known = s_emu.card.keys[sector].key_b_known;
  }

  if (!known)
    return false;

  *key_out = ((uint64_t)kdata[0] << 40) | ((uint64_t)kdata[1] << 32) | ((uint64_t)kdata[2] << 24) |
             ((uint64_t)kdata[3] << 16) | ((uint64_t)kdata[4] << 8) | (uint64_t)kdata[5];
  return true;
}

static int block_to_sector(int block) {
  if (block < MF_4K_BIG_BLOCK_BASE)
    return block / MF_SMALL_BLOCK_COUNT;
  return MF_4K_BIG_SECTOR_START + (block - MF_4K_BIG_BLOCK_BASE) / MF_4K_BIG_BLOCK_COUNT;
}

static int sector_first_block(int sector) {
  if (sector < MF_4K_BIG_SECTOR_START)
    return sector * MF_SMALL_BLOCK_COUNT;
  return MF_4K_BIG_BLOCK_BASE + (sector - MF_4K_BIG_SECTOR_START) * MF_4K_BIG_BLOCK_COUNT;
}

static int sector_block_count(int sector) {
  return (sector < MF_4K_BIG_SECTOR_START) ? MF_SMALL_BLOCK_COUNT : MF_4K_BIG_BLOCK_COUNT;
}

static int sector_trailer_block(int sector) {
  return sector_first_block(sector) + sector_block_count(sector) - 1;
}

static int block_index_in_sector(int block) {
  int sector = block_to_sector(block);
  int relative = block - sector_first_block(sector);
  return sector < MF_4K_BIG_SECTOR_START ? relative : relative / MFC_BIG_GROUP_BLOCKS;
}

bool mfc_emu_get_access_bits(
    const uint8_t trailer[16], int block_in_sector, uint8_t *c1, uint8_t *c2, uint8_t *c3) {
  if (trailer == NULL || c1 == NULL || c2 == NULL || c3 == NULL || block_in_sector < 0 ||
      block_in_sector >= MFC_ACCESS_GROUPS)
    return false;
  int grp = block_in_sector;

  uint8_t b6 = trailer[6], b7 = trailer[7], b8 = trailer[8];

  *c1 = (b7 >> (4 + grp)) & 1;
  *c2 = (b8 >> grp) & 1;
  *c3 = (b8 >> (4 + grp)) & 1;

  uint8_t c1_inv = (~b6 >> grp) & 1;
  uint8_t c2_inv = (~b6 >> (4 + grp)) & 1;
  uint8_t c3_inv = (~b7 >> grp) & 1;

  /* A broken redundant bit anywhere invalidates the sector, not only this group. */
  return (*c1 == c1_inv) && (*c2 == c2_inv) && (*c3 == c3_inv) &&
         (((b7 >> 4) ^ b6) & 0x0F) == 0x0F && (((b6 >> 4) ^ b8) & 0x0F) == 0x0F &&
         (((b8 >> 4) ^ b7) & 0x0F) == 0x0F;
}

static bool access_key_allowed(const uint8_t trailer[MFC_BLOCK_SIZE], mf_key_type_t key_type) {
  uint8_t c1, c2, c3;
  if ((key_type != MF_KEY_A && key_type != MF_KEY_B) ||
      !mfc_emu_get_access_bits(trailer, MFC_TRAILER_GROUP, &c1, &c2, &c3))
    return false;
  uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
  /* NXP table 8 note 1: readable Key B cannot authorize memory access. */
  return key_type == MF_KEY_A || (ac != 0 && ac != 1 && ac != 2);
}

static bool trailer_can_write_keys(uint8_t ac, mf_key_type_t key_type) {
  return ((ac == 0 || ac == 1) && key_type == MF_KEY_A) ||
         ((ac == 3 || ac == 4) && key_type == MF_KEY_B);
}

static bool trailer_can_write_access(uint8_t ac, mf_key_type_t key_type) {
  return (ac == 1 && key_type == MF_KEY_A) || ((ac == 3 || ac == 5) && key_type == MF_KEY_B);
}

bool mfc_emu_can_read(const uint8_t trailer[16], int block_in_sector, mf_key_type_t auth_key_type) {
  if (!access_key_allowed(trailer, auth_key_type))
    return false;
  uint8_t c1, c2, c3;
  if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
    return false;

  uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
  bool is_b = (auth_key_type == MF_KEY_B);
  int grp = block_in_sector;
  if (grp == MFC_TRAILER_GROUP)
    return true;

  switch (ac) {
    case 0:
    case 1:
    case 2:
    case 4:
    case 6:
      return true;
    case 3:
    case 5:
      return is_b;
    default:
      return false;
  }
}

bool mfc_emu_can_write(const uint8_t trailer[16],
                       int block_in_sector,
                       mf_key_type_t auth_key_type) {
  if (!access_key_allowed(trailer, auth_key_type))
    return false;
  uint8_t c1, c2, c3;
  if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
    return false;

  uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
  bool is_b = (auth_key_type == MF_KEY_B);

  if (block_in_sector == MFC_TRAILER_GROUP)
    return trailer_can_write_keys(ac, auth_key_type) || trailer_can_write_access(ac, auth_key_type);

  switch (ac) {
    case 0:
      return true;
    case 3:
    case 4:
    case 6:
      return is_b;
    default:
      return false;
  }
}

bool mfc_emu_can_increment(const uint8_t trailer[16],
                           int block_in_sector,
                           mf_key_type_t auth_key_type) {
  if (block_in_sector == MFC_TRAILER_GROUP || !access_key_allowed(trailer, auth_key_type))
    return false;
  uint8_t c1, c2, c3;
  if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
    return false;

  uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
  bool is_b = (auth_key_type == MF_KEY_B);

  switch (ac) {
    case 0:
      return true;
    case 6:
      return is_b;
    default:
      return false;
  }
}

bool mfc_emu_can_decrement(const uint8_t trailer[16],
                           int block_in_sector,
                           mf_key_type_t auth_key_type) {
  if (block_in_sector == MFC_TRAILER_GROUP || !access_key_allowed(trailer, auth_key_type))
    return false;
  uint8_t c1, c2, c3;
  if (!mfc_emu_get_access_bits(trailer, block_in_sector, &c1, &c2, &c3))
    return false;

  uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
  (void)auth_key_type;

  switch (ac) {
    case 0:
    case 1:
    case 6:
      return true;
    default:
      return false;
  }
}

static const uint8_t *get_trailer_for_block(uint8_t block_num) {
  int sector = block_to_sector(block_num);
  int tb = sector_trailer_block(sector);
  if (tb >= 0 && tb < s_emu.card.total_blocks)
    return s_emu.card.blocks[tb];
  return NULL;
}

static hb_nfc_err_t target_tx_raw(const uint8_t *data, size_t len_bytes, uint8_t extra_bits) {
  if (data == NULL || len_bytes == 0 || len_bytes > MFC_TX_MAX_BYTES ||
      extra_bits >= MFC_BITS_PER_BYTE)
    return HB_NFC_ERR_PARAM;
  /* Passive-listen replies use load modulation from the external reader's
   * field.  TX_EN drives
   * our own RF field and must stay off in target mode;
   * ST's RFAL also transmits passive-listen
   * frames with TX_EN cleared. */
  uint16_t full_bytes = (uint16_t)(len_bytes - (extra_bits != 0));
  st25r3916_irq_status_t stale;
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_CLEAR_FIFO) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES1, (uint8_t)(full_bytes >> 5)) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES2,
                           (uint8_t)((full_bytes << 3) | extra_bits)) != ESP_OK ||
      st25r3916_fifo_load(data, len_bytes) != ESP_OK || st25r3916_irq_read(&stale) != ESP_OK ||
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  /* RXE can share the TXE snapshot when the reader sends its next command quickly. */
  for (uint32_t elapsed = 0; elapsed < MFC_TX_WAIT_US; elapsed += MFC_EMU_POLL_DELAY_US) {
    st25r3916_irq_status_t irq;
    if (st25r3916_irq_read(&irq) != ESP_OK)
      return HB_NFC_ERR_INTERNAL;
    s_pending_irq.main |= irq.main & (uint8_t)~ST25R3916_IRQ_MAIN_TXE;
    s_pending_irq.timer |= irq.timer;
    s_pending_irq.error |= irq.error;
    s_pending_irq.target |= irq.target;
    if (irq.main & ST25R3916_IRQ_MAIN_TXE)
      return HB_NFC_OK;
    hb_nfc_timer_delay_us(MFC_EMU_POLL_DELAY_US);
  }
  return HB_NFC_ERR_TX_TIMEOUT;
}

static hb_nfc_err_t target_enter_listen(void) {
  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, MFC_OP_CTRL_TARGET) != ESP_OK ||
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_TARGET_NFCA) != ESP_OK ||
      hb_nfc_spi_reg_modify(
          ST25R3916_REG_PASSIVE_TARGET, ST25R3916_PASSIVE_TARGET_DISABLE_106_AUTO, 0) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, 0) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_MASK_MAIN_INT, 0) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TIMER_NFC_INT, 0) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_MASK_ERROR_WUP_INT, 0) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TARGET_INT, 0) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  st25r3916_irq_status_t status;
  if (st25r3916_irq_read(&status) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  /* Arm last: clearing FIFO after GOTO_SENSE can discard a new AUTH. */
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  return HB_NFC_OK;
}

static hb_nfc_err_t target_enter_sleep(void) {
  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, MFC_OP_CTRL_TARGET) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_TARGET_NFCA) != ESP_OK ||
      hb_nfc_spi_reg_modify(
          ST25R3916_REG_PASSIVE_TARGET, ST25R3916_PASSIVE_TARGET_DISABLE_106_AUTO, 0) != ESP_OK ||
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL) != ESP_OK ||
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  return HB_NFC_OK;
}

static int fifo_rx(uint8_t *buf, size_t buf_max);

static int target_rx_irq(uint8_t *buf, size_t buf_max, int timeout_ms) {
  const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
  while (esp_timer_get_time() < deadline) {
    if (hb_nfc_gpio_irq_level() || s_pending_irq.main || s_pending_irq.timer) {
      st25r3916_irq_status_t irq = {0};
      if (read_irq(&irq) != ESP_OK)
        return -1;
      // In target mode the FIFO strips parity. Ciphertext can legitimately flag
      // hardware parity/CRC errors; only a completed frame is consumed.
      if (irq.collision || (irq.error & MFC_RX_FRAME_ERROR_MASK))
        return -1;
      if (irq.main & ST25R3916_IRQ_MAIN_RXE)
        return fifo_rx(buf, buf_max);
    }
    hb_nfc_timer_delay_us(MFC_EMU_POLL_DELAY_US);
  }
  return 0;
}

static int fifo_rx(uint8_t *buf, size_t buf_max) {
  uint8_t fs1 = 0, fs2 = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS1, &fs1) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS2, &fs2) != ESP_OK ||
      (fs2 & MFC_FIFO_FAULT_MASK))
    return -1;
  int n = fs1 | ((fs2 & ST25R3916_FIFO_STATUS2_MSB_MASK) << 2);
  if (n <= 0)
    return 0;
  if (n > (int)buf_max)
    return -1;
  if (st25r3916_fifo_read(buf, (size_t)n) != ESP_OK)
    return -1;
  return n;
}

static void crypto1_encrypt_with_parity(const uint8_t *plain,
                                        size_t len,
                                        uint8_t *packed,
                                        size_t *packed_bits) {
  memset(packed, 0, (len * 9 + 7) / 8 + 1);
  size_t bit_pos = 0;

  for (size_t i = 0; i < len; i++) {
    uint8_t ks = crypto1_byte(&s_emu.crypto, 0, 0);
    uint8_t enc_byte = plain[i] ^ ks;
    uint8_t par_ks = crypto1_filter_output(&s_emu.crypto);
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

static hb_nfc_err_t target_tx_encrypted(const uint8_t *plain, size_t len) {
  uint8_t packed[24] = {0};
  size_t total_bits = 0;

  if (plain == NULL || len == 0 || len > 18)
    return HB_NFC_ERR_PARAM;
  crypto1_encrypt_with_parity(plain, len, packed, &total_bits);

  esp_err_t err = hb_nfc_spi_target_tx_bits(packed, total_bits);
  return err == ESP_OK ? HB_NFC_OK : HB_NFC_ERR_TX_TIMEOUT;
}

static hb_nfc_err_t target_tx_ack_encrypted(uint8_t ack_nack) {
  uint8_t enc = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t ks_bit = crypto1_bit(&s_emu.crypto, 0, 0);
    uint8_t plain_bit = (ack_nack >> i) & 1;
    enc |= (uint8_t)((plain_bit ^ ks_bit) << i);
  }

  return hb_nfc_spi_target_tx_bits(&enc, 4) == ESP_OK ? HB_NFC_OK : HB_NFC_ERR_INTERNAL;
}

static hb_nfc_err_t target_tx_nonce(uint32_t nt, bool nested) {
  uint8_t nt_bytes[4];
  u32_to_bytes_be(nt, nt_bytes);
  if (!nested) {
    (void)crypto1_word(&s_emu.crypto, nt ^ get_cuid(), 0);
    return target_tx_raw(nt_bytes, sizeof(nt_bytes), 0);
  }
  // Nested nonce encryption clocks NT xor CUID into the newly initialized key.
  uint8_t input[4];
  uint8_t packed[5] = {0};
  u32_to_bytes_be(nt ^ get_cuid(), input);
  size_t position = 0;
  for (size_t i = 0; i < sizeof(nt_bytes); i++) {
    const uint8_t enc = nt_bytes[i] ^ crypto1_byte(&s_emu.crypto, input[i], 0);
    const uint8_t parity = crypto1_odd_parity8(nt_bytes[i]) ^ crypto1_filter_output(&s_emu.crypto);
    for (size_t bit = 0; bit < 9U; bit++, position++) {
      const uint8_t value = bit == 8U ? parity : (enc >> bit) & 1U;
      packed[position / 8U] |= value << (position % 8U);
    }
  }
  return hb_nfc_spi_target_tx_bits(packed, position) == ESP_OK ? HB_NFC_OK : HB_NFC_ERR_INTERNAL;
}

static mfc_emu_event_t s_evt;

static bool command_frame_valid(const uint8_t *data, size_t len) {
  return data != NULL && len == MFC_COMMAND_BYTES && iso14443a_check_crc(data, len);
}

static void emit_event(mfc_emu_event_type_t type) {
  if (!s_emu.cb)
    return;
  s_evt.type = type;
  s_emu.cb(&s_evt, s_emu.cb_ctx);
}

static void reset_crypto_state(void) {
  memset(&s_pending_irq, 0, sizeof(s_pending_irq));
  s_emu.crypto_active = false;
  s_emu.auth_nt = 0;
  s_emu.auth_sector = 0;
  s_emu.pending_block = 0;
  s_emu.pending_cmd = 0;
  s_emu.pending_value = 0;
  s_emu.has_pending_value = false;
  crypto1_reset(&s_emu.crypto);
  hb_nfc_spi_reg_modify(
      ST25R3916_REG_ISO14443A, ST25R3916_ISO14443A_NO_TX_PAR | ST25R3916_ISO14443A_NO_RX_PAR, 0);
}

static mfc_emu_state_t handle_auth(uint8_t auth_cmd, uint8_t block_num) {
  s_emu.stats.total_auths++;
  const bool nested = s_emu.crypto_active;
  const mf_key_type_t key_type = auth_cmd == MFC_CMD_AUTH_KEY_A ? MF_KEY_A : MF_KEY_B;
  const int sector = block_to_sector(block_num);
  uint64_t key = 0;
  if (block_num >= s_emu.card.total_blocks || !get_key_for_sector(sector, key_type, &key)) {
    reset_crypto_state();
    s_emu.stats.failed_auths++;
    s_evt.auth.sector = sector;
    s_evt.auth.key_type = key_type;
    emit_event(MFC_EMU_EVT_AUTH_FAIL);
    return MFC_EMU_STATE_HALTED;
  }

  const uint32_t nt = emu_prng_next();
  s_emu.auth_nt = nt;
  s_emu.auth_sector = sector;
  s_emu.auth_key_type = key_type;
  crypto1_init(&s_emu.crypto, key);
  hb_nfc_err_t err = target_tx_nonce(nt, nested);
  if (err != HB_NFC_OK) {
    reset_crypto_state();
    s_emu.stats.failed_auths++;
    return MFC_EMU_STATE_ERROR;
  }
  s_emu.crypto_active = true;

  uint8_t nr_ar[8];
  const int length = target_rx_irq(nr_ar, sizeof(nr_ar), 50);
  crypto1_state_t verified;
  if (length != (int)sizeof(nr_ar) ||
      !auth_try_decode(
          nr_ar, crypto1_prng_successor(nt, MF_PRNG_AR_ADVANCE), &s_emu.crypto, &verified)) {
    ESP_LOGD(TAG, "AUTH rejected sector=%d bytes=%d nested=%d", sector, length, nested);
    reset_crypto_state();
    s_emu.stats.failed_auths++;
    s_evt.auth.sector = sector;
    s_evt.auth.key_type = key_type;
    emit_event(MFC_EMU_EVT_AUTH_FAIL);
    return MFC_EMU_STATE_HALTED;
  }
  s_emu.crypto = verified;
  uint8_t at[4];
  u32_to_bytes_be(crypto1_prng_successor(nt, MF_PRNG_AT_ADVANCE), at);
  if (target_tx_encrypted(at, sizeof(at)) != HB_NFC_OK) {
    reset_crypto_state();
    s_emu.stats.failed_auths++;
    return MFC_EMU_STATE_ERROR;
  }
  s_emu.stats.successful_auths++;
  s_emu.last_activity_us = esp_timer_get_time();
  ESP_LOGD(TAG, "AUTH OK sector=%d nested=%d", sector, nested);
  s_evt.auth.sector = sector;
  s_evt.auth.key_type = key_type;
  emit_event(MFC_EMU_EVT_AUTH_SUCCESS);
  return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_read(uint8_t block_num) {
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

  const uint8_t *trailer = get_trailer_for_block(block_num);
  if (trailer) {
    int bidx = block_index_in_sector(block_num);
    if (!mfc_emu_can_read(trailer, bidx, s_emu.auth_key_type)) {
      ESP_LOGW(TAG, "READ block %d denied by AC", block_num);
      target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
      s_emu.stats.nacks_sent++;
      return MFC_EMU_STATE_AUTHENTICATED;
    }
  }

  uint8_t resp[18];
  memcpy(resp, s_emu.card.blocks[block_num], MFC_BLOCK_SIZE);

  int tb = sector_trailer_block(sector);
  if (block_num == tb) {
    memset(resp, 0x00, MFC_KEY_SIZE);
    uint8_t c1, c2, c3;
    mfc_emu_get_access_bits(s_emu.card.blocks[tb], 3, &c1, &c2, &c3);
    uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
    if (ac > 2)
      memset(&resp[MFC_TRAILER_KEY_B_OFF], 0x00, MFC_KEY_SIZE);
  }

  iso14443a_crc(resp, MFC_BLOCK_SIZE, &resp[MFC_BLOCK_SIZE]);

  hb_nfc_err_t err = target_tx_encrypted(resp, 18);
  if (err != HB_NFC_OK) {
    ESP_LOGW(TAG, "READ TX failed");
    return MFC_EMU_STATE_ERROR;
  }

  ESP_LOGD(TAG, "READ block %d (sector %d)", block_num, sector);

  s_emu.stats.reads_served++;
  s_emu.last_activity_us = esp_timer_get_time();

  s_evt.read.block = block_num;
  emit_event(MFC_EMU_EVT_READ);

  return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_write_phase1(uint8_t block_num) {
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

  const uint8_t *trailer = get_trailer_for_block(block_num);
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

  ESP_LOGD(TAG, "WRITE phase 1: block %d - ACK", block_num);

  hb_nfc_err_t err = target_tx_ack_encrypted(MFC_ACK);
  if (err != HB_NFC_OK)
    return MFC_EMU_STATE_ERROR;

  s_emu.pending_block = block_num;
  s_emu.pending_cmd = MFC_CMD_WRITE;

  return MFC_EMU_STATE_WRITE_PENDING;
}

static mfc_emu_state_t handle_write_phase2(const uint8_t *data, int len) {
  if (len != 18) {
    target_tx_ack_encrypted(MFC_NACK_PARITY_CRC);
    s_emu.stats.nacks_sent++;
    return MFC_EMU_STATE_AUTHENTICATED;
  }

  uint8_t crc[2];
  iso14443a_crc(data, MFC_BLOCK_SIZE, crc);
  if (data[MFC_BLOCK_SIZE] != crc[0] || data[MFC_BLOCK_SIZE + 1] != crc[1]) {
    ESP_LOGW(TAG, "WRITE phase 2: CRC mismatch");
    target_tx_ack_encrypted(MFC_NACK_PARITY_CRC);
    s_emu.stats.nacks_sent++;
    return MFC_EMU_STATE_AUTHENTICATED;
  }

  uint8_t block_num = s_emu.pending_block;
  ESP_LOGD(TAG, "WRITE phase 2: block %d", block_num);

  int sector = block_to_sector(block_num);
  if (block_num == sector_trailer_block(sector)) {
    uint8_t *trailer = s_emu.card.blocks[block_num];
    uint8_t c1, c2, c3;
    if (!mfc_emu_get_access_bits(trailer, MFC_TRAILER_GROUP, &c1, &c2, &c3) ||
        !access_key_allowed(trailer, s_emu.auth_key_type))
      return MFC_EMU_STATE_ERROR;
    uint8_t ac = (c1 << 2) | (c2 << 1) | c3;
    /* Permissions come from the OLD trailer; preserve fields the key cannot change. */
    if (trailer_can_write_keys(ac, s_emu.auth_key_type)) {
      memcpy(trailer, data, MFC_KEY_SIZE);
      memcpy(&trailer[MFC_TRAILER_KEY_B_OFF], &data[MFC_TRAILER_KEY_B_OFF], MFC_KEY_SIZE);
      memcpy(s_emu.card.keys[sector].key_a, data, MFC_KEY_SIZE);
      memcpy(s_emu.card.keys[sector].key_b, &data[MFC_TRAILER_KEY_B_OFF], MFC_KEY_SIZE);
      s_emu.card.keys[sector].key_a_known = true;
      s_emu.card.keys[sector].key_b_known = true;
    }
    if (trailer_can_write_access(ac, s_emu.auth_key_type))
      memcpy(&trailer[MFC_ACCESS_OFFSET], &data[MFC_ACCESS_OFFSET], MFC_ACCESS_BYTES);
  } else {
    memcpy(s_emu.card.blocks[block_num], data, MFC_BLOCK_SIZE);
  }

  hb_nfc_err_t err = target_tx_ack_encrypted(MFC_ACK);
  if (err != HB_NFC_OK)
    return MFC_EMU_STATE_ERROR;

  s_emu.stats.writes_served++;
  s_emu.last_activity_us = esp_timer_get_time();

  s_evt.write.block = block_num;
  emit_event(MFC_EMU_EVT_WRITE);

  return MFC_EMU_STATE_AUTHENTICATED;
}

static bool is_value_block_format(const uint8_t *data) {
  uint32_t v1 =
      data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
  uint32_t v2 =
      data[4] | ((uint32_t)data[5] << 8U) | ((uint32_t)data[6] << 16U) | ((uint32_t)data[7] << 24U);
  uint32_t v3 = data[8] | ((uint32_t)data[9] << 8U) | ((uint32_t)data[10] << 16U) |
                ((uint32_t)data[11] << 24U);

  if (v1 != v3)
    return false;
  if ((v1 ^ v2) != 0xFFFFFFFF)
    return false;
  if (data[12] != data[14])
    return false;
  if ((data[12] ^ data[13]) != 0xFF)
    return false;
  if ((data[14] ^ data[15]) != 0xFF)
    return false;
  return true;
}

static int32_t read_value_from_block(const uint8_t *data) {
  return (int32_t)(data[0] | ((uint32_t)data[1] << 8U) | ((uint32_t)data[2] << 16U) |
                   ((uint32_t)data[3] << 24U));
}

static void write_value_to_block(uint8_t *data, int32_t value, uint8_t addr) {
  uint32_t v = (uint32_t)value;
  uint32_t nv = ~v;

  data[0] = v & 0xFF;
  data[1] = (v >> 8) & 0xFF;
  data[2] = (v >> 16) & 0xFF;
  data[3] = (v >> 24) & 0xFF;
  data[4] = nv & 0xFF;
  data[5] = (nv >> 8) & 0xFF;
  data[6] = (nv >> 16) & 0xFF;
  data[7] = (nv >> 24) & 0xFF;
  data[8] = v & 0xFF;
  data[9] = (v >> 8) & 0xFF;
  data[10] = (v >> 16) & 0xFF;
  data[11] = (v >> 24) & 0xFF;
  data[12] = addr;
  data[13] = ~addr;
  data[14] = addr;
  data[15] = ~addr;
}

static mfc_emu_state_t handle_value_op_phase1(uint8_t cmd, uint8_t block_num) {
  s_emu.has_pending_value = false;
  if (block_num >= s_emu.card.total_blocks) {
    target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
    s_emu.stats.nacks_sent++;
    return MFC_EMU_STATE_AUTHENTICATED;
  }

  int sector = block_to_sector(block_num);
  if (sector != s_emu.auth_sector || block_num == (uint8_t)sector_trailer_block(sector)) {
    target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
    s_emu.stats.nacks_sent++;
    return MFC_EMU_STATE_AUTHENTICATED;
  }

  const uint8_t *trailer = get_trailer_for_block(block_num);
  if (trailer) {
    int bidx = block_index_in_sector(block_num);
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
  if (err != HB_NFC_OK)
    return MFC_EMU_STATE_ERROR;

  s_emu.pending_block = block_num;
  s_emu.pending_cmd = cmd;

  return MFC_EMU_STATE_VALUE_PENDING;
}

static mfc_emu_state_t handle_value_op_phase2(const uint8_t *data, int len) {
  s_emu.has_pending_value = false;
  if (data == NULL || len != MFC_VALUE_FRAME_BYTES || !iso14443a_check_crc(data, len)) {
    s_emu.stats.nacks_sent++;
    return target_tx_ack_encrypted(MFC_NACK_PARITY_CRC) == HB_NFC_OK ? MFC_EMU_STATE_AUTHENTICATED
                                                                     : MFC_EMU_STATE_ERROR;
  }

  const uint32_t operand = (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
                           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
  const uint32_t current = (uint32_t)read_value_from_block(s_emu.card.blocks[s_emu.pending_block]);

  switch (s_emu.pending_cmd) {
    case MFC_CMD_INCREMENT:
      s_emu.pending_value = (int32_t)(current + operand);
      break;
    case MFC_CMD_DECREMENT:
      s_emu.pending_value = (int32_t)(current - operand);
      break;
    case MFC_CMD_RESTORE:
      s_emu.pending_value = current;
      break;
    default:
      return MFC_EMU_STATE_AUTHENTICATED;
  }

  // Value phase 2 is silent; receive must resume even without a reply transmission.
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_UNMASK_RX_DATA) != ESP_OK)
    return MFC_EMU_STATE_ERROR;
  s_emu.has_pending_value = true;
  s_emu.stats.value_ops++;
  s_evt.value_op.cmd = s_emu.pending_cmd;
  s_evt.value_op.block = s_emu.pending_block;
  s_evt.value_op.value = s_emu.pending_value;
  emit_event(MFC_EMU_EVT_VALUE_OP);

  return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_transfer(uint8_t block_num) {
  if (!s_emu.has_pending_value || block_num == 0 || block_num >= s_emu.card.total_blocks) {
    target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
    s_emu.stats.nacks_sent++;
    return MFC_EMU_STATE_AUTHENTICATED;
  }

  int sector = block_to_sector(block_num);
  if (sector != s_emu.auth_sector || block_num == (uint8_t)sector_trailer_block(sector)) {
    target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
    s_emu.stats.nacks_sent++;
    return MFC_EMU_STATE_AUTHENTICATED;
  }

  const uint8_t *trailer = get_trailer_for_block(block_num);
  if (trailer) {
    int bidx = block_index_in_sector(block_num);
    if (!mfc_emu_can_decrement(trailer, bidx, s_emu.auth_key_type)) {
      target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
      s_emu.stats.nacks_sent++;
      return MFC_EMU_STATE_AUTHENTICATED;
    }
  }

  uint8_t addr = s_emu.card.blocks[s_emu.pending_block][12];
  write_value_to_block(s_emu.card.blocks[block_num], s_emu.pending_value, addr);

  ESP_LOGD(TAG, "TRANSFER: value %ld -> block %d", (long)s_emu.pending_value, block_num);

  hb_nfc_err_t err = target_tx_ack_encrypted(MFC_ACK);
  if (err != HB_NFC_OK)
    return MFC_EMU_STATE_ERROR;

  s_emu.last_activity_us = esp_timer_get_time();
  return MFC_EMU_STATE_AUTHENTICATED;
}

static mfc_emu_state_t handle_halt(void) {
  ESP_LOGD(TAG, "HALT received");
  reset_crypto_state();
  s_emu.stats.halts++;
  emit_event(MFC_EMU_EVT_HALT);
  return MFC_EMU_STATE_HALTED;
}

static hb_nfc_err_t load_pt_memory(void) {
  uint8_t ptm[ST25R3916_SPI_PT_MEM_A_LEN];
  memset(ptm, 0, sizeof(ptm));

  if (s_emu.card.uid_len == 4) {
    ptm[0] = s_emu.card.uid[0];
    ptm[1] = s_emu.card.uid[1];
    ptm[2] = s_emu.card.uid[2];
    ptm[3] = s_emu.card.uid[3];

    ptm[PT_IDX_ATQA_0] = s_emu.card.atqa[0];
    ptm[PT_IDX_ATQA_1] = s_emu.card.atqa[1];
    ptm[PT_IDX_SAK] = s_emu.card.sak;

  } else if (s_emu.card.uid_len == 7) {
    ptm[0] = s_emu.card.uid[0];
    ptm[1] = s_emu.card.uid[1];
    ptm[2] = s_emu.card.uid[2];
    ptm[3] = s_emu.card.uid[3];
    ptm[4] = s_emu.card.uid[4];
    ptm[5] = s_emu.card.uid[5];
    ptm[6] = s_emu.card.uid[6];

    ptm[PT_IDX_ATQA_0] = s_emu.card.atqa[0];
    ptm[PT_IDX_ATQA_1] = s_emu.card.atqa[1];
    ptm[PT_IDX_SAK] = ISO14443A_CT_BYTE;
    ptm[PT_IDX_SAK + 1] = s_emu.card.sak;
  }

  ESP_LOGD(TAG, "PT Memory (write):");
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, ptm, ST25R3916_SPI_PT_MEM_A_LEN, ESP_LOG_DEBUG);

  hb_nfc_err_t err =
      hb_nfc_spi_pt_mem_write(ST25R3916_SPI_PT_MEM_A_WRITE, ptm, ST25R3916_SPI_PT_MEM_A_LEN);
  if (err != HB_NFC_OK) {
    ESP_LOGE(TAG, "PT Memory write failed: %d", err);
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(MFC_EMU_DELAY_SHORT_MS));

  uint8_t rb[ST25R3916_SPI_PT_MEM_A_LEN] = {0};
  if (hb_nfc_spi_pt_mem_read(rb, ST25R3916_SPI_PT_MEM_A_LEN) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  ESP_LOGD(TAG, "PT Memory (readback):");
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, rb, ST25R3916_SPI_PT_MEM_A_LEN, ESP_LOG_DEBUG);

  if (memcmp(ptm, rb, ST25R3916_SPI_PT_MEM_A_LEN) != 0) {
    ESP_LOGE(TAG, "PT memory mismatch: chip did not write correctly!");
    return HB_NFC_ERR_INTERNAL;
  }

  ESP_LOGI(TAG,
           "PT OK: UID=%02X%02X%02X%02X(%db) ATQA=%02X%02X SAK=%02X",
           s_emu.card.uid[0],
           s_emu.card.uid[1],
           s_emu.card.uid[2],
           s_emu.card.uid[3],
           s_emu.card.uid_len,
           ptm[10],
           ptm[11],
           s_emu.card.sak);
  return HB_NFC_OK;
}

hb_nfc_err_t mfc_emu_load_pt_memory(void) {
  return load_pt_memory();
}

hb_nfc_err_t mfc_emu_init(const mfc_emu_card_data_t *card) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return HB_NFC_ERR_INTERNAL;
  if (card == NULL ||
      (card->uid_len != MFC_UID_SINGLE_BYTES && card->uid_len != MFC_UID_DOUBLE_BYTES))
    return HB_NFC_ERR_PARAM;

  memcpy(&s_emu.card, card, sizeof(mfc_emu_card_data_t));
  memset(&s_emu.stats, 0, sizeof(mfc_emu_stats_t));
  s_emu.state = MFC_EMU_STATE_IDLE;
  s_emu.crypto_active = false;
  s_emu.pending_value = 0;
  s_emu.cb = NULL;
  s_emu.cb_ctx = NULL;
  s_emu.prng_state = get_cuid() ^ esp_random();
  s_emu.initialized = true;

  ESP_LOGI(TAG,
           "Emulator init: UID=%02X%02X%02X%02X SAK=0x%02X sectors=%d",
           card->uid[0],
           card->uid[1],
           card->uid[2],
           card->uid[3],
           card->sak,
           card->sector_count);

  return HB_NFC_OK;
}

void mfc_emu_set_callback(mfc_emu_event_cb_t cb, void *ctx) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return;
  s_emu.cb = cb;
  s_emu.cb_ctx = ctx;
}

hb_nfc_err_t mfc_emu_configure_target(void) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return HB_NFC_ERR_INTERNAL;
  if (!s_emu.initialized)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_prepare_target_tx() != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  ESP_LOGI(TAG, "=== Configuring ST25R3916 Target Mode ===");

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_OFF) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(MFC_EMU_DELAY_MEDIUM_MS));
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_prepare_target_tx() != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(MFC_EMU_DELAY_SHORT_MS));

  uint8_t ic_id = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &ic_id) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  ESP_LOGD(TAG, "IC Identity = 0x%02X", ic_id);
  if (ic_id == IC_IDENTITY_NOT_RESPONDING_LO || ic_id == IC_IDENTITY_NOT_RESPONDING_HI) {
    ESP_LOGE(TAG, "SPI not responding after reset!");
    return HB_NFC_ERR_INTERNAL;
  }

  /* The core is already fully initialized and calibrated by highboy_nfc_init().
   * Do not issue
   * SET_DEFAULT here: doing so discards the validated 3.3 V and
   * analog configuration
   * immediately before entering listen mode. */
  uint8_t mode_before = 0, io_conf2_before = 0, op_before = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_MODE, &mode_before) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_IO_CONF2, &io_conf2_before) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &op_before) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read the initialized target configuration");
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG,
           "Before target: MODE=0x%02X IO_CONF2=0x%02X OP=0x%02X",
           mode_before,
           io_conf2_before,
           op_before);
  if ((io_conf2_before & ST25R3916_IO_CONF2_SUP3V) == 0) {
    ESP_LOGE(TAG, "Core is not configured for the High Boy 3.3 V supply");
    return HB_NFC_ERR_INTERNAL;
  }

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_EN) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to keep the target oscillator enabled");
    return HB_NFC_ERR_INTERNAL;
  }

  if (s_emu.card.uid_len == 7) {
    if (hb_nfc_spi_reg_write(ST25R3916_REG_AUX_DEF, MFC_AUX_DEF_NFC_N) != ESP_OK)
      return HB_NFC_ERR_INTERNAL;
  } else {
    if (hb_nfc_spi_reg_write(ST25R3916_REG_AUX_DEF, 0x00) != ESP_OK)
      return HB_NFC_ERR_INTERNAL;
  }

  // A previous reader session may have selected the board's I/Q receiver.
  // Use the NFC-A listen receiver profile independently of the previous mode.
  if (hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF1, 0x08) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF2, 0x2D) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF3, 0x00) != ESP_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_RX_CONF4, 0x00) != ESP_OK ||
      hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF1, 0x51) != ESP_OK ||
      hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF2, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_PASSIVE_TARGET, ST25R3916_PASSIVE_TARGET_FDEL_5) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_ACT, MFC_FIELD_THRESH_ACT) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_DEACT, MFC_FIELD_THRESH_DEACT) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_PT_MOD, MFC_PT_MOD_OOK) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_RX_TIMER, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  hb_nfc_err_t err = load_pt_memory();
  if (err != HB_NFC_OK)
    return err;

  {
    st25r3916_irq_status_t s;
    if (st25r3916_irq_read(&s) != ESP_OK)
      return HB_NFC_ERR_INTERNAL;
  }
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_MAIN_INT, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TIMER_NFC_INT, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_ERROR_WUP_INT, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TARGET_INT, 0x00) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;

  /* MODE is applied only by target_enter_listen(), after GOTO_SENSE, matching
   * ST's RFAL listen
   * state machine. */
  uint8_t mode_rb = 0, aux_rb = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_MODE, &mode_rb) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DEF, &aux_rb) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  ESP_LOGD(TAG, "Prepared target: MODE=0x%02X AUX_DEF=0x%02X", mode_rb, aux_rb);

  ESP_LOGD(TAG, "=== Target configured ===");
  return HB_NFC_OK;
}

hb_nfc_err_t mfc_emu_start(void) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return HB_NFC_ERR_INTERNAL;
  if (!s_emu.initialized)
    return HB_NFC_ERR_INTERNAL;

  {
    st25r3916_irq_status_t s;
    if (st25r3916_irq_read(&s) != ESP_OK)
      return HB_NFC_ERR_INTERNAL;
  }
  if (target_enter_listen() != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(MFC_EMU_DELAY_SHORT_MS));

  uint8_t pt_sts = 0, op = 0, aux = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pt_sts) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &op) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  ESP_LOGI(TAG, "Start: PT_STS=0x%02X AUX=0x%02X OP=0x%02X", pt_sts, aux, op);

  s_emu.state = MFC_EMU_STATE_LISTEN;
  s_emu.last_activity_us = esp_timer_get_time();

  ESP_LOGI(TAG, "Emulator listening - bring the reader closer...");
  return HB_NFC_OK;
}

mfc_emu_state_t mfc_emu_run_step(void) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return MFC_EMU_STATE_ERROR;
  if (!s_emu.initialized)
    return MFC_EMU_STATE_ERROR;

  switch (s_emu.state) {
    case MFC_EMU_STATE_LISTEN: {
      if (!hb_nfc_gpio_irq_level()) {
        vTaskDelay(pdMS_TO_TICKS(1));
        break;
      }

      st25r3916_irq_status_t irq;
      if (read_irq(&irq) != ESP_OK) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        return s_emu.state;
      }
      uint8_t tgt_irq = irq.target;

      uint8_t pts = 0;
      if (hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts) != ESP_OK) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        return s_emu.state;
      }
      uint8_t pta = pts & PTA_STATE_MASK;

      /* WU_A/WU_A_X only announce that NFC-A wake-up/anticollision is in
       * progress.  They
       * can be raised while PTA state is still IDLE, so using
       * them as proof of selection
       * disables the automatic anticollision too
       * early.  Only move to application-frame
       * handling after the PT state
       * machine itself reports SELECTED/ACTIVE. */
      if (pta == PTA_STATE_SELECTED || pta == PTA_STATE_ACTIVE) {
        s_emu.stats.cycles++;
        s_emu.crypto_active = false;
        s_emu.last_activity_us = esp_timer_get_time();

        /* ST RFAL transitions from bitrate-detection listen mode to fixed
         * 106-kbit/s
         * NFC-A target mode before accepting application frames.
         * Automatic anticollision
         * must then be disabled so AUTH/READ reaches
         * the FIFO and raises the normal RXE
         * interrupt. */
        hb_nfc_spi_reg_modify(ST25R3916_REG_OP_CTRL,
                              ST25R3916_OP_CTRL_EN | ST25R3916_OP_CTRL_RX_EN,
                              ST25R3916_OP_CTRL_EN | ST25R3916_OP_CTRL_RX_EN);
        hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, 0x00);
        hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_TARGET_NFCA);
        hb_nfc_spi_reg_modify(ST25R3916_REG_PASSIVE_TARGET,
                              ST25R3916_PASSIVE_TARGET_DISABLE_106_AUTO,
                              ST25R3916_PASSIVE_TARGET_DISABLE_106_AUTO);
        hb_nfc_spi_reg_modify(ST25R3916_REG_ISO14443A,
                              ST25R3916_ISO14443A_NO_TX_PAR | ST25R3916_ISO14443A_NO_RX_PAR,
                              0);
        /* Do not clear FIFO or IRQs here.  The reader can send AUTH immediately
         * after
         * the automatic SAK; preserving a concurrently latched RXE/FIFO
         * frame is
         * required by ST's READY_A -> ACTIVE_A sequence. */

        emit_event(MFC_EMU_EVT_ACTIVATED);
        s_pending_irq = irq;
        s_emu.state = MFC_EMU_STATE_ACTIVATED;
        return s_emu.state;
      }

      if (pta == PTA_STATE_RF_DET || pta == PTA_STATE_SENSE || pta == PTA_STATE_RESOLUTION ||
          (tgt_irq & ST25R3916_IRQ_TGT_WU_A)) {
        ESP_LOGD(TAG, "Field detected (pta=%d)", pta);
        break;
      }

      break;
    }

    case MFC_EMU_STATE_ACTIVATED:
    case MFC_EMU_STATE_AUTHENTICATED: {
      st25r3916_irq_status_t irq;
      if (read_irq(&irq) != ESP_OK) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        return s_emu.state;
      }
      uint8_t main_irq = irq.main;
      uint8_t timer_irq = irq.timer;

      /* A reader may finish one RF phase at the same instant RXE reports the
       * following
       * application frame.  RX must win over EOF; otherwise the
       * first MIFARE AUTH command
       * is discarded and the emulator immediately
       * returns to LISTEN. */
      if (!(main_irq & ST25R3916_IRQ_MAIN_RXE)) {
        if (timer_irq & 0x08) {
          uint8_t aux = 0;
          hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
          if ((aux & ST25R3916_AUX_DISP_EFD_BIT) == 0U) {
            ESP_LOGW(TAG,
                     "Field lost -> LISTEN (main=0x%02X timer=0x%02X aux=0x%02X)",
                     main_irq,
                     timer_irq,
                     aux);
            reset_crypto_state();
            s_emu.stats.field_losses++;
            emit_event(MFC_EMU_EVT_FIELD_LOST);
            if (target_enter_listen() != HB_NFC_OK)
              return MFC_EMU_STATE_ERROR;
            s_emu.state = MFC_EMU_STATE_LISTEN;
            return s_emu.state;
          }
        }
        break;
      }

      uint8_t enc[20] = {0};
      uint8_t cmd[20] = {0};
      int len = fifo_rx(enc, sizeof(enc));
      if (len < 0) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        break;
      }
      if (len == 0)
        break;

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

      if (s_emu.crypto_active && !command_frame_valid(cmd, (size_t)len)) {
        if (target_tx_ack_encrypted(MFC_NACK_PARITY_CRC) != HB_NFC_OK)
          s_emu.state = MFC_EMU_STATE_ERROR;
        s_emu.stats.nacks_sent++;
        break;
      }

      if (cmd_byte == MFC_CMD_AUTH_KEY_A || cmd_byte == MFC_CMD_AUTH_KEY_B) {
        if (len >= 2)
          s_emu.state = handle_auth(cmd_byte, cmd[1]);
      } else if (s_emu.crypto_active) {
        switch (cmd_byte) {
          case MFC_CMD_READ:
            if (len >= 2)
              s_emu.state = handle_read(cmd[1]);
            break;
          case MFC_CMD_WRITE:
            if (len >= 2)
              s_emu.state = handle_write_phase1(cmd[1]);
            break;
          case MFC_CMD_INCREMENT:
          case MFC_CMD_DECREMENT:
          case MFC_CMD_RESTORE:
            if (len >= 2)
              s_emu.state = handle_value_op_phase1(cmd_byte, cmd[1]);
            break;
          case MFC_CMD_TRANSFER:
            if (len >= 2)
              s_emu.state = handle_transfer(cmd[1]);
            break;
          case MFC_CMD_HALT:
            s_emu.state = handle_halt();
            break;
          default:
            ESP_LOGW(TAG, "Unknown CMD: 0x%02X len=%d", cmd_byte, len);
            s_emu.stats.unknown_cmds++;
            target_tx_ack_encrypted(MFC_NACK_INVALID_OP);
            s_emu.stats.nacks_sent++;
            break;
        }
      } else if (cmd_byte == MFC_CMD_HALT) {
        s_emu.state = handle_halt();
      } else {
        ESP_LOGW(TAG, "CMD 0x%02X without authentication", cmd_byte);
        s_emu.stats.unknown_cmds++;
        s_emu.state = MFC_EMU_STATE_HALTED;
      }
      break;
    }

    case MFC_EMU_STATE_WRITE_PENDING: {
      st25r3916_irq_status_t irq;
      if (read_irq(&irq) != ESP_OK) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        return s_emu.state;
      }
      uint8_t main_irq = irq.main, timer_irq = irq.timer;

      if (!(main_irq & ST25R3916_IRQ_MAIN_RXE) && (timer_irq & 0x08)) {
        uint8_t aux = 0;
        hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
        if (((aux >> 6) & 1U) == 0U) {
          reset_crypto_state();
          s_emu.stats.field_losses++;
          emit_event(MFC_EMU_EVT_FIELD_LOST);
          if (target_enter_listen() != HB_NFC_OK)
            return MFC_EMU_STATE_ERROR;
          s_emu.state = MFC_EMU_STATE_LISTEN;
          return s_emu.state;
        }
      }

      if (!(main_irq & ST25R3916_IRQ_MAIN_RXE))
        break;

      uint8_t enc[20] = {0};
      int len = fifo_rx(enc, sizeof(enc));
      if (len < 0) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        break;
      }
      if (len == 0)
        break;

      uint8_t plain[20] = {0};
      for (int i = 0; i < len; i++) {
        uint8_t ks = crypto1_byte(&s_emu.crypto, 0, 0);
        plain[i] = enc[i] ^ ks;
      }
      s_emu.state = handle_write_phase2(plain, len);
      break;
    }

    case MFC_EMU_STATE_VALUE_PENDING: {
      st25r3916_irq_status_t irq;
      if (read_irq(&irq) != ESP_OK) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        return s_emu.state;
      }
      uint8_t main_irq = irq.main, timer_irq = irq.timer;

      if (!(main_irq & ST25R3916_IRQ_MAIN_RXE) && (timer_irq & 0x08)) {
        uint8_t aux = 0;
        hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
        if (((aux >> 6) & 1U) == 0U) {
          reset_crypto_state();
          s_emu.stats.field_losses++;
          emit_event(MFC_EMU_EVT_FIELD_LOST);
          if (target_enter_listen() != HB_NFC_OK)
            return MFC_EMU_STATE_ERROR;
          s_emu.state = MFC_EMU_STATE_LISTEN;
          return s_emu.state;
        }
      }

      if (!(main_irq & ST25R3916_IRQ_MAIN_RXE))
        break;

      uint8_t enc[8] = {0};
      int len = fifo_rx(enc, sizeof(enc));
      if (len < 0) {
        s_emu.state = MFC_EMU_STATE_ERROR;
        break;
      }
      if (len == 0)
        break;

      uint8_t plain[8] = {0};
      for (int i = 0; i < len; i++) {
        uint8_t ks = crypto1_byte(&s_emu.crypto, 0, 0);
        plain[i] = enc[i] ^ ks;
      }
      s_emu.state = handle_value_op_phase2(plain, len);
      break;
    }

    case MFC_EMU_STATE_HALTED:
      reset_crypto_state();
      if (target_enter_sleep() != HB_NFC_OK)
        return MFC_EMU_STATE_ERROR;
      s_emu.state = MFC_EMU_STATE_LISTEN;
      break;

    case MFC_EMU_STATE_ERROR: {
      reset_crypto_state();
      if (target_enter_listen() != HB_NFC_OK)
        return MFC_EMU_STATE_ERROR;
      s_emu.state = MFC_EMU_STATE_LISTEN;
      break;
    }

    default:
      break;
  }

  if (s_emu.state == MFC_EMU_STATE_ERROR) {
    reset_crypto_state();
    if (target_enter_listen() != HB_NFC_OK)
      return MFC_EMU_STATE_ERROR;
    s_emu.state = MFC_EMU_STATE_LISTEN;
  }
  return s_emu.state;
}

hb_nfc_err_t mfc_emu_update_card(const mfc_emu_card_data_t *card) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return HB_NFC_ERR_INTERNAL;
  if (card == NULL)
    return HB_NFC_ERR_PARAM;

  reset_crypto_state();
  memcpy(&s_emu.card, card, sizeof(mfc_emu_card_data_t));
  s_emu.prng_state = get_cuid() ^ esp_random();

  if (s_emu.state != MFC_EMU_STATE_IDLE)
    load_pt_memory();

  ESP_LOGI(TAG,
           "Card data updated: UID=%02X%02X%02X%02X",
           card->uid[0],
           card->uid[1],
           card->uid[2],
           card->uid[3]);

  return HB_NFC_OK;
}

void mfc_emu_stop(void) {
  if (!rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return;
  ESP_LOGI(TAG, "Emulator stopping...");
  reset_crypto_state();
  s_emu.state = MFC_EMU_STATE_IDLE;
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL);
}

mfc_emu_stats_t mfc_emu_get_stats(void) {
  return s_emu.stats;
}
mfc_emu_state_t mfc_emu_get_state(void) {
  return s_emu.state;
}

const char *mfc_emu_state_str(mfc_emu_state_t state) {
  switch (state) {
    case MFC_EMU_STATE_IDLE:
      return "IDLE";
    case MFC_EMU_STATE_LISTEN:
      return "LISTEN";
    case MFC_EMU_STATE_ACTIVATED:
      return "ACTIVATED";
    case MFC_EMU_STATE_AUTH_SENT_NT:
      return "AUTH_SENT_NT";
    case MFC_EMU_STATE_AUTHENTICATED:
      return "AUTHENTICATED";
    case MFC_EMU_STATE_WRITE_PENDING:
      return "WRITE_PENDING";
    case MFC_EMU_STATE_VALUE_PENDING:
      return "VALUE_PENDING";
    case MFC_EMU_STATE_HALTED:
      return "HALTED";
    case MFC_EMU_STATE_ERROR:
      return "ERROR";
    default:
      return "?";
  }
}

void mfc_emu_card_data_init(mfc_emu_card_data_t *cd,
                            const nfc_iso14443a_data_t *card,
                            mf_classic_type_t type) {
  memset(cd, 0, sizeof(*cd));

  memcpy(cd->uid, card->uid, card->uid_len);
  cd->uid_len = card->uid_len;
  memcpy(cd->atqa, card->atqa, 2);
  cd->sak = card->sak;
  if (cd->uid_len == 4 && cd->sak == NFC_SAK_CASCADE) {
    ESP_LOGW(TAG, "SAK 0x%02X with 4-byte UID; forcing 0x08 for emulation", NFC_SAK_CASCADE);
    cd->sak = 0x08;
  }
  cd->type = type;

  switch (type) {
    case MF_CLASSIC_MINI:
      cd->sector_count = 5;
      cd->total_blocks = 20;
      break;
    case MF_CLASSIC_1K:
      cd->sector_count = 16;
      cd->total_blocks = 64;
      break;
    case MF_CLASSIC_4K:
      cd->sector_count = 40;
      cd->total_blocks = 256;
      break;
    default:
      cd->sector_count = 16;
      cd->total_blocks = 64;
      break;
  }
}
