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
 * @file t4t_emu.c
 * @brief ISO14443A Type 4 Tag emulation over ST25R3916 passive target mode.
 */
#include "t4t_emu.h"
#include "nfc_t4t.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"
#include "mf_desfire_emu.h"
#include "iso14443a.h"

static const char *TAG = "NFC_T4T_EMU";

#define OP_CTRL_TARGET             0xC3U
#define T4T_RX_IRQ                 (ST25R3916_IRQ_MAIN_RXE)
#define T4T_MAX_RESP_SIZE          260
#define T4T_CHAIN_BUF_SIZE         300
#define T4T_PT_MEM_SIZE            15
#define T4T_OSC_AUX_MASK           0x14U
#define T4T_OSC_MAIN_MASK          0x80U
#define T4T_OSC_TGT_MASK           0x08U
#define T4T_CC_V1                  0x00U
#define T4T_CC_MLEU_V1             0x0FU
#define T4T_CC_T                   0x20U
#define T4T_CC_NDEF_CTL            0x04U
#define T4T_CC_NDEF_RW             0x06U
#define T4T_NDEF_RECORD_HDR        0xD1U
#define T4T_NDEF_TNF_TYPE          0x01U
#define T4T_NDEF_TEXT_CODE         0x02U
#define T4T_NDEF_TEXT_PL_LEN       1
#define T4T_NDEF_LANG_LEN          2
#define T4T_PT_MEM_UID_OFFSET      0
#define T4T_PT_MEM_ATQA_OFFSET     10
#define T4T_PT_MEM_SAK_OFFSET      13
#define T4T_PCB_I_BLOCK            0x02U
#define T4T_PCB_CID_MASK           0x08U
#define T4T_PCB_TOGGLE             0x01U
#define T4T_PCB_R_ACK              0xA2U
#define T4T_PCB_R_NAK_MASK         0x10U
#define T4T_FRAME_OVERHEAD         3U
#define T4T_FRAME_MAX              256U
#define T4T_FSDI_SHIFT             4U
#define T4T_CID_INVALID            15U
#define T4T_CRC_SIZE               2U
#define T4T_TX_POLL_US             10U
#define T4T_TX_POLL_COUNT          2000U
#define T4T_TX_BYTE_SHIFT          5U
#define T4T_TX_BIT_SHIFT           3U
#define T4T_FIELD_OFF_IRQ          0x08U
#define T4T_PCB_DESELECT           0xC2U
#define T4T_DELAY_5MS              5
#define T4T_DELAY_10MS             10
#define T4T_DELAY_2MS              2
#define T4T_REG_AUX_DEF            0x10U
#define T4T_REG_IO_CONF2           0x80U
#define T4T_REG_PT_MOD             0x60U
#define T4T_IC_ID_INVALID          0x00U
#define T4T_IC_ID_FF               0xFFU
#define T4T_OSC_TIMEOUT            200
#define T4T_APDU_INS_SELECT        0xA4U
#define T4T_APDU_INS_READ          0xB0U
#define T4T_APDU_INS_UPDATE        0xD6U
#define T4T_APDU_P1_SELECT_AID     0x04U
#define T4T_APDU_P1_SELECT_FILE    0x00U
#define T4T_APDU_LC_AID            7
#define T4T_APDU_LC_FID            2
#define T4T_APDU_MIN_LEN           4
#define T4T_APDU_HDR_LEN           5
#define T4T_APDU_SW_ERR_SYNTAX     0x6700U
#define T4T_APDU_SW_OK             0x9000U
#define T4T_APDU_SW_FILE_NOT_FOUND 0x6A82U
#define T4T_APDU_SW_SEC_ERROR      0x6985U
#define T4T_APDU_SW_LE_INVALID     0x6C00U
#define T4T_APDU_SW_EOF            0x6B00U
#define T4T_APDU_LE_MAX            256
#define T4T_PTA_STATE_IDLE         0x01U
#define T4T_PTA_STATE_ACTIVE       0x05U
#define T4T_PTA_SENSE_TIMEOUT      500
#define T4T_PTA_ACTIVE_TIMEOUT     2000
#define T4T_PTA_IDLE_CHECK         500
#define T4T_PTA_SENSE_CHECK        200
#define T4T_PCB_CLASS_MASK         0xC0U
#define T4T_PCB_I_CLASS            0x00U
#define T4T_PCB_R_CLASS            0x80U
#define T4T_PCB_S_CLASS            0xC0U
#define T4T_PCB_CHAIN_MASK         0x10U
#define T4T_PCB_NAD_MASK           0x04U
#define T4T_FS1_LEN_MASK           0x7FU
#define T4T_ATS_LENGTH             3U
#define T4T_ATS_T0                 0x46U
#define T4T_ATS_TC1                0x02U
#define T4T_CMD_RATS               0xE0U
#define T4T_CMD_PPS_MASK           0xF0U
#define T4T_CMD_PPS_BASE           0xD0U
#define T4T_PCB_I_VALID_MASK       0xE6U
#define T4T_PCB_R_VALID_MASK       0xE6U
#define T4T_FIFO_INVALID_MASK      0x3FU
#define T4T_DELAY_1MS              1
#define T4T_PT_MEM_CT_OFFSET       12
#define T4T_PT_MEM_CT_BYTE         0x04U
#define T4T_OP_CTRL_OFF            0x00U
#define T4T_OP_CTRL_ON             0x80U
#define T4T_APDU_SW_WRONG_P1P2     0x6A86U
#define T4T_PTA_STATE_MASK         0x0FU
#define T4T_PTA_STATE_WUPA         0x02U
#define T4T_PTA_STATE_SELECTED     0x03U
#define T4T_PTA_STATE_HALTED       0x0DU

