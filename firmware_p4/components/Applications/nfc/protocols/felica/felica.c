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

#include "felica.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"

#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "nfc_rf.h"

static const char *TAG = "NFC_FELICA";

#define FELICA_CRC_BYTES 2

static int frame_length(const uint8_t *frame, int received) {
  if (received < FELICA_FRAME_MIN_HDR_LEN)
    return 0;
  return received == frame[0] || received == frame[0] + FELICA_CRC_BYTES ? frame[0] : 0;
}

hb_nfc_err_t felica_poller_init(void) {
  nfc_rf_config_t cfg = {
      .tech = NFC_RF_TECH_F,
      .tx_rate = NFC_RF_BR_212,
      .rx_rate = NFC_RF_BR_212,
      .am_mod_percent = FELICA_AM_MOD_PERCENT,
      .tx_parity = true,
      .rx_raw_parity = false,
      .guard_time_us = 0,
      .fdt_min_us = 0,
      .validate_fdt = false,
  };
  hb_nfc_err_t err = nfc_rf_apply(&cfg);
  if (err != HB_NFC_OK)
    return err;

  ESP_LOGI(TAG, "FeliCa poller ready (NFC-F 212 kbps)");
  return HB_NFC_OK;
}

hb_nfc_err_t felica_sensf_req(uint16_t system_code, felica_tag_t *tag) {
  return felica_sensf_req_slots(system_code, 0, tag);
}

