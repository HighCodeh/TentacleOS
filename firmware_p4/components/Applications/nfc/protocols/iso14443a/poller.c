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
 * @file poller.c
 * @brief ISO14443A poller helpers.
 */
#include "poller.h"
#include "iso14443a.h"
#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_cmd.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"
#include "esp_timer.h"
#include "nfc_rf.h"

#include "esp_log.h"
#include <string.h>

static const char *TAG = "iso14443a_poller";

#define ANTICOLL_BVR          0x20U /* Bit code for anti-collision */
#define SELECT_CMD            0x70U /* SELECT command code */
#define UID_CASCADE_TAG       0x88U /* UID cascade indicator */
#define SAK_MORE_CL_MASK      0x04U /* SAK bit indicating more cascade levels */
#define FIFO_ATQA_LEN         2
#define ANTICOLL_RESPONSE_LEN 5
#define SELECT_RESPONSE_LEN   3
#define CASCADE_LEVELS        3
#define ANTICOLL_ATTEMPTS     3
#define ANTICOLL_CMD_LEN      2
#define SELECT_CMD_LEN        7
#define SELECT_RX_BUF_SIZE    4
#define ANTICOLL_TIMEOUT_MS   20
#define SELECT_TIMEOUT_MS     10
#define REACTIVATE_DELAY_MS   5
#define ANTICOLL_DELAY_MS     5
#define REQA_SUCCESS_LEN      2
#define ANTICOLL_DELAY_LONG   10
#define HALT_CMD_BYTE0        0x50U
#define HALT_TIMEOUT_MS       10
#define HALT_SETTLE_MS        5
#define FIFO_ATQA_EXPECTED    2

static void set_antcl(bool enable);
static int req_cmd(uint8_t cmd, uint8_t atqa[FIFO_ATQA_LEN]);
static hb_nfc_err_t iso14443a_select_from_atqa(const uint8_t atqa[FIFO_ATQA_LEN],
                                               nfc_iso14443a_data_t *card);

/**
 * Enable or disable anti-collision.
 */
static void set_antcl(bool enable) {
  uint8_t v;
  hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &v);
  if (enable)
    v |= ST25R3916_ISO14443A_ANTCL;
  else
    v &= (uint8_t)~ST25R3916_ISO14443A_ANTCL;
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, v);
}

/**
 * Send REQA or WUPA and read ATQA.
 */
static int req_cmd(uint8_t cmd, uint8_t atqa[FIFO_ATQA_LEN]) {
  set_antcl(false);
  /* ST reference sequence: stop/clear, reset RX gain, clear partial-byte count,
   * then issue the
   * dedicated 7-bit REQA or WUPA command. */
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_STOP_ALL);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_RESET_RX_GAIN);
  hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES2, 0x00);
  if (st25r3916_irq_clear() != ESP_OK)
    return 0;
  hb_nfc_spi_direct_cmd(cmd);

  st25r3916_irq_status_t irq = {0};
  esp_err_t waited = st25r3916_irq_wait_exchange(ANTICOLL_DELAY_LONG, &irq);
  uint16_t count = st25r3916_fifo_get_count();
  if (waited != ESP_OK || count != FIFO_ATQA_LEN || irq.error != 0 ||
      (irq.main & ST25R3916_IRQ_MAIN_COL) != 0)
    return 0;
  if (st25r3916_fifo_read(atqa, FIFO_ATQA_LEN) != ESP_OK)
    return 0;
  return FIFO_ATQA_LEN;
}

int iso14443a_poller_reqa(uint8_t atqa[2]) {
  return req_cmd(ST25R3916_CMD_TX_REQA, atqa);
}

int iso14443a_poller_wupa(uint8_t atqa[2]) {
  return req_cmd(ST25R3916_CMD_TX_WUPA, atqa);
}

hb_nfc_err_t iso14443a_poller_activate(uint8_t atqa[FIFO_ATQA_LEN]) {
  if (req_cmd(ST25R3916_CMD_TX_REQA, atqa) == REQA_SUCCESS_LEN)
    return HB_NFC_OK;
  hb_nfc_timer_delay_ms(REACTIVATE_DELAY_MS);
  if (req_cmd(ST25R3916_CMD_TX_WUPA, atqa) == REQA_SUCCESS_LEN)
    return HB_NFC_OK;
  return HB_NFC_ERR_NO_CARD;
}

int iso14443a_poller_anticoll(uint8_t sel, uint8_t uid_cl[ANTICOLL_RESPONSE_LEN]) {
  uint8_t cmd[ANTICOLL_CMD_LEN] = {sel, ANTICOLL_BVR};

  for (int attempt = 0; attempt < ANTICOLL_ATTEMPTS; attempt++) {
    set_antcl(true);
    int len = nfc_poller_transceive(cmd,
                                    ANTICOLL_CMD_LEN,
                                    false,
                                    uid_cl,
                                    ANTICOLL_RESPONSE_LEN,
                                    ANTICOLL_RESPONSE_LEN,
                                    ANTICOLL_TIMEOUT_MS);
    set_antcl(false);

    if (len == ANTICOLL_RESPONSE_LEN &&
        (uint8_t)(uid_cl[0] ^ uid_cl[1] ^ uid_cl[2] ^ uid_cl[3]) == uid_cl[4])
      return ANTICOLL_RESPONSE_LEN;
    if (len == ANTICOLL_RESPONSE_LEN)
      nfc_log_hex("Invalid UID BCC:", uid_cl, ANTICOLL_RESPONSE_LEN);
    hb_nfc_timer_delay_ms(ANTICOLL_DELAY_MS);
  }
  return 0;
}

