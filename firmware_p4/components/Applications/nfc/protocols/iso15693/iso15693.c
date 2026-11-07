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
 * @file iso15693.c
 * @brief ISO 15693 (NFC-V) poller implementation.
 */
#include "iso15693.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

#include "nfc_poller.h"
#include "nfc_common.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "nfc_rf.h"

static const char *TAG = "NFC_ISO15693";

#define ISO15693_CRC_LEN             2
#define ISO15693_CRC_INIT            0xFFFFU
#define ISO15693_CRC_POLY            0x8408U
#define ISO15693_DEFAULT_BLOCK_SIZE  4
#define ISO15693_DEFAULT_BLOCK_COUNT 16
#define ISO15693_BLOCK_SIZE_MASK     0x1FU
#define ISO15693_INVENTORY_MIN_LEN   10
#define ISO15693_AM_MOD_PERCENT      10

#define ISO15693_SYSINFO_FLAG_DSFID 0x01U
#define ISO15693_SYSINFO_FLAG_AFI   0x02U
#define ISO15693_SYSINFO_FLAG_MEM   0x04U
#define ISO15693_SYSINFO_FLAG_IC    0x08U

#define ISO15693_TIMEOUT_MS       30
#define ISO15693_TIMEOUT_QUIET_MS 10
#define ISO15693_TIMEOUT_MULTI_MS 50
#define ISO15693_BITS_PER_BYTE    8

static int strip_crc_len(const uint8_t *buf, int len);
static hb_nfc_err_t iso15693_stay_quiet(const iso15693_tag_t *tag);

static int strip_crc_len(const uint8_t *buf, int len) {
  if (len >= (int)(ISO15693_CRC_LEN + 1) && iso15693_check_crc(buf, (size_t)len))
    return len - ISO15693_CRC_LEN;
  return 0;
}

