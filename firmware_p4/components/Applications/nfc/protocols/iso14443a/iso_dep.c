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
 * @file iso_dep.c
 * @brief ISO-DEP - basic RATS + I-Block exchange with chaining/WTX (best effort).
 */
#include "iso_dep.h"

#include <string.h>

#include "esp_log.h"

#include "nfc_poller.h"
#include "iso14443a.h"

static const char *TAG = "iso_dep";

/* The PCD block number belongs to the active ISO-DEP session and must
 * alternate between APDUs.
 * Keeping it local to iso_dep_transceive() made the
 * first SELECT work while the following
 * EMV/NDEF command was rejected. */
static uint8_t s_pcd_block_num = 0;

/* ISO-DEP PCB helpers (best-effort values) */
#define ISO_DEP_PCB_I_BLOCK 0x02U
#define ISO_DEP_PCB_I_CHAIN 0x10U
#define ISO_DEP_PCB_R_ACK   0xA2U
#define ISO_DEP_PCB_R_NAK   0xB2U
#define ISO_DEP_PCB_S_WTX   0xF2U

/* ISO-DEP timing and protocol constants */
#define ISO_DEP_FWT_BASE_US          302U
#define ISO_DEP_FWT_MIN_MS           5
#define ISO_DEP_FSC_FSCI_MASK        0x0FU
#define ISO_DEP_FSC_FSCI_MAX         8
#define ISO_DEP_FSC_MAX_SIZE         256
#define ISO_DEP_ATS_PARSE_START      2
#define ISO_DEP_ATS_TA_MASK          0x10U
#define ISO_DEP_ATS_TB_MASK          0x20U
#define ISO_DEP_ATS_TB_FWI_SHIFT     4
#define ISO_DEP_ATS_TB_FWI_MASK      0x0FU
#define ISO_DEP_ATS_TC_MASK          0x40U
#define ISO_DEP_PCB_CLASS_MASK       0xC0U
#define ISO_DEP_PCB_I_CLASS          0x00U
#define ISO_DEP_PCB_R_CLASS          0x80U
#define ISO_DEP_PCB_S_CLASS          0xC0U
#define ISO_DEP_PCB_WTX_INF_MASK     0x0FU
#define ISO_DEP_PCB_WTX_CTRL         0x02U
#define ISO_DEP_CRC_LEN              2
#define ISO_DEP_MIN_ATS_LEN          3
#define ISO_DEP_RATS_CMD             0xE0U
#define ISO_DEP_RATS_LEN             2
#define ISO_DEP_RATS_FSDI_SHIFT      4
#define ISO_DEP_RATS_CID_MASK        0x0FU
#define ISO_DEP_PPS_CMD_BASE         0xD0U
#define ISO_DEP_PPS_LEN              3
#define ISO_DEP_PPS0_PPS1_PRESENT    0x11U
#define ISO_DEP_DSI_MASK             0x03U
#define ISO_DEP_DRI_MASK             0x03U
#define ISO_DEP_DSI_SHIFT            2
#define ISO_DEP_RATS_TIMEOUT_MS      30
#define ISO_DEP_PPS_TIMEOUT_MS       20
#define ISO_DEP_DEFAULT_FSC          32
#define ISO_DEP_DEFAULT_FWI          4
#define ISO_DEP_TRANSCEIVE_MIN_LEN   1
#define ISO_DEP_RX_BUF_SIZE          64
#define ISO_DEP_PPS_RX_BUF_SIZE      4
#define ISO_DEP_DIV_ROUNDING         999U
#define ISO_DEP_MS_DIVISOR           1000U
#define ISO_DEP_TRANSCEIVE_BUF_SIZE  260
#define ISO_DEP_FRAME_HDR_SIZE       1
#define ISO_DEP_CHAIN_MAX_DATA       256
#define ISO_DEP_BLOCK_NUM_MASK       0x01U
#define ISO_DEP_WTX_MIN_MULT         1U
#define ISO_DEP_SW1_WRONG_LEN        0x6CU
#define ISO_DEP_SW1_MORE_DATA        0x61U
#define ISO_DEP_SW_LEN               2
#define ISO_DEP_GET_RESPONSE_INS     0xC0U
#define ISO_DEP_GET_RESPONSE_CMD_LEN 5
#define ISO_DEP_GET_RESPONSE_RETRIES 8
#define ISO_DEP_APDU_MIN_LEN         5
#define ISO_DEP_INFO_FIELD_OFFSET    1
#define ISO_DEP_LINK_RETRIES         2

