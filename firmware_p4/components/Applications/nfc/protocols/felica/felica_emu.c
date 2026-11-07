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

#include "felica_emu.h"
#include "nfc_felica.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"

static const char *TAG = "NFC_FELICA_EMU";

#define MODE_TARGET_NFCF    0xA0U
#define OP_CTRL_TARGET      0xC3U
#define FELICA_AUX_DEF      0x18U
#define FELICA_ISO_F_CTRL   0x80U
#define FELICA_BITRATE_212  0x11U
#define SENSE_IDLE_TIMEOUT  30000U
#define ACTIVE_IDLE_TIMEOUT 20000U
#define TIMER_I_EON         0x10U
#define TIMER_I_EOF         0x08U

// Register bit masks and thresholds
#define AUX_DISP_OSC_MASK       0x14U
#define MAIN_INT_OSC_MASK       0x80U
#define TARGET_INT_OSC_MASK     0x08U
#define PT_MEM_SIZE             19U
#define BLK_FLAG_MASK           0x80U
#define DESC_LEN_2BYTE          2
#define DESC_LEN_3BYTE          3
#define WRITE_RESP_SIZE         12U
#define FELICA_SC_DEFAULT       0x0009U
#define FELICA_BLK_DEFAULT      16
#define OP_CTRL_INIT            0x00U
#define OP_CTRL_ACTIVE          0x80U
#define ISO14443A_CFG           0x10U
#define IC_CHIP_INVALID_A       0x00U
#define IC_CHIP_INVALID_B       0xFFU
#define IO_CONF2_CFG            0x80U
#define PT_MOD_CFG              0x60U
#define FIFO_STATUS_LEN_MASK    0x7FU
#define PTA_MASK                0x0FU
#define DELAY_5MS               5
#define DELAY_10MS              10
#define DELAY_2MS               2
#define DELAY_OSC_WAIT          200
#define ALIVE_LOG_MOD           10000U
#define IDLE_LOG_MOD            5000U
#define STATUS_OK               0x00U
#define STATUS_ERROR            0x01U
#define STATUS1_ERROR           0x01U
#define RSSI_IDX_SHIFT          6
#define FIFO_RSP_POS            0
#define FIFO_LEN_LIMIT          256
#define OSC_POLLING_INTERVAL_MS 1
#define FRAME_LEN_MIN_BYTES     2
#define CMD_CODE_OFFSET         1
#define DESC_POS_INCREMENT      1

typedef enum {
  FELICA_STATE_SLEEP,
  FELICA_STATE_SENSE,
  FELICA_STATE_ACTIVE,
} felica_emu_state_t;

static felica_emu_card_t s_card;
static nfc_felica_listener_t s_listener;
static nfc_felica_card_t s_legacy;
static nfc_felica_system_t s_legacy_system;
static nfc_felica_service_t s_legacy_service;
static nfc_felica_public_t s_legacy_blocks[FELICA_EMU_MAX_BLOCKS];
static esp_err_t s_error;

static felica_emu_state_t s_state = FELICA_STATE_SLEEP;
static bool s_init_done = false;
static uint32_t s_idle = 0;

static esp_err_t wait_oscillator(int timeout_ms) {
  for (int i = 0; i < timeout_ms; i++) {
    uint8_t aux = 0, mi = 0, ti = 0;
    ESP_RETURN_ON_ERROR(
        hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux), TAG, "Oscillator SPI read failed");
    ESP_RETURN_ON_ERROR(
        hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &mi), TAG, "Oscillator SPI read failed");
    ESP_RETURN_ON_ERROR(
        hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &ti), TAG, "Oscillator SPI read failed");
    if (((aux & AUX_DISP_OSC_MASK) != 0U) || ((mi & MAIN_INT_OSC_MASK) != 0U) ||
        ((ti & TARGET_INT_OSC_MASK) != 0U)) {
      ESP_LOGI(TAG, "Osc OK in %dms: AUX=0x%02X MAIN=0x%02X TGT=0x%02X", i, aux, mi, ti);
      return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(OSC_POLLING_INTERVAL_MS));
  }
  ESP_LOGW(TAG, "Oscillator timeout");
  return ESP_ERR_TIMEOUT;
}