static uint8_t k_uid7[7] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
static uint8_t k_atqa[2] = {0x44, 0x00};
static uint8_t k_sak = 0x20;

static uint8_t k_ats[T4T_EMU_ATS_CAPACITY] = {T4T_ATS_LENGTH, T4T_ATS_T0, T4T_ATS_TC1};
static size_t s_ats_len = T4T_ATS_LENGTH;
static uint8_t s_uid_len = 7;
static bool s_has_rats;
static mfp_listener_t *s_plus;
static nfc_t4t_listener_t *s_ndef_listener;
static bool s_is_identity_only;
static bool s_transport_error;
static uint8_t s_pending_main;
static const uint8_t k_ndef_aid[] = {0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01};

#define T4T_CC_LEN   15
#define T4T_NDEF_MAX 128
#define T4T_FILE_MAX (2 + T4T_NDEF_MAX)
#define T4T_ML       0x0050U
#define T4T_NDEF_FID 0xE104U
#define T4T_CC_FID   0xE103U

typedef enum {
  T4T_STATE_SLEEP,
  T4T_STATE_SENSE,
  T4T_STATE_ACTIVE,
} t4t_state_t;

typedef enum {
  T4T_FILE_NONE = 0,
  T4T_FILE_CC,
  T4T_FILE_NDEF,
} t4t_file_t;

static bool wait_oscillator(int timeout_ms);
static hb_nfc_err_t load_pt_memory(void);
static void build_cc(void);
static void build_ndef_text(const char *text);
static void tx_with_crc(const uint8_t *data, int len);
static void send_i_block_resp(const uint8_t *inf, int inf_len, uint8_t pcb_in);
static void send_r_ack(uint8_t pcb_in);
static void
apdu_resp(uint8_t *out_buffer, int *out_len, const uint8_t *data, int data_len, uint16_t sw);
static uint16_t file_len(t4t_file_t f);
static uint8_t *file_ptr(t4t_file_t f);
static void handle_apdu(const uint8_t *apdu, int apdu_len, uint8_t pcb_in);

static t4t_state_t s_state = T4T_STATE_SLEEP;
static bool s_init_done = false;

static bool s_app_selected = false;
static t4t_file_t s_file = T4T_FILE_NONE;

static uint8_t s_cc[T4T_CC_LEN];
static uint8_t s_ndef_file[T4T_FILE_MAX];

static uint8_t s_last_resp[T4T_MAX_RESP_SIZE];
static int s_last_resp_len = 0;

static uint8_t s_chain_buf[T4T_CHAIN_BUF_SIZE];
static size_t s_chain_len = 0;
static bool s_chain_active = false;
static uint8_t s_pcd_cid = 0;
static uint8_t s_expected_block = 0;
static uint16_t s_frame_size = T4T_FRAME_MAX;
static uint8_t s_tx_data[T4T_MAX_RESP_SIZE];
static size_t s_tx_length;
static size_t s_tx_offset;
static uint8_t s_tx_pcb;
static TickType_t s_sense_tick = 0;
static TickType_t s_active_tick = 0;

static bool wait_oscillator(int timeout_ms) {
  for (int i = 0; i < timeout_ms; i++) {
    uint8_t aux = 0, mi = 0, ti = 0;
    hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
    hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &mi);
    hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &ti);
    if (((aux & T4T_OSC_AUX_MASK) != 0) || ((mi & T4T_OSC_MAIN_MASK) != 0) ||
        ((ti & T4T_OSC_TGT_MASK) != 0)) {
      ESP_LOGI(TAG, "Osc OK in %dms: AUX=0x%02X MAIN=0x%02X TGT=0x%02X", i, aux, mi, ti);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_1MS));
  }
  ESP_LOGW(TAG, "Oscillator did not become ready");
  return false;
}