static hb_nfc_err_t iso15693_stay_quiet(const iso15693_tag_t *tag) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[2 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_STAY_QUIET;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;

  uint8_t rx[8] = {0};
  (void)nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 0, ISO15693_TIMEOUT_QUIET_MS);
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_poller_init(void) {
  nfc_rf_config_t cfg = {
      .tech = NFC_RF_TECH_V,
      .tx_rate = NFC_RF_BR_V_HIGH,
      .rx_rate = NFC_RF_BR_V_HIGH,
      .am_mod_percent = ISO15693_AM_MOD_PERCENT,
      .tx_parity = true,
      .rx_raw_parity = false,
      .guard_time_us = 0,
      .fdt_min_us = 0,
      .validate_fdt = false,
  };
  hb_nfc_err_t err = nfc_rf_apply(&cfg);
  if (err != HB_NFC_OK)
    return err;

  if (!st25r3916_core_field_is_on()) {
    err = st25r3916_core_field_on();
    if (err != HB_NFC_OK)
      return err;
  }

  ESP_LOGI(TAG, "ISO15693 poller ready (NFC-V 26.48 kbps)");
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_inventory(iso15693_tag_t *tag) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;
  memset(tag, 0, sizeof(*tag));

  uint8_t cmd[3] = {ISO15693_FLAGS_INVENTORY, ISO15693_CMD_INVENTORY, 0x00U};

  uint8_t rx[32] = {0};
  int len = nfc_poller_transceive(cmd, sizeof(cmd), true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < ISO15693_INVENTORY_MIN_LEN) {
    if (len > 0)
      nfc_log_hex("INVENTORY partial:", rx, (size_t)len);
    return HB_NFC_ERR_NO_CARD;
  }

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "INVENTORY error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < ISO15693_INVENTORY_MIN_LEN)
    return HB_NFC_ERR_PROTOCOL;

  tag->dsfid = rx[1];
  memcpy(tag->uid, &rx[2], ISO15693_UID_LEN);
  tag->info_valid = false;
  ESP_LOGI(TAG,
           "Tag found (UID=%02X%02X%02X%02X%02X%02X%02X%02X)",
           tag->uid[7],
           tag->uid[6],
           tag->uid[5],
           tag->uid[4],
           tag->uid[3],
           tag->uid[2],
           tag->uid[1],
           tag->uid[0]);
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_get_system_info(iso15693_tag_t *tag) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[2 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_GET_SYSTEM_INFO;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;

  uint8_t rx[64] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 2) {
    if (len > 0)
      nfc_log_hex("SYSINFO partial:", rx, (size_t)len);
    return HB_NFC_ERR_NO_CARD;
  }

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "SYSINFO error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 2 + ISO15693_UID_LEN)
    return HB_NFC_ERR_PROTOCOL;

  uint8_t info_flags = rx[1];
  size_t required = 2 + ISO15693_UID_LEN;
  if (info_flags & ISO15693_SYSINFO_FLAG_DSFID)
    required++;
  if (info_flags & ISO15693_SYSINFO_FLAG_AFI)
    required++;
  if (info_flags & ISO15693_SYSINFO_FLAG_MEM)
    required += 2;
  if (info_flags & ISO15693_SYSINFO_FLAG_IC)
    required++;
  if ((size_t)plen != required || memcmp(tag->uid, &rx[2], ISO15693_UID_LEN) != 0)
    return HB_NFC_ERR_PROTOCOL;
  pos = 2;

  memcpy(tag->uid, &rx[pos], ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;

  if (info_flags & ISO15693_SYSINFO_FLAG_DSFID) {
    tag->dsfid = rx[pos++];
  }
  if (info_flags & ISO15693_SYSINFO_FLAG_AFI) {
    tag->afi = rx[pos++];
  }
  if (info_flags & ISO15693_SYSINFO_FLAG_MEM) {
    tag->block_count = (uint16_t)rx[pos++] + 1U;
    tag->block_size = (uint8_t)((rx[pos++] & ISO15693_BLOCK_SIZE_MASK) + 1U);
    tag->info_valid = true;
  }
  if (info_flags & ISO15693_SYSINFO_FLAG_IC) {
    tag->ic_ref = rx[pos++];
  }

  ESP_LOGI(TAG,
           "SYSINFO: blocks=%u size=%u dsfid=0x%02X afi=0x%02X",
           tag->block_count,
           tag->block_size,
           tag->dsfid,
           tag->afi);
  return HB_NFC_OK;
}

int iso15693_inventory_all(iso15693_tag_t *out, size_t max_tags) {
  if (out == NULL || max_tags == 0)
    return 0;

  int count = 0;
  for (;;) {
    if (count >= (int)max_tags)
      break;

    iso15693_tag_t tag;
    hb_nfc_err_t err = iso15693_inventory(&tag);
    if (err != HB_NFC_OK)
      break;

    bool dup = false;
    for (int i = 0; i < count; i++) {
      if (memcmp(out[i].uid, tag.uid, ISO15693_UID_LEN) == 0) {
        dup = true;
        break;
      }
    }
    if (!dup) {
      out[count++] = tag;
    }

    (void)iso15693_stay_quiet(&tag);
  }

  return count;
}

hb_nfc_err_t iso15693_read_single_block(
    const iso15693_tag_t *tag, uint8_t block, uint8_t *data, size_t data_max, size_t *data_len) {
  if (tag == NULL || data == NULL)
    return HB_NFC_ERR_PARAM;
  uint8_t blk_size = (tag->block_size != 0) ? tag->block_size : ISO15693_DEFAULT_BLOCK_SIZE;
  if (data_max < blk_size)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[3 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_READ_SINGLE_BLOCK;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;
  cmd[pos++] = block;

  uint8_t rx[64] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 2)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "READ_SINGLE error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }

  if (plen < 1 + (int)blk_size)
    return HB_NFC_ERR_PROTOCOL;
  memcpy(data, &rx[1], blk_size);
  if (data_len != NULL)
    *data_len = blk_size;
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_write_single_block(const iso15693_tag_t *tag,
                                         uint8_t block,
                                         const uint8_t *data,
                                         size_t data_len) {
  if (tag == NULL || data == NULL)
    return HB_NFC_ERR_PARAM;
  uint8_t blk_size = (tag->block_size != 0) ? tag->block_size : ISO15693_DEFAULT_BLOCK_SIZE;
  if (data_len != blk_size)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[3 + ISO15693_UID_LEN + ISO15693_MAX_BLOCK_SIZE];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_WRITE_SINGLE_BLOCK;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;
  cmd[pos++] = block;
  memcpy(&cmd[pos], data, blk_size);
  pos += blk_size;

  uint8_t rx[8] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 1)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "WRITE_SINGLE error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1)
    return HB_NFC_ERR_PROTOCOL;
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_lock_block(const iso15693_tag_t *tag, uint8_t block) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[3 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_LOCK_BLOCK;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;
  cmd[pos++] = block;

  uint8_t rx[8] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 1)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "LOCK_BLOCK error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1)
    return HB_NFC_ERR_PROTOCOL;
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_read_multiple_blocks(const iso15693_tag_t *tag,
                                           uint8_t first_block,
                                           uint8_t count,
                                           uint8_t *out_buf,
                                           size_t out_max,
                                           size_t *out_len) {
  if (tag == NULL || out_buf == NULL || count == 0)
    return HB_NFC_ERR_PARAM;
  if (tag->block_size == 0)
    return HB_NFC_ERR_PARAM;

  size_t total = (size_t)count * tag->block_size;
  if (out_max < total)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[4 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_READ_MULTIPLE_BLOCKS;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;
  cmd[pos++] = first_block;
  cmd[pos++] = (uint8_t)(count - 1U);

  size_t rx_cap = 1 + total + 2;
  uint8_t *rx = (uint8_t *)malloc(rx_cap);
  if (rx == NULL)
    return HB_NFC_ERR_INTERNAL;
  memset(rx, 0, rx_cap);

  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, rx_cap, 1, ISO15693_TIMEOUT_MULTI_MS);
  if (len < 2) {
    free(rx);
    return HB_NFC_ERR_NO_CARD;
  }

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "READ_MULTI error: 0x%02X", rx[1]);
    free(rx);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1 + (int)total) {
    free(rx);
    return HB_NFC_ERR_PROTOCOL;
  }

  memcpy(out_buf, &rx[1], total);
  if (out_len != NULL)
    *out_len = total;
  free(rx);
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_write_afi(const iso15693_tag_t *tag, uint8_t afi) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[3 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_WRITE_AFI;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;
  cmd[pos++] = afi;

  uint8_t rx[8] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 1)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "WRITE_AFI error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1)
    return HB_NFC_ERR_PROTOCOL;
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_lock_afi(const iso15693_tag_t *tag) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[2 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_LOCK_AFI;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;

  uint8_t rx[8] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 1)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "LOCK_AFI error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1)
    return HB_NFC_ERR_PROTOCOL;
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_write_dsfid(const iso15693_tag_t *tag, uint8_t dsfid) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[3 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_WRITE_DSFID;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;
  cmd[pos++] = dsfid;

  uint8_t rx[8] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 1)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "WRITE_DSFID error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1)
    return HB_NFC_ERR_PROTOCOL;
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_lock_dsfid(const iso15693_tag_t *tag) {
  if (tag == NULL)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[2 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_LOCK_DSFID;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;

  uint8_t rx[8] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 1)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "LOCK_DSFID error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1)
    return HB_NFC_ERR_PROTOCOL;
  return HB_NFC_OK;
}

hb_nfc_err_t iso15693_get_multi_block_sec(const iso15693_tag_t *tag,
                                          uint8_t first_block,
                                          uint8_t count,
                                          uint8_t *out_buf,
                                          size_t out_max,
                                          size_t *out_len) {
  if (tag == NULL || out_buf == NULL || count == 0)
    return HB_NFC_ERR_PARAM;
  if (out_max < count)
    return HB_NFC_ERR_PARAM;

  uint8_t cmd[4 + ISO15693_UID_LEN];
  int pos = 0;
  cmd[pos++] = ISO15693_FLAGS_ADDRESSED;
  cmd[pos++] = ISO15693_CMD_GET_MULTI_BLOCK_SEC;
  memcpy(&cmd[pos], tag->uid, ISO15693_UID_LEN);
  pos += ISO15693_UID_LEN;
  cmd[pos++] = first_block;
  cmd[pos++] = (uint8_t)(count - 1U);

  uint8_t rx[64] = {0};
  int len = nfc_poller_transceive(cmd, (size_t)pos, true, rx, sizeof(rx), 1, ISO15693_TIMEOUT_MS);
  if (len < 2)
    return HB_NFC_ERR_NO_CARD;

  int plen = strip_crc_len(rx, len);
  if (rx[0] & ISO15693_RESP_ERROR) {
    ESP_LOGW(TAG, "GET_MULTI_BLOCK_SEC error: 0x%02X", rx[1]);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (plen < 1 + count)
    return HB_NFC_ERR_PROTOCOL;

  memcpy(out_buf, &rx[1], count);
  if (out_len != NULL)
    *out_len = count;
  return HB_NFC_OK;
}

void iso15693_dump_card(void) {
  iso15693_tag_t tag = {0};
  hb_nfc_err_t err = iso15693_inventory(&tag);
  if (err != HB_NFC_OK) {
    ESP_LOGW(TAG, "No ISO15693 tag found");
    return;
  }

  err = iso15693_get_system_info(&tag);
  if (err != HB_NFC_OK) {
    ESP_LOGW(TAG, "SYSINFO failed, using default block size");
    tag.block_size = ISO15693_DEFAULT_BLOCK_SIZE;
    tag.block_count = ISO15693_DEFAULT_BLOCK_COUNT;
  }

  uint8_t blk[ISO15693_MAX_BLOCK_SIZE];
  for (uint16_t b = 0; b < tag.block_count; b++) {
    size_t got = 0;
    err = iso15693_read_single_block(&tag, (uint8_t)b, blk, sizeof(blk), &got);
    if (err != HB_NFC_OK) {
      ESP_LOGW(TAG, "READ block %u failed", (unsigned)b);
      break;
    }
    uint8_t b0 = (got > 0) ? blk[0] : 0;
    uint8_t b1 = (got > 1) ? blk[1] : 0;
    uint8_t b2 = (got > 2) ? blk[2] : 0;
    uint8_t b3 = (got > 3) ? blk[3] : 0;
    uint8_t b4 = (got > 4) ? blk[4] : 0;
    uint8_t b5 = (got > 5) ? blk[5] : 0;
    uint8_t b6 = (got > 6) ? blk[6] : 0;
    uint8_t b7 = (got > 7) ? blk[7] : 0;
    ESP_LOGI(TAG,
             "BLK %03u: %02X %02X %02X %02X %02X %02X %02X %02X",
             (unsigned)b,
             b0,
             b1,
             b2,
             b3,
             b4,
             b5,
             b6,
             b7);
  }
}

uint16_t iso15693_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = ISO15693_CRC_INIT;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < ISO15693_BITS_PER_BYTE; b++) {
      if (crc & 0x0001U)
        crc = (crc >> 1) ^ ISO15693_CRC_POLY;
      else
        crc >>= 1;
    }
  }
  return (uint16_t)~crc;
}

bool iso15693_check_crc(const uint8_t *data, size_t len) {
  if (data == NULL || len < ISO15693_CRC_LEN)
    return false;
  uint16_t calc = iso15693_crc16(data, len - ISO15693_CRC_LEN);
  uint16_t rx = (uint16_t)data[len - 2] | ((uint16_t)data[len - 1] << 8);
  return (calc == rx);
}