static void tx_response(const uint8_t *data, int len) {
  if (data == NULL || len <= 0)
    return;
  st25r3916_fifo_clear();
  st25r3916_fifo_set_tx_bytes((uint16_t)len, 0);
  s_error = st25r3916_fifo_load(data, (size_t)len);
  if (s_error == ESP_OK)
    s_error = hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WITH_CRC);
  if (s_error == ESP_OK)
    s_error = st25r3916_irq_wait_txe();
}

static void goto_sleep(void) {
  nfc_felica_listener_reset(&s_listener);
  st25r3916_fifo_clear();
  s_error = hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
  s_state = FELICA_STATE_SLEEP;
  s_idle = 0;
}

static esp_err_t write_pt_mem_f(void) {
  uint8_t pt[PT_MEM_SIZE] = {0};
  pt[FELICA_EMU_PT_SC_OFFSET] = (uint8_t)(s_card.system_code >> FELICA_EMU_BYTE_SHIFT_HI);
  pt[FELICA_EMU_PT_SC_OFFSET + 1] = (uint8_t)(s_card.system_code & FELICA_EMU_BYTE_MASK_SC_LO);
  pt[FELICA_EMU_PT_DESC_OFFSET] = FELICA_EMU_PT_DESC_VALUE;
  memcpy(&pt[FELICA_EMU_PT_IDM_OFFSET], s_card.idm, FELICA_IDM_LEN);
  memcpy(&pt[FELICA_EMU_PT_PMM_OFFSET], s_card.pmm, FELICA_PMM_LEN);
  ESP_RETURN_ON_ERROR(hb_nfc_spi_pt_mem_write(ST25R3916_SPI_PT_MEM_F_WRITE, pt, sizeof(pt)),
                      TAG,
                      "Passive target memory write failed");
  char idm_s[FELICA_IDM_STR_BUFFER_SIZE];
  snprintf(idm_s,
           sizeof(idm_s),
           "%02X%02X%02X%02X%02X%02X%02X%02X",
           s_card.idm[0],
           s_card.idm[1],
           s_card.idm[2],
           s_card.idm[3],
           s_card.idm[4],
           s_card.idm[5],
           s_card.idm[6],
           s_card.idm[7]);
  ESP_LOGI(TAG,
           "PT Mem F: SC=0x%04X IDm=%s PMm=%02X%02X%02X%02X%02X%02X%02X%02X",
           s_card.system_code,
           idm_s,
           s_card.pmm[0],
           s_card.pmm[1],
           s_card.pmm[2],
           s_card.pmm[3],
           s_card.pmm[4],
           s_card.pmm[5],
           s_card.pmm[6],
           s_card.pmm[7]);
  return ESP_OK;
}
void felica_emu_card_from_tag(
    felica_emu_card_t *card, const felica_tag_t *tag, uint16_t sc, uint8_t bc, const uint8_t *mem) {
  if (card == NULL || tag == NULL)
    return;
  memset(card, 0, sizeof(*card));
  memcpy(card->idm, tag->idm, FELICA_IDM_LEN);
  memcpy(card->pmm, tag->pmm, FELICA_PMM_LEN);
  card->system_code = tag->rd_valid
                          ? (uint16_t)((tag->rd[0] << FELICA_EMU_BYTE_SHIFT_HI) | tag->rd[1])
                          : FELICA_SC_COMMON;
  card->service_code = sc;
  card->block_count = bc;
  if (mem) {
    size_t bytes = (size_t)bc * FELICA_BLOCK_SIZE;
    if (bytes > FELICA_EMU_MEM_SIZE)
      bytes = FELICA_EMU_MEM_SIZE;
    memcpy(card->mem, mem, bytes);
  }
}