static hb_nfc_err_t load_pt_memory(void) {
  uint8_t ptm[T4T_PT_MEM_SIZE] = {0};
  ptm[0] = k_uid7[0];
  ptm[1] = k_uid7[1];
  ptm[2] = k_uid7[2];
  ptm[3] = k_uid7[3];
  ptm[4] = k_uid7[4];
  ptm[5] = k_uid7[5];
  ptm[6] = k_uid7[6];
  ptm[7] = 0x00;
  ptm[8] = 0x00;
  ptm[9] = 0x00;
  ptm[T4T_PT_MEM_ATQA_OFFSET] = k_atqa[0];
  ptm[T4T_PT_MEM_ATQA_OFFSET + 1] = k_atqa[1];
  if (s_uid_len == T4T_EMU_UID_CAPACITY) {
    ptm[T4T_PT_MEM_CT_OFFSET] = T4T_PT_MEM_CT_BYTE;
    ptm[T4T_PT_MEM_SAK_OFFSET] = k_sak;
  } else {
    ptm[T4T_PT_MEM_CT_OFFSET] = k_sak;
  }
  ptm[14] = 0x00;

  hb_nfc_err_t err = hb_nfc_spi_pt_mem_write(ST25R3916_SPI_PT_MEM_A_WRITE, ptm, T4T_PT_MEM_SIZE);
  if (err != HB_NFC_OK)
    return err;
  vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_2MS));

  uint8_t rb[T4T_PT_MEM_SIZE] = {0};
  err = hb_nfc_spi_pt_mem_read(rb, T4T_PT_MEM_SIZE);
  if (err != HB_NFC_OK)
    return err;
  if (memcmp(ptm, rb, T4T_PT_MEM_SIZE) != 0) {
    ESP_LOGE(TAG, "PT Memory mismatch!");
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG,
           "PT OK: UID=%02X%02X%02X%02X%02X%02X%02X ATQA=%02X%02X SAK=%02X",
           ptm[0],
           ptm[1],
           ptm[2],
           ptm[3],
           ptm[4],
           ptm[5],
           ptm[6],
           ptm[10],
           ptm[11],
           ptm[13]);
  return HB_NFC_OK;
}

static void build_cc(void) {
  s_cc[0] = T4T_CC_V1;
  s_cc[1] = T4T_CC_MLEU_V1;
  s_cc[2] = T4T_CC_T;
  s_cc[3] = (uint8_t)((T4T_ML >> 8) & 0xFF);
  s_cc[4] = (uint8_t)(T4T_ML & 0xFF);
  s_cc[5] = (uint8_t)((T4T_ML >> 8) & 0xFF);
  s_cc[6] = (uint8_t)(T4T_ML & 0xFF);
  s_cc[7] = T4T_CC_NDEF_CTL;
  s_cc[8] = T4T_CC_NDEF_RW;
  s_cc[9] = (uint8_t)((T4T_NDEF_FID >> 8) & 0xFF);
  s_cc[10] = (uint8_t)(T4T_NDEF_FID & 0xFF);
  s_cc[11] = 0x00;
  s_cc[12] = (uint8_t)T4T_NDEF_MAX;
  s_cc[13] = 0x00;
  s_cc[14] = 0x00;
}

static void build_ndef_text(const char *text) {
  memset(s_ndef_file, 0x00, sizeof(s_ndef_file));

  if (text == NULL)
    text = "High Boy NFC T4T";
  size_t tl = strlen(text);
  size_t max_text = (T4T_NDEF_MAX > 7) ? (T4T_NDEF_MAX - 7) : 0;
  if (tl > max_text)
    tl = max_text;
  size_t pl = T4T_NDEF_TEXT_PL_LEN + T4T_NDEF_LANG_LEN + tl;

  size_t pos = 2;
  s_ndef_file[pos++] = T4T_NDEF_RECORD_HDR;
  s_ndef_file[pos++] = T4T_NDEF_TNF_TYPE;
  s_ndef_file[pos++] = (uint8_t)pl;
  s_ndef_file[pos++] = 'T';
  s_ndef_file[pos++] = T4T_NDEF_TEXT_CODE;
  s_ndef_file[pos++] = 'p';
  s_ndef_file[pos++] = 't';

  memcpy(&s_ndef_file[pos], text, tl);
  pos += tl;

  uint16_t nlen = (uint16_t)(pos - 2);
  s_ndef_file[0] = (uint8_t)((nlen >> 8) & 0xFF);
  s_ndef_file[1] = (uint8_t)(nlen & 0xFF);
}

static void reset_application(void) {
  if (s_ndef_listener)
    nfc_t4t_listener_reset(s_ndef_listener);
  mf_desfire_emu_reset();
  if (s_plus != NULL)
    mfp_listener_reset(s_plus);
}