hb_nfc_err_t felica_sensf_req_slots(uint16_t system_code, uint8_t time_slots, felica_tag_t *tag) {
  if (tag == NULL || (time_slots != 0 && time_slots != 1 && time_slots != 3 && time_slots != 7 &&
                      time_slots != 15))
    return HB_NFC_ERR_PARAM;
  memset(tag, 0, sizeof(*tag));

  uint8_t cmd[FELICA_SENSF_REQ_LENGTH];
  cmd[0] = FELICA_SENSF_REQ_LENGTH;
  cmd[1] = FELICA_CMD_SENSF_REQ;
  cmd[2] = (uint8_t)(system_code >> 8);
  cmd[3] = (uint8_t)system_code;
  cmd[4] = FELICA_RCF_SYSTEM_CODE;
  cmd[5] = time_slots;

  uint8_t rx[FELICA_SENSF_RES_BUFFER_SIZE] = {0};
  int len = nfc_poller_transceive(
      cmd + 1, sizeof(cmd) - 1, true, rx, sizeof(rx), 1, FELICA_SENSF_TIMEOUT_MS);
  len = frame_length(rx, len);
  if (len < FELICA_SENSF_RES_MIN_LEN) {
    if (len > 0)
      nfc_log_hex("SENSF partial:", rx, (size_t)len);
    return HB_NFC_ERR_NO_CARD;
  }

  nfc_log_hex("SENSF_RES:", rx, (size_t)len);

  if (rx[1] != FELICA_CMD_SENSF_RES || rx[0] != len ||
      (len != FELICA_SENSF_RES_MIN_LEN && len != FELICA_SENSF_RES_WITH_RD_LEN)) {
    ESP_LOGW(TAG, "Unexpected response code 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }

  memcpy(tag->idm, &rx[FELICA_SENSF_RES_IDM_OFFSET], FELICA_IDM_LEN);
  memcpy(tag->pmm, &rx[FELICA_SENSF_RES_PMM_OFFSET], FELICA_PMM_LEN);
  if (len >= FELICA_SENSF_RES_WITH_RD_LEN) {
    tag->rd[0] = rx[FELICA_SENSF_RES_RD0_OFFSET];
    tag->rd[1] = rx[FELICA_SENSF_RES_RD1_OFFSET];
    tag->rd_valid = true;
  }

  char idm_str[FELICA_IDM_STR_BUFFER_SIZE];
  snprintf(idm_str,
           sizeof(idm_str),
           "%02X%02X%02X%02X%02X%02X%02X%02X",
           tag->idm[0],
           tag->idm[1],
           tag->idm[2],
           tag->idm[3],
           tag->idm[4],
           tag->idm[5],
           tag->idm[6],
           tag->idm[7]);
  ESP_LOGI(TAG, "Tag found: IDm=%s", idm_str);
  if (tag->rd_valid) {
    ESP_LOGI(TAG, "System code: %02X%02X", tag->rd[0], tag->rd[1]);
  }
  return HB_NFC_OK;
}

int felica_polling(uint16_t system_code, uint8_t time_slots, felica_tag_t *out, size_t max_tags) {
  if (out == NULL || max_tags == 0)
    return 0;

  int count = 0;
  uint8_t tries = (time_slots == 0) ? 1 : (uint8_t)(time_slots + 1);
  for (uint8_t i = 0; i < tries && count < (int)max_tags; i++) {
    felica_tag_t tag = {0};
    if (felica_sensf_req_slots(system_code, time_slots, &tag) != HB_NFC_OK) {
      continue;
    }

    bool dup = false;
    for (int k = 0; k < count; k++) {
      if (memcmp(out[k].idm, tag.idm, FELICA_IDM_LEN) == 0) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;

    out[count++] = tag;
  }

  return count;
}

hb_nfc_err_t felica_read_blocks(const felica_tag_t *tag,
                                uint16_t service_code,
                                uint8_t first_block,
                                uint8_t count,
                                uint8_t *out) {
  if (tag == NULL || out == NULL || count == 0 || count > FELICA_READ_MAX_BLOCKS ||
      (size_t)first_block + count > UINT8_MAX + 1U)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[FELICA_READ_CMD_BUFFER_SIZE];
  int pos = 0;

  cmd[pos++] = 0x00U;
  cmd[pos++] = FELICA_CMD_READ_WO_ENC;
  memcpy(&cmd[pos], tag->idm, FELICA_IDM_LEN);
  pos += FELICA_IDM_LEN;
  cmd[pos++] = 0x01U;
  cmd[pos++] = (uint8_t)(service_code & 0xFFU);
  cmd[pos++] = (uint8_t)(service_code >> 8);
  cmd[pos++] = count;
  for (int i = 0; i < count; i++) {
    cmd[pos++] = FELICA_BLOCK_DESC_BYTE;
    cmd[pos++] = (uint8_t)(first_block + i);
  }
  cmd[0] = (uint8_t)pos;

  uint8_t rx[FELICA_READ_RES_BUFFER_SIZE] = {0};
  int len = nfc_poller_transceive(
      cmd + 1, (size_t)pos - 1, true, rx, sizeof(rx), 1, FELICA_READ_TIMEOUT_MS);
  len = frame_length(rx, len);
  if (len < FELICA_READ_RES_MIN_LEN) {
    ESP_LOGW(TAG, "ReadBlocks: timeout (len=%d)", len);
    return HB_NFC_ERR_TIMEOUT;
  }

  if (rx[1] != FELICA_CMD_READ_RESP || rx[0] != len ||
      memcmp(&rx[FELICA_READ_RES_IDM_OFFSET], tag->idm, FELICA_IDM_LEN) != 0) {
    ESP_LOGW(TAG, "ReadBlocks: unexpected resp 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }

  uint8_t status1 = rx[FELICA_READ_RES_STATUS1_POS];
  uint8_t status2 = rx[FELICA_READ_RES_STATUS2_POS];
  if (status1 != 0x00U || status2 != 0x00U) {
    ESP_LOGW(TAG, "ReadBlocks: status1=0x%02X status2=0x%02X", status1, status2);
    return HB_NFC_ERR_PROTOCOL;
  }

  len = frame_length(rx, len);
  if (len < FELICA_READ_RES_DATA_POS)
    return HB_NFC_ERR_PROTOCOL;
  uint8_t blk_count = rx[FELICA_READ_RES_COUNT_POS];
  if (blk_count != count || len != FELICA_READ_RES_DATA_POS + count * FELICA_BLOCK_SIZE) {
    ESP_LOGW(TAG, "ReadBlocks: response too short");
    return HB_NFC_ERR_PROTOCOL;
  }

  memcpy(out, &rx[FELICA_READ_RES_DATA_POS], (size_t)blk_count * FELICA_BLOCK_SIZE);
  return HB_NFC_OK;
}

hb_nfc_err_t felica_write_blocks(const felica_tag_t *tag,
                                 uint16_t service_code,
                                 uint8_t first_block,
                                 uint8_t count,
                                 const uint8_t *data) {
  if (tag == NULL || data == NULL || count == 0 || count > 1)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[FELICA_WRITE_CMD_BUFFER_SIZE];
  int pos = 0;

  cmd[pos++] = 0x00U;
  cmd[pos++] = FELICA_CMD_WRITE_WO_ENC;
  memcpy(&cmd[pos], tag->idm, FELICA_IDM_LEN);
  pos += FELICA_IDM_LEN;
  cmd[pos++] = 0x01U;
  cmd[pos++] = (uint8_t)(service_code & 0xFFU);
  cmd[pos++] = (uint8_t)(service_code >> 8);
  cmd[pos++] = count;
  for (int i = 0; i < count; i++) {
    cmd[pos++] = FELICA_BLOCK_DESC_BYTE;
    cmd[pos++] = (uint8_t)(first_block + i);
    memcpy(&cmd[pos], data + i * FELICA_BLOCK_SIZE, FELICA_BLOCK_SIZE);
    pos += FELICA_BLOCK_SIZE;
  }
  cmd[0] = (uint8_t)pos;

  uint8_t rx[FELICA_WRITE_RES_BUFFER_SIZE] = {0};
  int len = nfc_poller_transceive(
      cmd + 1, (size_t)pos - 1, true, rx, sizeof(rx), 1, FELICA_WRITE_TIMEOUT_MS);
  len = frame_length(rx, len);
  if (len < FELICA_WRITE_RES_MIN_LEN) {
    ESP_LOGW(TAG, "WriteBlocks: timeout (len=%d)", len);
    return HB_NFC_ERR_TIMEOUT;
  }

  if (rx[0] != len || rx[1] != FELICA_CMD_WRITE_RESP ||
      memcmp(&rx[FELICA_WRITE_RES_IDM_OFFSET], tag->idm, FELICA_IDM_LEN) != 0)
    return HB_NFC_ERR_PROTOCOL;
  uint8_t status1 = rx[FELICA_WRITE_RES_STATUS1_POS];
  uint8_t status2 = rx[FELICA_WRITE_RES_STATUS2_POS];
  if (status1 != 0x00U || status2 != 0x00U) {
    ESP_LOGW(TAG, "WriteBlocks: status1=0x%02X status2=0x%02X", status1, status2);
    return HB_NFC_ERR_NACK;
  }

  ESP_LOGI(TAG, "WriteBlock[%u]: OK", first_block);
  return HB_NFC_OK;
}

void felica_dump_card(void) {
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "FeliCa / NFC-F Read");
  ESP_LOGI(TAG, "");

  felica_poller_init();

  felica_tag_t tag = {0};
  hb_nfc_err_t err = felica_sensf_req(FELICA_SC_COMMON, &tag);
  if (err != HB_NFC_OK) {
    ESP_LOGW(TAG, "No FeliCa tag found");
    return;
  }

  static const struct {
    uint16_t sc;
    const char *name;
  } services[] = {
      {FELICA_SERVICE_COMMON_RO, "Common Area (RO)"},
      {FELICA_SERVICE_COMMON_RW, "Common Area (RW)"},
      {FELICA_SERVICE_NDEF, "NDEF service"},
  };

  char hex[FELICA_HEX_DUMP_BUFFER_SIZE], asc[FELICA_ASCII_DUMP_BUFFER_SIZE];
  uint8_t block[FELICA_BLOCK_SIZE];

  for (size_t si = 0; si < sizeof(services) / sizeof(services[0]); si++) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Service 0x%04X - %s:", services[si].sc, services[si].name);

    int read_ok = 0;
    for (int b = 0; b < FELICA_MAX_BLOCKS; b++) {
      err = felica_read_blocks(&tag, services[si].sc, (uint8_t)b, 1, block);
      if (err != HB_NFC_OK)
        break;

      size_t p = 0;
      for (int i = 0; i < FELICA_BLOCK_SIZE; i++) {
        p += (size_t)snprintf(hex + p, sizeof(hex) - p, "%02X ", block[i]);
      }

      for (int i = 0; i < FELICA_BLOCK_SIZE; i++) {
        asc[i] = (block[i] >= ASCII_PRINTABLE_START && block[i] <= ASCII_PRINTABLE_END)
                     ? (char)block[i]
                     : '.';
      }
      asc[FELICA_BLOCK_SIZE] = '\0';

      ESP_LOGI(TAG, "[%02d] %s|%s|", b, hex, asc);
      read_ok++;
    }
    if (read_ok == 0) {
      ESP_LOGW(TAG, "(no blocks readable)");
    }
  }
  ESP_LOGI(TAG, "");
}