void felica_emu_card_default(felica_emu_card_t *card,
                             const uint8_t idm[FELICA_IDM_LEN],
                             const uint8_t pmm[FELICA_PMM_LEN]) {
  if (card == NULL)
    return;
  memset(card, 0, sizeof(*card));
  memcpy(card->idm, idm, FELICA_IDM_LEN);
  memcpy(card->pmm, pmm, FELICA_PMM_LEN);
  card->system_code = FELICA_SC_COMMON;
  card->service_code = FELICA_SC_DEFAULT;
  card->block_count = FELICA_BLK_DEFAULT;
}

hb_nfc_err_t felica_emu_init(const felica_emu_card_t *card) {
  if (card == NULL || card->block_count > FELICA_EMU_MAX_BLOCKS)
    return HB_NFC_ERR_PARAM;
  memcpy(&s_card, card, sizeof(s_card));
  memset(&s_legacy, 0, sizeof(s_legacy));
  memset(&s_legacy_system, 0, sizeof(s_legacy_system));
  memset(s_legacy_blocks, 0, sizeof(s_legacy_blocks));
  memcpy(s_legacy.tag.idm, card->idm, FELICA_IDM_LEN);
  memcpy(s_legacy.tag.pmm, card->pmm, FELICA_PMM_LEN);
  s_legacy.type = NFC_FELICA_TYPE_STANDARD;
  s_legacy.system_count = 1;
  s_legacy.systems = &s_legacy_system;
  s_legacy_system.code = card->system_code;
  s_legacy_system.services = &s_legacy_service;
  s_legacy_system.service_count = 1;
  s_legacy_service.code = card->service_code;
  s_legacy_system.blocks = s_legacy_blocks;
  s_legacy_system.block_count = card->block_count;
  for (size_t i = 0; i < card->block_count; i++) {
    s_legacy_blocks[i].service = card->service_code;
    s_legacy_blocks[i].index = i;
    memcpy(s_legacy_blocks[i].block.data, card->mem + i * FELICA_BLOCK_SIZE, FELICA_BLOCK_SIZE);
  }
  nfc_felica_listener_init(&s_listener, &s_legacy);
  s_error = ESP_OK;
  s_state = FELICA_STATE_SLEEP;
  s_idle = 0;
  s_init_done = true;
  char idm_s[FELICA_IDM_STR_BUFFER_SIZE];
  snprintf(idm_s,
           sizeof(idm_s),
           "%02X%02X%02X%02X%02X%02X%02X%02X",
           card->idm[0],
           card->idm[1],
           card->idm[2],
           card->idm[3],
           card->idm[4],
           card->idm[5],
           card->idm[6],
           card->idm[7]);
  ESP_LOGI(TAG, "Init: IDm=%s SC=0x%04X blocks=%d", idm_s, card->system_code, card->block_count);
  return HB_NFC_OK;
}

esp_err_t felica_emu_init_profile(nfc_felica_card_t *card) {
  if (!card || (card->type != NFC_FELICA_TYPE_LITE && card->type != NFC_FELICA_TYPE_STANDARD) ||
      (card->type == NFC_FELICA_TYPE_STANDARD && (!card->system_count || !card->systems)))
    return ESP_ERR_INVALID_ARG;
  felica_emu_card_t identity = {0};
  memcpy(identity.idm, card->tag.idm, FELICA_IDM_LEN);
  memcpy(identity.pmm, card->tag.pmm, FELICA_PMM_LEN);
  memset(identity.pmm + 2, UINT8_MAX, FELICA_PMM_LEN - 2);
  identity.system_code =
      card->type == NFC_FELICA_TYPE_LITE ? NFC_FELICA_LITE_SYSTEM : card->systems[0].code;
  if (card->type == NFC_FELICA_TYPE_STANDARD)
    identity.idm[0] &= 0x0FU;
  esp_err_t error = felica_emu_init(&identity);
  if (error == ESP_OK)
    nfc_felica_listener_init(&s_listener, card);
  return error;
}