static void reset_link(void) {
  s_has_rats = false;
  s_expected_block = 0;
  s_last_resp_len = 0;
  s_tx_length = 0;
  s_tx_offset = 0;
  s_chain_len = 0;
  s_chain_active = false;
  s_app_selected = false;
  s_file = T4T_FILE_NONE;
  s_pcd_cid = 0;
  reset_application();
}

hb_nfc_err_t t4t_emu_init_default(void) {
  s_ndef_listener = NULL;
  s_plus = NULL;
  s_is_identity_only = false;
  s_transport_error = false;
  s_pending_main = 0;
  const uint8_t uid[] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
  memcpy(k_uid7, uid, sizeof(uid));
  k_atqa[0] = 0x44;
  k_atqa[1] = 0;
  k_sak = 0x20;
  k_ats[0] = T4T_ATS_LENGTH;
  k_ats[1] = T4T_ATS_T0;
  k_ats[2] = T4T_ATS_TC1;
  s_ats_len = T4T_ATS_LENGTH;
  s_uid_len = T4T_EMU_UID_CAPACITY;
  s_has_rats = false;
  build_cc();
  build_ndef_text("High Boy NFC T4T");
  s_state = T4T_STATE_SLEEP;
  s_init_done = true;
  s_app_selected = false;
  s_file = T4T_FILE_NONE;
  s_chain_active = false;
  s_chain_len = 0;
  s_pcd_cid = 0;
  s_expected_block = 0;
  s_last_resp_len = 0;
  s_tx_length = 0;
  s_tx_offset = 0;
  s_sense_tick = 0;
  s_active_tick = 0;
  ESP_LOGI(TAG, "T4T init: NDEF max=%u", (unsigned)T4T_NDEF_MAX);
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_emu_init_identity(const t4t_emu_identity_t *identity) {
  if (!identity || (identity->uid_len != 4 && identity->uid_len != 7) || !identity->ats_len ||
      identity->ats_len > T4T_EMU_ATS_CAPACITY || identity->ats[0] != identity->ats_len ||
      !(identity->sak & 0x20))
    return HB_NFC_ERR_PARAM;
  hb_nfc_err_t error = t4t_emu_init_default();
  if (error != HB_NFC_OK)
    return error;
  memset(k_uid7, 0, sizeof(k_uid7));
  memcpy(k_uid7, identity->uid, identity->uid_len);
  s_uid_len = identity->uid_len;
  memcpy(k_atqa, identity->atqa, sizeof(k_atqa));
  k_sak = identity->sak;
  memcpy(k_ats, identity->ats, identity->ats_len);
  s_ats_len = identity->ats_len;
  s_is_identity_only = true;
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_emu_init_ndef(nfc_t4t_listener_t *listener) {
  if (!listener || !listener->card)
    return HB_NFC_ERR_PARAM;
  hb_nfc_err_t error = t4t_emu_init_identity(&listener->card->identity);
  if (error == HB_NFC_OK) {
    s_ndef_listener = listener;
    s_is_identity_only = false;
    nfc_t4t_listener_reset(listener);
  }
  return error;
}

hb_nfc_err_t t4t_emu_init_plus(const t4t_emu_identity_t *identity, mfp_listener_t *listener) {
  if (identity == NULL || listener == NULL || listener->config.key == NULL ||
      listener->config.read == NULL || (identity->uid_len != 4 && identity->uid_len != 7) ||
      identity->ats_len < 2 || identity->ats_len > T4T_EMU_ATS_CAPACITY ||
      identity->ats[0] != identity->ats_len || !(identity->sak & 0x20))
    return HB_NFC_ERR_PARAM;
  hb_nfc_err_t err = t4t_emu_init_default();
  if (err != HB_NFC_OK)
    return err;
  memset(k_uid7, 0, sizeof(k_uid7));
  memcpy(k_uid7, identity->uid, identity->uid_len);
  s_uid_len = identity->uid_len;
  memcpy(k_atqa, identity->atqa, sizeof(k_atqa));
  k_sak = identity->sak;
  memcpy(k_ats, identity->ats, identity->ats_len);
  s_ats_len = identity->ats_len;
  s_plus = listener;
  mfp_listener_reset(s_plus);
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_emu_configure_target(void) {
  if (!s_init_done)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, T4T_OP_CTRL_OFF) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_5MS));
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_SET_DEFAULT) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_10MS));

  uint8_t ic = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &ic) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (ic == T4T_IC_ID_INVALID || ic == T4T_IC_ID_FF)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_IO_CONF2, T4T_REG_IO_CONF2) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_2MS));

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, T4T_OP_CTRL_ON) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (!wait_oscillator(T4T_OSC_TIMEOUT))
    return HB_NFC_ERR_TIMEOUT;

  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_10MS));

  if (hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_TARGET_NFCA) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_AUX_DEF, T4T_REG_AUX_DEF) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_PASSIVE_TARGET, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_ACT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_DEACT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_PT_MOD, T4T_REG_PT_MOD) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  hb_nfc_err_t err = load_pt_memory();
  if (err != HB_NFC_OK)
    return err;

  {
    st25r3916_irq_status_t s;
    (void)st25r3916_irq_read(&s);
  }
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_MAIN_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TIMER_NFC_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_ERROR_WUP_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TARGET_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  ESP_LOGI(TAG, "T4T target configured");
  return HB_NFC_OK;
}