static size_t iso_dep_fsc_from_fsci(uint8_t fsci);
static int iso_dep_fwt_ms(uint8_t fwi);
static void iso_dep_parse_ats(nfc_iso_dep_data_t *dep);
static bool iso_dep_is_i_block(uint8_t pcb);
static bool iso_dep_is_r_block(uint8_t pcb);
static bool iso_dep_is_s_block(uint8_t pcb);
static bool iso_dep_is_wtx(uint8_t pcb);
static int iso_dep_strip_crc(uint8_t *buf, int len);
static int
iso_dep_request_retransmit(uint8_t block_num, uint8_t *rx, size_t rx_max, int timeout_ms);

static size_t iso_dep_fsc_from_fsci(uint8_t fsci) {
  static const uint16_t tbl[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
  if (fsci > ISO_DEP_FSC_FSCI_MAX)
    fsci = ISO_DEP_FSC_FSCI_MAX;
  return tbl[fsci];
}

static int iso_dep_fwt_ms(uint8_t fwi) {
  uint32_t us = ISO_DEP_FWT_BASE_US * (1U << (fwi & ISO_DEP_FSC_FSCI_MASK));
  int ms = (int)((us + ISO_DEP_DIV_ROUNDING) / ISO_DEP_MS_DIVISOR);
  if (ms < ISO_DEP_FWT_MIN_MS)
    ms = ISO_DEP_FWT_MIN_MS;
  return ms;
}

static void iso_dep_parse_ats(nfc_iso_dep_data_t *dep) {
  if (dep == NULL || dep->ats_len < ISO_DEP_SW_LEN)
    return;

  uint8_t t0 = dep->ats[1];
  uint8_t fsci = (uint8_t)(t0 & ISO_DEP_FSC_FSCI_MASK);
  size_t fsc = iso_dep_fsc_from_fsci(fsci);
  if (fsc > (ISO_DEP_FSC_MAX_SIZE - 1U))
    fsc = (ISO_DEP_FSC_MAX_SIZE - 1U);
  dep->fsc = (uint8_t)fsc;

  int pos = ISO_DEP_ATS_PARSE_START;

  if (t0 & ISO_DEP_ATS_TA_MASK)
    pos++;
  if (t0 & ISO_DEP_ATS_TB_MASK) {
    if ((size_t)pos < dep->ats_len) {
      uint8_t tb = dep->ats[pos];
      dep->fwi = (uint8_t)((tb >> ISO_DEP_ATS_TB_FWI_SHIFT) & ISO_DEP_ATS_TB_FWI_MASK);
    }
    pos++;
  }
  /* TC present */
  if (t0 & ISO_DEP_ATS_TC_MASK)
    pos++;

  ESP_LOGI(TAG,
           "ATS parsed: FSCI=%u FSC=%u FWI=%u",
           (unsigned)fsci,
           (unsigned)dep->fsc,
           (unsigned)dep->fwi);
}

static bool iso_dep_is_i_block(uint8_t pcb) {
  return (pcb & ISO_DEP_PCB_CLASS_MASK) == ISO_DEP_PCB_I_CLASS;
}
static bool iso_dep_is_r_block(uint8_t pcb) {
  return (pcb & ISO_DEP_PCB_CLASS_MASK) == ISO_DEP_PCB_R_CLASS;
}
static bool iso_dep_is_s_block(uint8_t pcb) {
  return (pcb & ISO_DEP_PCB_CLASS_MASK) == ISO_DEP_PCB_S_CLASS;
}
static bool iso_dep_is_wtx(uint8_t pcb) {
  return iso_dep_is_s_block(pcb) && ((pcb & ISO_DEP_PCB_WTX_INF_MASK) == ISO_DEP_PCB_WTX_CTRL);
}

static int iso_dep_strip_crc(uint8_t *buf, int len) {
  if (len >= ISO_DEP_MIN_ATS_LEN && iso14443a_check_crc(buf, (size_t)len))
    return len - ISO_DEP_CRC_LEN;
  return len;
}

static int
iso_dep_request_retransmit(uint8_t block_num, uint8_t *rx, size_t rx_max, int timeout_ms) {
  uint8_t rnak[1] = {(uint8_t)(ISO_DEP_PCB_R_NAK | (block_num & ISO_DEP_BLOCK_NUM_MASK))};

  for (int attempt = 1; attempt <= ISO_DEP_LINK_RETRIES; attempt++) {
    ESP_LOGW(TAG,
             "ISO-DEP RX recovery: R-NAK PCB=0x%02X attempt=%d/%d",
             rnak[0],
             attempt,
             ISO_DEP_LINK_RETRIES);
    int len = nfc_poller_transceive(
        rnak, sizeof(rnak), true, rx, rx_max, ISO_DEP_TRANSCEIVE_MIN_LEN, timeout_ms);
    if (len > 0) {
      return iso_dep_strip_crc(rx, len);
    }
  }
  return 0;
}

hb_nfc_err_t iso_dep_rats(uint8_t fsdi, uint8_t cid, nfc_iso_dep_data_t *dep) {
  s_pcd_block_num = 0;
  uint8_t cmd[ISO_DEP_RATS_LEN] = {
      ISO_DEP_RATS_CMD,
      (uint8_t)((fsdi << ISO_DEP_RATS_FSDI_SHIFT) | (cid & ISO_DEP_RATS_CID_MASK))};
  uint8_t rx[ISO_DEP_RX_BUF_SIZE] = {0};
  int len = nfc_poller_transceive(cmd,
                                  ISO_DEP_RATS_LEN,
                                  true,
                                  rx,
                                  sizeof(rx),
                                  ISO_DEP_TRANSCEIVE_MIN_LEN,
                                  ISO_DEP_RATS_TIMEOUT_MS);
  if (len < 1) {
    ESP_LOGW(TAG, "RATS failed");
    return HB_NFC_ERR_PROTOCOL;
  }
  if (len >= ISO_DEP_MIN_ATS_LEN && iso14443a_check_crc(rx, (size_t)len)) {
    len -= ISO_DEP_CRC_LEN;
  }
  if (dep != NULL) {
    dep->ats_len = (size_t)len;
    for (int i = 0; i < len && i < NFC_ATS_MAX_LEN; i++) {
      dep->ats[i] = rx[i];
    }
    dep->fsc = ISO_DEP_DEFAULT_FSC;
    dep->fwi = ISO_DEP_DEFAULT_FWI;
    iso_dep_parse_ats(dep);
  }
  return HB_NFC_OK;
}

hb_nfc_err_t iso_dep_pps(uint8_t cid, uint8_t dsi, uint8_t dri) {
  if ((dsi == 0) && (dri == 0))
    return HB_NFC_OK;
  uint8_t cmd[ISO_DEP_PPS_LEN] = {
      (uint8_t)(ISO_DEP_PPS_CMD_BASE | (cid & ISO_DEP_RATS_CID_MASK)),
      ISO_DEP_PPS0_PPS1_PRESENT, /* PPS0: PPS1 present */
      (uint8_t)(((dsi & ISO_DEP_DSI_MASK) << ISO_DEP_DSI_SHIFT) | (dri & ISO_DEP_DRI_MASK))};
  uint8_t rx[ISO_DEP_PPS_RX_BUF_SIZE] = {0};
  int len = nfc_poller_transceive(
      cmd, sizeof(cmd), true, rx, sizeof(rx), ISO_DEP_TRANSCEIVE_MIN_LEN, ISO_DEP_PPS_TIMEOUT_MS);
  if (len < 1) {
    ESP_LOGW(TAG, "PPS failed");
    return HB_NFC_ERR_PROTOCOL;
  }
  return HB_NFC_OK;
}

int iso_dep_transceive(const nfc_iso_dep_data_t *dep,
                       const uint8_t *tx,
                       size_t tx_len,
                       uint8_t *rx,
                       size_t rx_max,
                       int timeout_ms) {
  if (!tx || !rx || tx_len == 0)
    return 0;

  size_t fsc = (dep && dep->fsc) ? dep->fsc : ISO_DEP_DEFAULT_FSC;
  size_t max_inf = (fsc > 1) ? (fsc - 1U) : tx_len;
  int base_timeout =
      (timeout_ms > 0) ? timeout_ms : iso_dep_fwt_ms(dep ? dep->fwi : ISO_DEP_DEFAULT_FWI);

  uint8_t rbuf[ISO_DEP_TRANSCEIVE_BUF_SIZE] = {0};
  int rlen = 0;
  uint8_t block_num = s_pcd_block_num;

  size_t off = 0;
  while (off < tx_len) {
    size_t chunk = tx_len - off;
    bool chaining = chunk > max_inf;
    if (chaining)
      chunk = max_inf;

    uint8_t frame[ISO_DEP_FRAME_HDR_SIZE + ISO_DEP_CHAIN_MAX_DATA];
    frame[0] = (uint8_t)(ISO_DEP_PCB_I_BLOCK | (block_num & ISO_DEP_BLOCK_NUM_MASK));
    if (chaining)
      frame[0] |= ISO_DEP_PCB_I_CHAIN;
    memcpy(&frame[ISO_DEP_FRAME_HDR_SIZE], &tx[off], chunk);

    rlen = nfc_poller_transceive(frame,
                                 ISO_DEP_FRAME_HDR_SIZE + chunk,
                                 true,
                                 rbuf,
                                 sizeof(rbuf),
                                 ISO_DEP_TRANSCEIVE_MIN_LEN,
                                 base_timeout);
    if (rlen <= 0) {
      rlen = iso_dep_request_retransmit(block_num, rbuf, sizeof(rbuf), base_timeout);
      if (rlen <= 0)
        return 0;
    } else {
      rlen = iso_dep_strip_crc(rbuf, rlen);
    }

    if (chaining) {
      if (!iso_dep_is_r_block(rbuf[0])) {
        ESP_LOGW(TAG, "Expected R-block, got 0x%02X", rbuf[0]);
        return 0;
      }
      off += chunk;
      block_num ^= ISO_DEP_BLOCK_NUM_MASK;
      continue;
    }

    off += chunk;
    break;
  }

  int out_len = 0;
  while (1) {
    uint8_t pcb = rbuf[0];

    if (iso_dep_is_wtx(pcb)) {
      uint8_t wtxm = (rlen >= 2) ? rbuf[1] : ISO_DEP_WTX_MIN_MULT;
      uint8_t wtx_resp[2] = {ISO_DEP_PCB_S_WTX, wtxm};
      int tmo = base_timeout * (wtxm != 0U ? wtxm : ISO_DEP_WTX_MIN_MULT);
      rlen = nfc_poller_transceive(
          wtx_resp, sizeof(wtx_resp), true, rbuf, sizeof(rbuf), ISO_DEP_TRANSCEIVE_MIN_LEN, tmo);
      if (rlen <= 0)
        return 0;
      rlen = iso_dep_strip_crc(rbuf, rlen);
      continue;
    }

    if (iso_dep_is_i_block(pcb)) {
      bool chaining = (pcb & ISO_DEP_PCB_I_CHAIN) != 0U;
      size_t inf_len =
          (rlen > ISO_DEP_INFO_FIELD_OFFSET) ? (size_t)(rlen - ISO_DEP_INFO_FIELD_OFFSET) : 0;
      if ((size_t)out_len + inf_len > rx_max) {
        inf_len = (rx_max > (size_t)out_len) ? (rx_max - (size_t)out_len) : 0;
      }
      if (inf_len > 0)
        memcpy(&rx[out_len], &rbuf[ISO_DEP_INFO_FIELD_OFFSET], inf_len);
      out_len += (int)inf_len;

      if (!chaining) {
        s_pcd_block_num = block_num ^ ISO_DEP_BLOCK_NUM_MASK;
        return out_len;
      }

      uint8_t rack[1] = {(uint8_t)(ISO_DEP_PCB_R_ACK | (pcb & ISO_DEP_BLOCK_NUM_MASK))};
      rlen = nfc_poller_transceive(
          rack, sizeof(rack), true, rbuf, sizeof(rbuf), ISO_DEP_TRANSCEIVE_MIN_LEN, base_timeout);
      if (rlen <= 0)
        return out_len ? out_len : 0;
      rlen = iso_dep_strip_crc(rbuf, rlen);
      continue;
    }

    if (iso_dep_is_r_block(pcb)) {
      ESP_LOGW(TAG, "Unexpected R-block 0x%02X", pcb);
      return out_len;
    }

    ESP_LOGW(TAG, "Unknown PCB 0x%02X", pcb);
    return out_len;
  }
}

int iso_dep_apdu_transceive(const nfc_iso_dep_data_t *dep,
                            const uint8_t *apdu,
                            size_t apdu_len,
                            uint8_t *rx,
                            size_t rx_max,
                            int timeout_ms) {
  if (!apdu || !rx || apdu_len == 0)
    return 0;

  uint8_t cmd_buf[ISO_DEP_TRANSCEIVE_BUF_SIZE];
  if (apdu_len > sizeof(cmd_buf))
    return 0;
  memcpy(cmd_buf, apdu, apdu_len);

  int len = iso_dep_transceive(dep, cmd_buf, apdu_len, rx, rx_max, timeout_ms);
  if (len < ISO_DEP_SW_LEN)
    return len;

  uint8_t sw1 = rx[len - 2];
  uint8_t sw2 = rx[len - 1];

  if (sw1 == ISO_DEP_SW1_WRONG_LEN && apdu_len >= ISO_DEP_APDU_MIN_LEN) {
    cmd_buf[apdu_len - 1] = sw2;
    len = iso_dep_transceive(dep, cmd_buf, apdu_len, rx, rx_max, timeout_ms);
    return len;
  }

  if (sw1 == ISO_DEP_SW1_MORE_DATA) {
    size_t out_pos = (size_t)(len - ISO_DEP_SW_LEN);
    uint8_t le = (sw2 != 0U) ? sw2 : 0x00U;

    for (int i = 0; i < ISO_DEP_GET_RESPONSE_RETRIES; i++) {
      uint8_t get_resp[ISO_DEP_GET_RESPONSE_CMD_LEN] = {
          0x00, ISO_DEP_GET_RESPONSE_INS, 0x00, 0x00, le};
      uint8_t tmp[ISO_DEP_TRANSCEIVE_BUF_SIZE] = {0};
      int rlen = iso_dep_transceive(dep, get_resp, sizeof(get_resp), tmp, sizeof(tmp), timeout_ms);
      if (rlen < ISO_DEP_SW_LEN)
        return (int)out_pos;

      uint8_t rsw1 = tmp[rlen - 2];
      uint8_t rsw2 = tmp[rlen - 1];
      int data_len = rlen - ISO_DEP_SW_LEN;
      if (data_len > 0) {
        size_t copy = (out_pos + (size_t)data_len <= rx_max)
                          ? (size_t)data_len
                          : (rx_max > out_pos ? (rx_max - out_pos) : 0);
        if (copy > 0) {
          memcpy(&rx[out_pos], tmp, copy);
          out_pos += copy;
        }
      }

      sw1 = rsw1;
      sw2 = rsw2;
      if (sw1 != ISO_DEP_SW1_MORE_DATA) {
        break;
      }
      le = (sw2 != 0U) ? sw2 : 0x00U;
    }

    if (out_pos + ISO_DEP_SW_LEN <= rx_max) {
      rx[out_pos++] = sw1;
      rx[out_pos++] = sw2;
    }
    return (int)out_pos;
  }

  return len;
}