esp_err_t felica_emu_error(void) {
  return s_error;
}
bool felica_emu_dirty(void) {
  return s_listener.is_dirty;
}

hb_nfc_err_t felica_emu_configure_target(void) {
  if (!s_init_done)
    return HB_NFC_ERR_INTERNAL;
  ESP_LOGI(TAG, "Configure NFC-F target");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, OP_CTRL_INIT),
                      TAG,
                      "Target SPI operation failed");
  vTaskDelay(pdMS_TO_TICKS(DELAY_5MS));
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_SET_DEFAULT), TAG, "Target SPI operation failed");
  vTaskDelay(pdMS_TO_TICKS(DELAY_10MS));
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, ISO14443A_CFG),
                      TAG,
                      "Target SPI operation failed");
  vTaskDelay(pdMS_TO_TICKS(DELAY_2MS));
  uint8_t ic = 0;
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &ic), TAG, "Target SPI operation failed");
  if (ic == IC_CHIP_INVALID_A || ic == IC_CHIP_INVALID_B) {
    ESP_LOGE(TAG, "Chip not responding: IC=0x%02X", ic);
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG, "Chip OK: IC=0x%02X", ic);
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_IO_CONF2, IO_CONF2_CFG),
                      TAG,
                      "Target SPI operation failed");
  vTaskDelay(pdMS_TO_TICKS(DELAY_2MS));
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, OP_CTRL_ACTIVE),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(wait_oscillator(DELAY_OSC_WAIT), TAG, "Oscillator unavailable");
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS), TAG, "Target SPI operation failed");
  vTaskDelay(pdMS_TO_TICKS(DELAY_10MS));
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_MODE, MODE_TARGET_NFCF),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_AUX_DEF, FELICA_AUX_DEF),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, FELICA_BITRATE_212),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_PASSIVE_TARGET, STATUS_OK),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443B_FELICA, FELICA_ISO_F_CTRL),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_ACT, STATUS_OK),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_DEACT, STATUS_OK),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_reg_write(ST25R3916_REG_PT_MOD, PT_MOD_CFG), TAG, "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(write_pt_mem_f(), TAG, "Target memory failed");
  {
    st25r3916_irq_status_t s;
    ESP_RETURN_ON_ERROR(st25r3916_irq_read(&s), TAG, "Target IRQ read failed");
  }
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_MASK_MAIN_INT, STATUS_OK),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TIMER_NFC_INT, STATUS_OK),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_MASK_ERROR_WUP_INT, STATUS_OK),
                      TAG,
                      "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TARGET_INT, STATUS_OK),
                      TAG,
                      "Target SPI operation failed");
  uint8_t mode_rb = 0, aux_rb = 0, isof_rb = 0;
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_reg_read(ST25R3916_REG_MODE, &mode_rb), TAG, "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DEF, &aux_rb), TAG, "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443B_FELICA, &isof_rb),
                      TAG,
                      "Target SPI operation failed");
  if (mode_rb != MODE_TARGET_NFCF) {
    ESP_LOGE(TAG, "MODE mismatch: 0x%02X", mode_rb);
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG, "Configured: MODE=0x%02X AUX_DEF=0x%02X ISO_F=0x%02X", mode_rb, aux_rb, isof_rb);
  return HB_NFC_OK;
}