hb_nfc_err_t t4t_emu_start(void) {
  if (!s_init_done)
    return HB_NFC_ERR_INTERNAL;

  {
    st25r3916_irq_status_t s;
    (void)st25r3916_irq_read(&s);
  }

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, OP_CTRL_TARGET) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_5MS));

  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T4T_DELAY_2MS));

  reset_link();
  s_state = T4T_STATE_SENSE;
  ESP_LOGI(TAG, "T4T emulation active - present a reader");
  return HB_NFC_OK;
}

bool t4t_emu_has_error(void) {
  return s_transport_error;
}

void t4t_emu_stop(void) {
  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, T4T_OP_CTRL_OFF);
  s_state = T4T_STATE_SLEEP;
  s_init_done = false;
  reset_application();
  s_ndef_listener = NULL;
  s_plus = NULL;
  ESP_LOGI(TAG, "T4T emulation stopped");
}

static void tx_with_crc(const uint8_t *data, int len) {
  if (data == NULL || len <= 0)
    return;
  uint8_t main = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main) != HB_NFC_OK)
    goto failed;
  s_pending_main |= main & T4T_RX_IRQ;
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_CLEAR_FIFO) != HB_NFC_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES1, (uint8_t)(len >> T4T_TX_BYTE_SHIFT)) !=
          HB_NFC_OK ||
      hb_nfc_spi_reg_write(
          ST25R3916_REG_NUM_TX_BYTES2,
          (uint8_t)((len & ST25R3916_FIFO_TX_BYTES_LSB_MASK) << T4T_TX_BIT_SHIFT)) != HB_NFC_OK ||
      st25r3916_fifo_load(data, (size_t)len) != HB_NFC_OK ||
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WITH_CRC) != HB_NFC_OK)
    goto failed;
  for (unsigned i = 0; i < T4T_TX_POLL_COUNT; i++) {
    if (hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main) != HB_NFC_OK)
      goto failed;
    s_pending_main |= main & T4T_RX_IRQ;
    if (main & ST25R3916_IRQ_MAIN_TXE)
      return;
    hb_nfc_timer_delay_us(T4T_TX_POLL_US);
  }
  reset_link();
  s_pending_main = 0;
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE) != HB_NFC_OK)
    goto failed;
  s_state = T4T_STATE_SENSE;
  return;
failed:
  s_transport_error = true;
}

static void send_fragment(void) {
  size_t header = (s_tx_pcb & T4T_PCB_CID_MASK) ? 2U : 1U;
  size_t max_inf = s_frame_size - T4T_FRAME_OVERHEAD - (header - 1);
  size_t length = s_tx_length - s_tx_offset;
  if (length > max_inf)
    length = max_inf;
  s_last_resp[0] = s_tx_pcb;
  if (s_tx_offset + length < s_tx_length)
    s_last_resp[0] |= T4T_PCB_CHAIN_MASK;
  if (header == 2)
    s_last_resp[1] = s_pcd_cid;
  memcpy(s_last_resp + header, s_tx_data + s_tx_offset, length);
  s_last_resp_len = (int)(header + length);
  s_tx_offset += length;
  s_expected_block = (s_tx_pcb & T4T_PCB_TOGGLE) ^ T4T_PCB_TOGGLE;
  tx_with_crc(s_last_resp, s_last_resp_len);
}

static void send_i_block_resp(const uint8_t *inf, int inf_len, uint8_t pcb_in) {
  if (inf_len < 0 || (size_t)inf_len > sizeof(s_tx_data) || (inf == NULL && inf_len))
    return;
  if (inf_len)
    memcpy(s_tx_data, inf, (size_t)inf_len);
  s_tx_length = (size_t)inf_len;
  s_tx_offset = 0;
  s_tx_pcb = (uint8_t)(T4T_PCB_I_BLOCK | (pcb_in & (T4T_PCB_TOGGLE | T4T_PCB_CID_MASK)));
  send_fragment();
}