int iso14443a_poller_sel(uint8_t sel, const uint8_t uid_cl[ANTICOLL_RESPONSE_LEN], uint8_t *sak) {
  uint8_t cmd[SELECT_CMD_LEN] = {
      sel, SELECT_CMD, uid_cl[0], uid_cl[1], uid_cl[2], uid_cl[3], uid_cl[4]};
  uint8_t rx[SELECT_RX_BUF_SIZE] = {0};
  int len = nfc_poller_transceive(
      cmd, SELECT_CMD_LEN, true, rx, sizeof(rx), SELECT_RESPONSE_LEN, SELECT_TIMEOUT_MS);
  if (len != SELECT_RESPONSE_LEN || !iso14443a_check_crc(rx, (size_t)len)) {
    nfc_log_hex("SELECT unanswered:", cmd, sizeof(cmd));
    return 0;
  }
  *sak = rx[0];
  return 1;
}

static hb_nfc_err_t iso14443a_select_from_atqa(const uint8_t atqa[FIFO_ATQA_LEN],
                                               nfc_iso14443a_data_t *card) {
  if (card == NULL || atqa == NULL)
    return HB_NFC_ERR_PARAM;

  nfc_iso14443a_data_t selected = {0};
  selected.atqa[0] = atqa[0];
  selected.atqa[1] = atqa[1];

  static const uint8_t sel_cmds[] = {SEL_CL1, SEL_CL2, SEL_CL3};

  for (int cl = 0; cl < CASCADE_LEVELS; cl++) {
    uint8_t uid_cl[ANTICOLL_RESPONSE_LEN] = {0};
    uint8_t sak = 0;

    if (iso14443a_poller_anticoll(sel_cmds[cl], uid_cl) != ANTICOLL_RESPONSE_LEN) {
      ESP_LOGW(TAG, "Anticoll CL%d failed", cl + 1);
      return HB_NFC_ERR_COLLISION;
    }
    if (!iso14443a_poller_sel(sel_cmds[cl], uid_cl, &sak)) {
      ESP_LOGW(TAG, "Select CL%d failed", cl + 1);
      return HB_NFC_ERR_PROTOCOL;
    }

    const bool has_more = (sak & SAK_MORE_CL_MASK) != 0;
    if ((cl < CASCADE_LEVELS - 1 && has_more != (uid_cl[0] == UID_CASCADE_TAG)) ||
        (has_more && cl == CASCADE_LEVELS - 1))
      return HB_NFC_ERR_PROTOCOL;
    if (has_more) {
      selected.uid[selected.uid_len++] = uid_cl[1];
      selected.uid[selected.uid_len++] = uid_cl[2];
      selected.uid[selected.uid_len++] = uid_cl[3];
    } else {
      selected.uid[selected.uid_len++] = uid_cl[0];
      selected.uid[selected.uid_len++] = uid_cl[1];
      selected.uid[selected.uid_len++] = uid_cl[2];
      selected.uid[selected.uid_len++] = uid_cl[3];
    }

    selected.sak = sak;
    if (!has_more) {
      *card = selected;
      return HB_NFC_OK;
    }
  }

  return HB_NFC_ERR_PROTOCOL;
}

hb_nfc_err_t iso14443a_poller_select(nfc_iso14443a_data_t *card) {
  if (card == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t atqa[FIFO_ATQA_LEN];
  hb_nfc_err_t err = iso14443a_poller_activate(atqa);
  if (err != HB_NFC_OK)
    return err;

  nfc_log_hex("ATQA:", atqa, FIFO_ATQA_LEN);
  hb_nfc_err_t sel_err = iso14443a_select_from_atqa(atqa, card);
  if (sel_err != HB_NFC_OK)
    return sel_err;
  nfc_log_hex("UID:", card->uid, card->uid_len);
  return HB_NFC_OK;
}

hb_nfc_err_t iso14443a_poller_reselect(nfc_iso14443a_data_t *card) {
  if (card == NULL || card->uid_len == 0 || card->uid_len > sizeof(card->uid))
    return HB_NFC_ERR_PARAM;
  if (st25r3916_core_field_cycle() != ESP_OK)
    return HB_NFC_ERR_FIELD;
  nfc_iso14443a_data_t selected = {0};
  hb_nfc_err_t err = iso14443a_poller_select(&selected);
  if (err != HB_NFC_OK)
    return err;
  // Do not attribute a replacement tag's data to the original UID.
  if (selected.uid_len != card->uid_len || memcmp(selected.uid, card->uid, card->uid_len) != 0)
    return HB_NFC_ERR_PROTOCOL;
  *card = selected;
  return HB_NFC_OK;
}

hb_nfc_err_t iso14443a_poller_halt(void) {
  uint8_t cmd[2] = {HALT_CMD_BYTE0, 0x00};
  uint8_t rx[4] = {0};
  (void)nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 0, HALT_TIMEOUT_MS);
  return HB_NFC_OK;
}

int iso14443a_poller_select_all(nfc_iso14443a_data_t *out, size_t max_cards) {
  if (out == NULL || max_cards == 0)
    return 0;

  size_t count = 0;
  for (;;) {
    if (count >= max_cards)
      break;

    uint8_t atqa[2] = {0};
    if (iso14443a_poller_reqa(atqa) != FIFO_ATQA_LEN)
      break;

    nfc_iso14443a_data_t card = {0};
    if (iso14443a_select_from_atqa(atqa, &card) != HB_NFC_OK)
      break;

    out[count++] = card;
    (void)iso14443a_poller_halt();
    hb_nfc_timer_delay_ms(HALT_SETTLE_MS);
  }

  return (int)count;
}