hb_nfc_err_t felica_emu_start(void) {
  if (!s_init_done)
    return HB_NFC_ERR_INTERNAL;
  {
    st25r3916_irq_status_t s;
    ESP_RETURN_ON_ERROR(st25r3916_irq_read(&s), TAG, "Target IRQ read failed");
  }
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, OP_CTRL_TARGET),
                      TAG,
                      "Target SPI operation failed");
  vTaskDelay(pdMS_TO_TICKS(DELAY_5MS));
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP), TAG, "Target SPI operation failed");
  vTaskDelay(pdMS_TO_TICKS(DELAY_2MS));
  s_state = FELICA_STATE_SLEEP;
  s_idle = 0;
  uint8_t op = 0, aux = 0, pts = 0;
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &op), TAG, "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(
      hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux), TAG, "Target SPI operation failed");
  ESP_RETURN_ON_ERROR(hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts),
                      TAG,
                      "Target SPI operation failed");
  ESP_LOGI(TAG, "Start (SLEEP): OP=0x%02X AUX=0x%02X PT_STS=0x%02X", op, aux, pts);
  ESP_LOGI(TAG, "FeliCa emulation active - present a reader!");
  return HB_NFC_OK;
}

void felica_emu_stop(void) {
  if (!s_init_done)
    return;
  esp_err_t error = hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, OP_CTRL_INIT);
  if (s_error == ESP_OK)
    s_error = error;
  s_state = FELICA_STATE_SLEEP;
  s_idle = 0;
  s_init_done = false;
  nfc_felica_listener_reset(&s_listener);
  ESP_LOGI(TAG, "FeliCa emulation stopped");
}