static void send_r_ack(uint8_t pcb_in) {
  uint8_t resp[2];
  int pos = 0;
  resp[pos++] = (uint8_t)(T4T_PCB_R_ACK | s_expected_block | (pcb_in & T4T_PCB_CID_MASK));
  if (pcb_in & T4T_PCB_CID_MASK) {
    resp[pos++] = s_pcd_cid;
  }
  memcpy(s_last_resp, resp, (size_t)pos);
  s_last_resp_len = pos;
  tx_with_crc(resp, pos);
}

static void
apdu_resp(uint8_t *out_buffer, int *out_len, const uint8_t *data, int data_len, uint16_t sw) {
  int pos = 0;
  if (data != NULL && data_len > 0) {
    memcpy(&out_buffer[pos], data, (size_t)data_len);
    pos += data_len;
  }
  out_buffer[pos++] = (uint8_t)((sw >> 8) & 0xFF);
  out_buffer[pos++] = (uint8_t)(sw & 0xFF);
  *out_len = pos;
}

static uint16_t file_len(t4t_file_t f) {
  if (f == T4T_FILE_CC)
    return T4T_CC_LEN;
  if (f == T4T_FILE_NDEF) {
    uint16_t nlen = (uint16_t)((s_ndef_file[0] << 8) | s_ndef_file[1]);
    if (nlen > T4T_NDEF_MAX)
      nlen = T4T_NDEF_MAX;
    return (uint16_t)(nlen + 2);
  }
  return 0;
}

static uint8_t *file_ptr(t4t_file_t f) {
  if (f == T4T_FILE_CC)
    return s_cc;
  if (f == T4T_FILE_NDEF)
    return s_ndef_file;
  return NULL;
}

static void handle_apdu(const uint8_t *apdu, int apdu_len, uint8_t pcb_in) {
  uint8_t resp[T4T_MAX_RESP_SIZE];
  int resp_len = 0;

  if (s_is_identity_only)
    return;
  if (s_ndef_listener != NULL) {
    nfc_t4t_request_t request = {.data = apdu, .length = apdu_len > 0 ? (size_t)apdu_len : 0};
    nfc_t4t_response_t response;
    if (nfc_t4t_exchange(s_ndef_listener, &request, &response) == ESP_OK)
      send_i_block_resp(response.data, (int)response.length, pcb_in);
    return;
  }
  if (s_plus != NULL) {
    size_t length = 0;
    if (apdu_len > 0 &&
        mfp_listener_exchange(s_plus, apdu, (size_t)apdu_len, resp, sizeof(resp), &length) ==
            HB_NFC_OK)
      send_i_block_resp(resp, (int)length, pcb_in);
    return;
  }
  if (mf_desfire_emu_handle_apdu(apdu, apdu_len, resp, &resp_len)) {
    send_i_block_resp(resp, resp_len, pcb_in);
    return;
  }

  if (apdu_len < T4T_APDU_MIN_LEN) {
    apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_ERR_SYNTAX);
    send_i_block_resp(resp, resp_len, pcb_in);
    return;
  }

  uint8_t cla = apdu[0];
  uint8_t ins = apdu[1];
  uint8_t p1 = apdu[2];
  uint8_t p2 = apdu[3];
  (void)cla;

  if (ins == T4T_APDU_INS_SELECT) {
    if (apdu_len < T4T_APDU_HDR_LEN) {
      apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_ERR_SYNTAX);
    } else {
      uint8_t lc = apdu[4];
      if (apdu_len < T4T_APDU_HDR_LEN + lc) {
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_ERR_SYNTAX);
      } else if (p1 == T4T_APDU_P1_SELECT_AID && lc == T4T_APDU_LC_AID &&
                 memcmp(&apdu[5], k_ndef_aid, T4T_APDU_LC_AID) == 0) {
        s_app_selected = true;
        s_file = T4T_FILE_NONE;
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_OK);
      } else if (p1 == T4T_APDU_P1_SELECT_FILE && lc == T4T_APDU_LC_FID) {
        uint16_t fid = (uint16_t)((apdu[5] << 8) | apdu[6]);
        if (fid == T4T_CC_FID) {
          s_file = T4T_FILE_CC;
          apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_OK);
        } else if (fid == T4T_NDEF_FID) {
          s_file = T4T_FILE_NDEF;
          apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_OK);
        } else {
          apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_FILE_NOT_FOUND);
        }
      } else {
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_FILE_NOT_FOUND);
      }
    }
    send_i_block_resp(resp, resp_len, pcb_in);
    return;
  }

  if (ins == T4T_APDU_INS_READ) {
    if (apdu_len < T4T_APDU_HDR_LEN) {
      apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_ERR_SYNTAX);
    } else if (s_file == T4T_FILE_NONE || !s_app_selected) {
      apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_SEC_ERROR);
    } else {
      uint16_t off = (uint16_t)((p1 << 8) | p2);
      uint16_t flen = file_len(s_file);
      uint8_t le = apdu[4];
      uint16_t max_le = (le == 0) ? T4T_APDU_LE_MAX : le;
      if (max_le > T4T_ML) {
        apdu_resp(resp, &resp_len, NULL, 0, (uint16_t)(T4T_APDU_SW_LE_INVALID | (T4T_ML & 0xFF)));
        send_i_block_resp(resp, resp_len, pcb_in);
        return;
      }
      if (off >= flen) {
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_EOF);
      } else {
        uint16_t avail = (uint16_t)(flen - off);
        uint16_t rd = (avail < max_le) ? avail : max_le;
        uint8_t *fp = file_ptr(s_file);
        apdu_resp(resp, &resp_len, &fp[off], rd, T4T_APDU_SW_OK);
      }
    }
    send_i_block_resp(resp, resp_len, pcb_in);
    return;
  }

  if (ins == T4T_APDU_INS_UPDATE) {
    if (apdu_len < T4T_APDU_HDR_LEN) {
      apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_ERR_SYNTAX);
    } else if (s_file != T4T_FILE_NDEF || !s_app_selected) {
      apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_SEC_ERROR);
    } else {
      uint16_t off = (uint16_t)((p1 << 8) | p2);
      uint8_t lc = apdu[4];
      if (apdu_len < T4T_APDU_HDR_LEN + lc) {
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_ERR_SYNTAX);
      } else if (lc > T4T_ML) {
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_ERR_SYNTAX);
      } else if (off + lc > (uint16_t)sizeof(s_ndef_file)) {
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_EOF);
      } else {
        memcpy(&s_ndef_file[off], &apdu[5], lc);
        apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_OK);
      }
    }
    send_i_block_resp(resp, resp_len, pcb_in);
    return;
  }

  apdu_resp(resp, &resp_len, NULL, 0, T4T_APDU_SW_WRONG_P1P2);
  send_i_block_resp(resp, resp_len, pcb_in);
}

static void process_block(const uint8_t *buf, size_t len) {
  if (buf == NULL || len == 0)
    return;
  uint8_t pcb = buf[0];
  size_t idx = 1;
  if (pcb & T4T_PCB_CID_MASK) {
    if (idx >= len || buf[idx] != s_pcd_cid)
      return;
    idx++;
  }
  if (s_pcd_cid != 0 && !(pcb & T4T_PCB_CID_MASK))
    return;
  if ((pcb & (uint8_t)~T4T_PCB_CID_MASK) == T4T_PCB_DESELECT && len == idx) {
    tx_with_crc(buf, (int)len);
    reset_link();
    hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
    s_state = T4T_STATE_SLEEP;
    return;
  }
  if ((pcb & T4T_PCB_CLASS_MASK) == T4T_PCB_R_CLASS) {
    if ((pcb & T4T_PCB_R_VALID_MASK) != T4T_PCB_R_ACK || len != idx || s_last_resp_len == 0)
      return;
    if (!(pcb & T4T_PCB_R_NAK_MASK) && (pcb & T4T_PCB_TOGGLE) == s_expected_block &&
        s_tx_offset < s_tx_length) {
      s_tx_pcb ^= T4T_PCB_TOGGLE;
      send_fragment();
    } else if ((pcb & T4T_PCB_R_NAK_MASK) || (pcb & T4T_PCB_TOGGLE) != s_expected_block) {
      tx_with_crc(s_last_resp, s_last_resp_len);
    }
    return;
  }
  /* NAD is not advertised in ATS; reject it before accessing INF. */
  if ((pcb & T4T_PCB_I_VALID_MASK) != T4T_PCB_I_BLOCK)
    return;
  if ((pcb & T4T_PCB_TOGGLE) != s_expected_block) {
    if (s_last_resp_len > 0)
      tx_with_crc(s_last_resp, s_last_resp_len);
    return;
  }
  if (s_tx_offset < s_tx_length)
    return;
  size_t inf_len = len - idx;
  if (inf_len > sizeof(s_chain_buf) - s_chain_len) {
    /* Never execute a truncated APDU or acknowledge discarded bytes. */
    s_chain_len = 0;
    s_chain_active = false;
    s_last_resp_len = 0;
    s_tx_length = 0;
    s_tx_offset = 0;
    s_state = T4T_STATE_SLEEP;
    s_app_selected = false;
    s_file = T4T_FILE_NONE;
    reset_application();
    hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
    return;
  }
  memcpy(s_chain_buf + s_chain_len, buf + idx, inf_len);
  s_chain_len += inf_len;
  s_expected_block ^= T4T_PCB_TOGGLE;
  s_active_tick = xTaskGetTickCount();
  if (pcb & T4T_PCB_CHAIN_MASK) {
    s_chain_active = true;
    send_r_ack(pcb);
    return;
  }
  handle_apdu(s_chain_buf, (int)s_chain_len, pcb);
  s_chain_active = false;
  s_chain_len = 0;
}