void felica_emu_run_step(void) {
  if (!s_init_done || s_error != ESP_OK)
    return;
  static uint32_t s_alive = 0;
  if ((++s_alive % ALIVE_LOG_MOD) == 0U) {
    uint8_t aux = 0, pts = 0, op = 0;
    s_error = hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
    if (s_error != ESP_OK)
      return;
    s_error = hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts);
    if (s_error != ESP_OK)
      return;
    s_error = hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &op);
    if (s_error != ESP_OK)
      return;
    ESP_LOGI(TAG,
             "alive | state=%s OP=%02X AUX=%02X efd=%d pta=%d",
             s_state == FELICA_STATE_SLEEP   ? "SLEEP"
             : s_state == FELICA_STATE_SENSE ? "SENSE"
                                             : "ACTIVE",
             op,
             aux,
             (aux >> RSSI_IDX_SHIFT) & 1,
             pts & PTA_MASK);
  }

  uint8_t tgt_irq = 0;
  uint8_t main_irq = 0;
  uint8_t timer_irq = 0;
  uint8_t err_irq = 0;
  s_error = hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &tgt_irq);
  if (s_error != ESP_OK)
    return;
  s_error = hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main_irq);
  if (s_error != ESP_OK)
    return;
  s_error = hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &timer_irq);
  if (s_error != ESP_OK)
    return;
  s_error = hb_nfc_spi_reg_read(ST25R3916_REG_ERROR_INT, &err_irq);
  if (s_error != ESP_OK)
    return;

  uint8_t pts = 0;
  s_error = hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts);
  if (s_error != ESP_OK)
    return;
  uint8_t pta = pts & PTA_MASK;

  if (s_state == FELICA_STATE_SLEEP) {
    bool wake = (timer_irq & TIMER_I_EON) || (tgt_irq & ST25R3916_IRQ_TGT_WU_F) ||
                (tgt_irq & ST25R3916_IRQ_TGT_WU_A);
    if (!wake)
      return;

    ESP_LOGI(TAG,
             "SLEEP -> SENSE (pta=%u tgt=0x%02X main=0x%02X tmr=0x%02X err=0x%02X)",
             pta,
             tgt_irq,
             main_irq,
             timer_irq,
             err_irq);

    s_error = hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE);
    if (s_error != ESP_OK)
      return;
    s_state = FELICA_STATE_SENSE;
    s_idle = 0;
    if (!(main_irq & ST25R3916_IRQ_MAIN_RXE))
      return;
  }

  if (s_state == FELICA_STATE_SENSE) {
    if (tgt_irq || main_irq || timer_irq || err_irq) {
      uint8_t rssi = 0;
      s_error = hb_nfc_spi_reg_read(ST25R3916_REG_RSSI_RESULT, &rssi);
      if (s_error != ESP_OK)
        return;
      ESP_LOGI(TAG,
               "[SENSE-IRQ] tgt=0x%02X main=0x%02X tmr=0x%02X err=0x%02X pta=%u RSSI=0x%02X",
               tgt_irq,
               main_irq,
               timer_irq,
               err_irq,
               pta,
               rssi);
    }

    bool go_active = (tgt_irq & ST25R3916_IRQ_TGT_WU_F) || (main_irq & ST25R3916_IRQ_MAIN_RXE);

    if (go_active) {
      s_idle = 0;
      ESP_LOGI(TAG,
               "SENSE -> ACTIVE (pta=%u tgt=0x%02X main=0x%02X tmr=0x%02X)",
               pta,
               tgt_irq,
               main_irq,
               timer_irq);
      s_state = FELICA_STATE_ACTIVE;
      if (!(main_irq & ST25R3916_IRQ_MAIN_RXE))
        return;

    } else if (timer_irq & TIMER_I_EOF) {
      uint8_t rssi = 0;
      s_error = hb_nfc_spi_reg_read(ST25R3916_REG_RSSI_RESULT, &rssi);
      if (s_error != ESP_OK)
        return;
      ESP_LOGW(TAG,
               "SENSE: field lost -> SLEEP | timer_irq=0x%02X tgt=0x%02X pta=%u RSSI=0x%02X",
               timer_irq,
               tgt_irq,
               pta,
               rssi);
      goto_sleep();
      return;

    } else if (!tgt_irq && !main_irq && !timer_irq) {
      if ((++s_idle) >= SENSE_IDLE_TIMEOUT) {
        ESP_LOGI(TAG, "SENSE: timeout -> SLEEP");
        goto_sleep();
        return;
      }
      if ((s_idle % IDLE_LOG_MOD) == 0U) {
        uint8_t aux = 0, rssi = 0;
        s_error = hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
        if (s_error != ESP_OK)
          return;
        s_error = hb_nfc_spi_reg_read(ST25R3916_REG_RSSI_RESULT, &rssi);
        if (s_error != ESP_OK)
          return;
        ESP_LOGI(TAG,
                 "[SENSE] idle=%lu AUX=0x%02X pta=%d RSSI=0x%02X",
                 (unsigned long)s_idle,
                 aux,
                 pta,
                 rssi);
      }
      return;
    } else {
      return;
    }
  }

  if (timer_irq & TIMER_I_EOF) {
    uint8_t aux = 0;
    s_error = hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
    if (s_error != ESP_OK)
      return;
    ESP_LOGI(TAG, "ACTIVE: field lost (AUX=0x%02X) -> SLEEP", aux);
    goto_sleep();
    return;
  }
  if (!(main_irq & ST25R3916_IRQ_MAIN_RXE)) {
    if ((++s_idle) >= ACTIVE_IDLE_TIMEOUT) {
      ESP_LOGI(TAG, "ACTIVE: idle timeout -> SLEEP");
      goto_sleep();
    }
    return;
  }
  s_idle = 0;

  uint8_t buf[FIFO_LEN_LIMIT] = {0};
  size_t length = st25r3916_fifo_get_count();
  if (!length)
    return;
  if (length > sizeof(buf)) {
    st25r3916_fifo_clear();
    return;
  }
  s_error = st25r3916_fifo_read(buf, length);
  if (s_error != ESP_OK)
    return;
  if (length < FRAME_LEN_MIN_BYTES || (length != buf[0] && length != (size_t)buf[0] + 2U))
    return;
  if (err_irq) {
    st25r3916_fifo_clear();
    return;
  }
  nfc_felica_frame_t response = {0};
  esp_err_t error = nfc_felica_listener_frame(&s_listener, buf, buf[0], &response);
  if (error == ESP_OK && response.size)
    tx_response(response.data, response.size);
}