void t4t_emu_run_step(void) {
  uint8_t tgt_irq = 0;
  uint8_t main_irq = 0;
  uint8_t timer_irq = 0;

  uint8_t pts = 0;
  if (s_transport_error)
    return;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &tgt_irq) != HB_NFC_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main_irq) != HB_NFC_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &timer_irq) != HB_NFC_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts) != HB_NFC_OK) {
    s_transport_error = true;
    return;
  }
  main_irq |= s_pending_main;
  s_pending_main = 0;
  uint8_t pta_state = pts & T4T_PTA_STATE_MASK;

  if (!(main_irq & T4T_RX_IRQ) && (timer_irq & T4T_FIELD_OFF_IRQ)) {
    uint8_t aux = 0;
    if (hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux) != HB_NFC_OK) {
      s_transport_error = true;
      return;
    }
    if (!(aux & ST25R3916_AUX_DISP_EFD_BIT)) {
      reset_link();
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE);
      s_state = T4T_STATE_SENSE;
      return;
    }
  }
  if (s_state != T4T_STATE_ACTIVE) {
    if (pta_state != T4T_PTA_STATE_ACTIVE && pta_state != T4T_PTA_STATE_HALTED)
      return;
    reset_link();
    s_state = T4T_STATE_ACTIVE;
    s_active_tick = xTaskGetTickCount();
  }

  if (!(main_irq & T4T_RX_IRQ))
    goto idle_active;

  uint8_t buf[T4T_CHAIN_BUF_SIZE + T4T_CRC_SIZE] = {0};
  uint8_t fs1 = 0, fs2 = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS1, &fs1) != HB_NFC_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS2, &fs2) != HB_NFC_OK) {
    s_transport_error = true;
    return;
  }
  size_t len = fs1 | ((size_t)(fs2 & ST25R3916_FIFO_STATUS2_MSB_MASK) << 2);
  if (len == 0)
    return;
  if (len > sizeof(buf) || (fs2 & T4T_FIFO_INVALID_MASK) != 0) {
    st25r3916_fifo_clear();
    return;
  }
  if (hb_nfc_spi_fifo_read(buf, len) != HB_NFC_OK) {
    s_transport_error = true;
    return;
  }
  if (len <= T4T_CRC_SIZE || !iso14443a_check_crc(buf, len))
    return;
  len -= T4T_CRC_SIZE;

  if (buf[0] == T4T_CMD_RATS && len == 2) {
    static const uint16_t frame_sizes[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
    uint8_t fsdi = buf[1] >> T4T_FSDI_SHIFT;
    if (fsdi >= sizeof(frame_sizes) / sizeof(frame_sizes[0]) ||
        (buf[1] & T4T_PTA_STATE_MASK) == T4T_CID_INVALID)
      return;
    s_frame_size = frame_sizes[fsdi];
    s_expected_block = 0;
    s_last_resp_len = 0;
    s_tx_length = 0;
    s_tx_offset = 0;
    s_chain_len = 0;
    s_chain_active = false;
    s_app_selected = false;
    s_file = T4T_FILE_NONE;
    s_pcd_cid = buf[1] & T4T_PTA_STATE_MASK;
    reset_application();
    s_has_rats = true;
    ESP_LOGI(TAG, "RATS");
    tx_with_crc(k_ats, (int)s_ats_len);
    return;
  }

  if (!s_has_rats)
    return;
  if ((buf[0] & T4T_CMD_PPS_MASK) == T4T_CMD_PPS_BASE) {
    if ((buf[0] & T4T_PTA_STATE_MASK) != s_pcd_cid || len < 2 ||
        !((len == 2 && buf[1] == 0x01) || (len == 3 && buf[1] == 0x11 && buf[2] == 0)))
      return;
    tx_with_crc(buf, 1);
    return;
  }

  process_block(buf, (size_t)len);
  return;

idle_active: {
  TickType_t now = xTaskGetTickCount();
  if ((now - s_active_tick) > pdMS_TO_TICKS(T4T_PTA_ACTIVE_TIMEOUT)) {
    ESP_LOGD(TAG, "ACTIVE: idle timeout -> SENSE");
    reset_link();
    hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE);
    s_state = T4T_STATE_SENSE;
  } else if (((now - s_active_tick) % pdMS_TO_TICKS(T4T_PTA_IDLE_CHECK)) == 0U) {
    uint8_t aux = 0;
    hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
    ESP_LOGD(TAG, "[ACTIVE] AUX=0x%02X pta=%u", aux, pta_state);
  }
}
}
