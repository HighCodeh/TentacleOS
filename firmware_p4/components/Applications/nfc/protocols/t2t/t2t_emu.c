// SPDX-License-Identifier: GPL-3.0-or-later
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

#include "t2t_emu.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "legacy_des/des.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"

static const char *TAG = "NFC_T2T_EMU";

#define T2T_PAGE_SIZE 4
#define T2T_MAX_PAGES 256
#define T2T_MEM_SIZE  (T2T_PAGE_SIZE * T2T_MAX_PAGES)

#define T2T_CMD_READ        0x30
#define T2T_CMD_WRITE       0xA2
#define T2T_CMD_COMP_WRITE  0xA0
#define T2T_CMD_GET_VERSION 0x60
#define T2T_CMD_FAST_READ   0x3A
#define T2T_CMD_PWD_AUTH    0x1B
#define T2T_CMD_ULC_AUTH    0x1A
#define T2T_CMD_HALT        0x50

#define T2T_ACK 0x0A

#define NTAG_ACCESS_PROT 0x80

#define OP_CTRL_TARGET 0xC3

#define IC_IDENTITY_NOT_RESPONDING_LO 0x00
#define IC_IDENTITY_NOT_RESPONDING_HI 0xFF

#define T2T_IO_CONF2_SUP_AAT 0x80
#define T2T_AUX_DEF_NFC_N    0x10
#define T2T_PT_MOD_OOK       0x60
#define T2T_BIT_RATE_RX_212  0x10

#define AUX_DISPLAY_OSC_MASK 0x14U
#define FIFO_LEN_MASK        0x7FU

#define PTA_STATE_MASK        0x0FU
#define PTA_STATE_IDLE        0x00U
#define PTA_STATE_READY_A     0x01U
#define PTA_STATE_READY_Ax    0x02U
#define PTA_STATE_ACTIVE      0x03U
#define PTA_STATE_SELECTED    0x05U
#define PTA_STATE_ACTIVE_STAR 0x0DU

#define PT_MEM_SIZE   15
#define PT_IDX_ATQA_0 10
#define PT_IDX_ATQA_1 11
#define PT_IDX_CT     12
#define PT_IDX_SAK    13

#define ISO14443A_CASCADE_TAG 0x88U
#define ISO14443A_CT_BYTE     0x04U

#define T2T_INTERNAL_BYTE 0x48U
#define T2T_CC_NDEF_MAGIC 0xE1U
#define T2T_CC_VERSION_10 0x10U

#define T2T_READ_RESP_SIZE    16
#define T2T_FAST_READ_MAX_LEN 256
#define T2T_COMP_WRITE_LEN    16
#define T2T_CMD_BUF_SIZE      32

#define T2T_STATIC_LOCK_BYTE0      10
#define T2T_STATIC_LOCK_BYTE1      11
#define T2T_FIRST_LOCKED_PAGE      2
#define T2T_STATIC_LOCK_START      3
#define T2T_STATIC_LOCK_END        19
#define T2T_DYN_LOCK_BASE          19
#define T2T_DYN_LOCK_PAGES_PER_BIT 4U

#define T2T_DYN_LOCK_BIT_COUNT 16

#define T2T_PAGE_UID_END 2

#define T2T_PAGE_OTP 3

#define T2T_UID_LEN_LONG 7

#define NDEF_TLV_MESSAGE     0x03U
#define NDEF_TLV_TERMINATOR  0xFEU
#define NDEF_REC_HDR_TEXT    0xD1U
#define NDEF_TYPE_LEN_TEXT   0x01U
#define NDEF_TEXT_UTF8_LANG2 0x02U

#define NDEF_TLV_HEADER_LEN  2
#define NDEF_REC_OVERHEAD    4
#define NDEF_TEXT_PREFIX_LEN 3

#define T2T_DES_BLOCK_SIZE 8

#define T2T_ULC_KEY_LEN 16

#define T2T_OSC_TIMEOUT_MS    200
#define T2T_DELAY_OSC_POLL_MS 1
#define T2T_DELAY_SHORT_MS    2
#define T2T_DELAY_MEDIUM_MS   5
#define T2T_DELAY_LONG_MS     10
#define T2T_IDLE_LOG_INTERVAL 500U

#define T2T_COMP_WRITE_PAGES 4

static const uint8_t k_atqa[2] = {0x44, 0x00};
static const uint8_t k_sak = 0x00;
static const uint8_t k_uid7_default[T2T_UID_LEN_LONG] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

typedef struct {
  t2t_tag_type_t type;
  uint16_t total_pages;
  uint16_t user_start;
  uint16_t user_end;
  uint16_t dyn_lock_page;
  uint16_t cfg_page;
  uint16_t pwd_page;
  uint16_t pack_page;
  uint8_t cc_size;
  bool supports_get_version;
  bool supports_pwd;
  bool supports_ulc_auth;
  uint8_t version[8];
} t2t_profile_t;

static const t2t_profile_t k_profiles[] = {
    {
        .type = T2T_TAG_NTAG213,
        .total_pages = 45,
        .user_start = 4,
        .user_end = 39,
        .dyn_lock_page = 40,
        .cfg_page = 41,
        .pwd_page = 43,
        .pack_page = 44,
        .cc_size = 0x12,
        .supports_get_version = true,
        .supports_pwd = true,
        .supports_ulc_auth = false,
        .version = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03},
    },
    {
        .type = T2T_TAG_NTAG215,
        .total_pages = 135,
        .user_start = 4,
        .user_end = 129,
        .dyn_lock_page = 130,
        .cfg_page = 131,
        .pwd_page = 133,
        .pack_page = 134,
        .cc_size = 0x3F,
        .supports_get_version = true,
        .supports_pwd = true,
        .supports_ulc_auth = false,
        .version = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03},
    },
    {
        .type = T2T_TAG_NTAG216,
        .total_pages = 231,
        .user_start = 4,
        .user_end = 225,
        .dyn_lock_page = 226,
        .cfg_page = 227,
        .pwd_page = 229,
        .pack_page = 230,
        .cc_size = 0x6F,
        .supports_get_version = true,
        .supports_pwd = true,
        .supports_ulc_auth = false,
        .version = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x13, 0x03},
    },
    {
        .type = T2T_TAG_ULTRALIGHT_C,
        .total_pages = 48,
        .user_start = 4,
        .user_end = 15,
        .dyn_lock_page = 40,
        .cfg_page = 41,
        .pwd_page = 0,
        .pack_page = 0,
        .cc_size = 0x06,
        .supports_get_version = false,
        .supports_pwd = false,
        .supports_ulc_auth = true,
        .version = {0},
    },
};

typedef enum {
  T2T_STATE_SLEEP,
  T2T_STATE_SENSE,
  T2T_STATE_ACTIVE,
} t2t_state_t;

typedef enum {
  ULC_AUTH_IDLE = 0,
  ULC_AUTH_WAIT,
} ulc_auth_state_t;

static uint8_t s_mem[T2T_MEM_SIZE];
static uint16_t s_page_count = 0;
static uint16_t s_mem_size = 0;
static t2t_profile_t s_prof;

static uint8_t s_uid[T2T_UID_LEN_LONG];
static uint8_t s_uid_len = T2T_UID_LEN_LONG;

static bool s_pwd_enabled = false;
static uint8_t s_pwd[4];
static uint8_t s_pack[2];
static uint8_t s_auth0 = 0xFF;
static uint8_t s_access = 0x00;

static bool s_authenticated = false;
static ulc_auth_state_t s_ulc_state = ULC_AUTH_IDLE;
static bool s_ulc_auth_enabled = false;
static uint8_t s_ulc_key[T2T_ULC_KEY_LEN];
static uint8_t s_ulc_rndb[T2T_DES_BLOCK_SIZE];
static uint8_t s_ulc_last_iv[T2T_DES_BLOCK_SIZE];

static bool s_comp_write_pending = false;
static uint8_t s_comp_write_page = 0;

static t2t_state_t s_state = T2T_STATE_SLEEP;
static bool s_init_done = false;

static uint32_t s_idle_sense = 0;
static uint32_t s_idle_active = 0;

/**
 * @brief Parameters for des3_cbc_crypt().
 */
typedef struct {
  bool encrypt;         /**< true for encrypt, false for decrypt. */
  const uint8_t *key;   /**< 16-byte 2-key 3DES key. */
  const uint8_t *iv_in; /**< 8-byte input IV. */
  uint8_t *iv_out;      /**< 8-byte output IV (NULL to discard). */
} des3_cbc_params_t;

static bool wait_oscillator(int timeout_ms);
static const t2t_profile_t *profile_for_type(t2t_tag_type_t type);
static void t2t_reset_auth(void);
static uint16_t get_static_lock_bits(void);
static uint16_t get_dynamic_lock_bits(void);
static bool is_page_locked(uint16_t page);
static bool is_page_protected(uint16_t page);
static bool can_read_page(uint16_t page);
static bool can_write_page(uint16_t page);
static void otp_write_bytes(uint8_t *dst, const uint8_t *src, size_t len);
static void ulc_random(uint8_t *out, size_t len);
static void rotate_left_8(uint8_t *out, const uint8_t *in);
static bool des3_cbc_crypt(const des3_cbc_params_t *p, const uint8_t *in, size_t len, uint8_t *out);
static hb_nfc_err_t load_pt_memory(void);
static void build_memory(const char *text);
static void tx_with_crc(const uint8_t *data, int len);
static void tx_4bit_nack(uint8_t code);
static void tx_ack(void);
static int rx_poll(uint8_t *buf, int max, uint8_t main_irq);
static bool write_page_internal(uint8_t page, const uint8_t *data);
static void handle_read(uint8_t page);
static void handle_fast_read(uint8_t start, uint8_t end);
static void handle_write(uint8_t page, const uint8_t *data);
static void handle_comp_write_start(uint8_t page);
static void handle_comp_write_data(const uint8_t *data, int len);
static void handle_get_version(void);
static void handle_pwd_auth(const uint8_t *cmd, int len);
static void handle_ulc_auth_start(void);
static void handle_ulc_auth_step2(const uint8_t *data, int len);

hb_nfc_err_t t2t_emu_init(const t2t_emu_config_t *cfg) {
  if (cfg == NULL)
    return HB_NFC_ERR_PARAM;

  const t2t_profile_t *prof = profile_for_type(cfg->type);
  if (prof == NULL)
    return HB_NFC_ERR_PARAM;

  s_prof = *prof;
  s_page_count = s_prof.total_pages;
  s_mem_size = s_page_count * T2T_PAGE_SIZE;
  if (s_mem_size > T2T_MEM_SIZE)
    return HB_NFC_ERR_INTERNAL;

  if (cfg->uid_len == T2T_UID_LEN_LONG) {
    memcpy(s_uid, cfg->uid, T2T_UID_LEN_LONG);
    s_uid_len = T2T_UID_LEN_LONG;
  } else {
    memcpy(s_uid, k_uid7_default, sizeof(k_uid7_default));
    s_uid_len = T2T_UID_LEN_LONG;
  }

  s_pwd_enabled = cfg->enable_pwd && s_prof.supports_pwd;
  if (s_pwd_enabled) {
    memcpy(s_pwd, cfg->pwd, sizeof(s_pwd));
    memcpy(s_pack, cfg->pack, sizeof(s_pack));
    s_auth0 = cfg->auth0;
    s_access = cfg->access;
  } else {
    memset(s_pwd, 0x00, sizeof(s_pwd));
    memset(s_pack, 0x00, sizeof(s_pack));
    s_auth0 = 0xFF;
    s_access = 0x00;
  }

  s_ulc_auth_enabled = cfg->enable_ulc_auth && s_prof.supports_ulc_auth;
  if (s_prof.supports_ulc_auth) {
    if (s_ulc_auth_enabled)
      memcpy(s_ulc_key, cfg->ulc_key, sizeof(s_ulc_key));
    else
      memset(s_ulc_key, 0x00, sizeof(s_ulc_key));
  } else {
    memset(s_ulc_key, 0x00, sizeof(s_ulc_key));
  }

  build_memory(cfg->ndef_text);

  s_state = T2T_STATE_SLEEP;
  s_init_done = true;
  t2t_reset_auth();

  ESP_LOGI(TAG,
           "T2T init: type=%d pages=%u UID=%02X%02X%02X%02X%02X%02X%02X",
           (int)s_prof.type,
           s_page_count,
           s_uid[0],
           s_uid[1],
           s_uid[2],
           s_uid[3],
           s_uid[4],
           s_uid[5],
           s_uid[6]);
  return HB_NFC_OK;
}

hb_nfc_err_t t2t_emu_init_default(void) {
  t2t_emu_config_t cfg = {0};
  cfg.type = T2T_TAG_NTAG213;
  cfg.ndef_text = "High Boy NFC";
  memcpy(cfg.uid, k_uid7_default, sizeof(k_uid7_default));
  cfg.uid_len = T2T_UID_LEN_LONG;
  cfg.enable_pwd = false;
  cfg.auth0 = 0xFF;
  cfg.access = 0x00;
  cfg.enable_ulc_auth = false;
  return t2t_emu_init(&cfg);
}

hb_nfc_err_t t2t_emu_configure_target(void) {
  if (!s_init_done)
    return HB_NFC_ERR_INTERNAL;

  ESP_LOGI(TAG, "T2T configure target");

  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_OFF);
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_MEDIUM_MS));
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_SET_DEFAULT);
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_LONG_MS));

  hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, T2T_BIT_RATE_RX_212);
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_SHORT_MS));

  uint8_t ic = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &ic);
  if (ic == IC_IDENTITY_NOT_RESPONDING_LO || ic == IC_IDENTITY_NOT_RESPONDING_HI) {
    ESP_LOGE(TAG, "Chip not responding: IC=0x%02X", ic);
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG, "Chip OK: IC=0x%02X", ic);

  hb_nfc_spi_reg_write(ST25R3916_REG_IO_CONF2, T2T_IO_CONF2_SUP_AAT);
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_SHORT_MS));

  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_EN);
  wait_oscillator(T2T_OSC_TIMEOUT_MS);

  hb_nfc_spi_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS);
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_LONG_MS));

  hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_TARGET_NFCA);

  hb_nfc_spi_reg_write(ST25R3916_REG_AUX_DEF,
                       (s_uid_len == T2T_UID_LEN_LONG) ? T2T_AUX_DEF_NFC_N : 0x00);

  hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_PASSIVE_TARGET, 0x00);

  hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_ACT, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_FIELD_THRESH_DEACT, 0x00);

  hb_nfc_spi_reg_write(ST25R3916_REG_PT_MOD, T2T_PT_MOD_OOK);

  hb_nfc_err_t err = load_pt_memory();
  if (err != HB_NFC_OK)
    return err;

  {
    st25r3916_irq_status_t s;
    (void)st25r3916_irq_read(&s);
  }

  hb_nfc_spi_reg_write(ST25R3916_REG_MASK_MAIN_INT, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TIMER_NFC_INT, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_MASK_ERROR_WUP_INT, 0x00);
  hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TARGET_INT, 0x00);

  uint8_t mode_rb = 0, aux_rb = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_MODE, &mode_rb);
  hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DEF, &aux_rb);
  if (mode_rb != ST25R3916_MODE_TARGET_NFCA) {
    ESP_LOGE(TAG,
             "MODE readback mismatch: 0x%02X (expected 0x%02X)",
             mode_rb,
             ST25R3916_MODE_TARGET_NFCA);
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG, "Regs OK: MODE=0x%02X AUX=0x%02X", mode_rb, aux_rb);
  ESP_LOGI(TAG, "T2T target configured");
  return HB_NFC_OK;
}

hb_nfc_err_t t2t_emu_start(void) {
  if (!s_init_done)
    return HB_NFC_ERR_INTERNAL;

  {
    st25r3916_irq_status_t s;
    (void)st25r3916_irq_read(&s);
  }

  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, OP_CTRL_TARGET);
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_MEDIUM_MS));

  hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_SHORT_MS));

  s_state = T2T_STATE_SLEEP;
  t2t_reset_auth();

  uint8_t op = 0, aux = 0, pts = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &op);
  hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
  hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts);
  ESP_LOGI(TAG, "Start (SLEEP): OP=0x%02X AUX=0x%02X PT_STS=0x%02X", op, aux, pts);
  ESP_LOGI(TAG, "T2T emulation active - present the phone!");
  ESP_LOGI(TAG, "In SLEEP: efd should rise when external field is detected.");
  return HB_NFC_OK;
}

void t2t_emu_stop(void) {
  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_OFF);
  s_state = T2T_STATE_SLEEP;
  s_init_done = false;
  t2t_reset_auth();
  ESP_LOGI(TAG, "T2T emulation stopped");
}

uint8_t *t2t_emu_get_mem(void) {
  return s_mem;
}

void t2t_emu_run_step(void) {
  uint8_t tgt_irq = 0;
  uint8_t main_irq = 0;
  uint8_t timer_irq = 0;

  hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &tgt_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main_irq);
  hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &timer_irq);

  uint8_t pts = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts);
  uint8_t pta_state = pts & PTA_STATE_MASK;

  if (s_state == T2T_STATE_SLEEP) {
    if (pta_state == PTA_STATE_READY_A || pta_state == PTA_STATE_READY_Ax ||
        pta_state == PTA_STATE_ACTIVE || (tgt_irq & ST25R3916_IRQ_TGT_WU_A)) {
      ESP_LOGI(TAG, "SLEEP SENSE (pta=%d)", pta_state);
      s_state = T2T_STATE_SENSE;
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE);
      hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &tgt_irq);
      hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main_irq);
      hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &timer_irq);
    }
    return;
  }

  if (s_state == T2T_STATE_SENSE) {
    if (pta_state == PTA_STATE_SELECTED || pta_state == PTA_STATE_ACTIVE_STAR ||
        (tgt_irq & ST25R3916_IRQ_TGT_SDD_C)) {
      ESP_LOGI(TAG, "SENSE ACTIVE (pta=%d)", pta_state);
      s_state = T2T_STATE_ACTIVE;
      hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &tgt_irq);
      hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main_irq);
      hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT, &timer_irq);
      if (!(main_irq & ST25R3916_IRQ_MAIN_FWL))
        return;
    } else if (timer_irq & ST25R3916_IRQ_TGT_OSCF) {
      ESP_LOGI(TAG, "SENSE: field lost SLEEP");
      s_state = T2T_STATE_SLEEP;
      t2t_reset_auth();
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
      return;
    } else if (!tgt_irq && !main_irq && !timer_irq) {
      if ((++s_idle_sense % T2T_IDLE_LOG_INTERVAL) == 0U) {
        uint8_t aux = 0;
        hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
        ESP_LOGI(TAG, "[SENSE] AUX=0x%02X pta=%d", aux, pta_state);
      }
      return;
    }
    if (s_state != T2T_STATE_ACTIVE)
      return;
  }

  if (timer_irq & ST25R3916_IRQ_TGT_OSCF) {
    ESP_LOGI(TAG, "ACTIVE: field lost SLEEP");
    s_state = T2T_STATE_SLEEP;
    t2t_reset_auth();
    hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
    return;
  }

  if (!tgt_irq && !main_irq && !timer_irq) {
    if ((++s_idle_active % T2T_IDLE_LOG_INTERVAL) == 0U) {
      uint8_t aux = 0;
      hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
      ESP_LOGI(TAG, "[ACTIVE] AUX=0x%02X pta=%d", aux, pta_state);
    }
    return;
  }

  if (!(main_irq & ST25R3916_IRQ_MAIN_FWL))
    return;

  uint8_t buf[T2T_CMD_BUF_SIZE] = {0};

  uint8_t fs1 = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS1, &fs1);
  uint8_t n = fs1 & FIFO_LEN_MASK;
  if (n == 0)
    return;
  if (n > T2T_CMD_BUF_SIZE)
    n = T2T_CMD_BUF_SIZE;
  hb_nfc_spi_fifo_read(buf, n);
  int len = (int)n;

  ESP_LOGI(TAG, "CMD: 0x%02X len=%d", buf[0], len);

  if (s_comp_write_pending) {
    handle_comp_write_data(buf, len);
    return;
  }

  if (s_ulc_state == ULC_AUTH_WAIT) {
    handle_ulc_auth_step2(buf, len);
    return;
  }

  switch (buf[0]) {
    case T2T_CMD_READ:
      if (len >= 2)
        handle_read(buf[1]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_WRITE:
      if (len >= 6)
        handle_write(buf[1], &buf[2]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_COMP_WRITE:
      if (len >= 2)
        handle_comp_write_start(buf[1]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_GET_VERSION:
      handle_get_version();
      break;
    case T2T_CMD_FAST_READ:
      if (len >= 3)
        handle_fast_read(buf[1], buf[2]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_PWD_AUTH:
      handle_pwd_auth(buf, len);
      break;
    case T2T_CMD_ULC_AUTH:
      handle_ulc_auth_start();
      break;
    case T2T_CMD_HALT:
      ESP_LOGI(TAG, "HALT SLEEP");
      s_state = T2T_STATE_SLEEP;
      t2t_reset_auth();
      hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SLEEP);
      break;
    default:
      ESP_LOGW(TAG, "Cmd unknown: 0x%02X", buf[0]);
      tx_4bit_nack(0x00);
      break;
  }
}

static bool wait_oscillator(int timeout_ms) {
  for (int i = 0; i < timeout_ms; i++) {
    uint8_t aux = 0, mi = 0, ti = 0;
    hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
    hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &mi);
    hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT, &ti);
    if (((aux & AUX_DISPLAY_OSC_MASK) != 0) || ((mi & ST25R3916_IRQ_MAIN_OSC) != 0) ||
        ((ti & ST25R3916_IRQ_TGT_OSCF) != 0)) {
      ESP_LOGI(TAG, "Osc OK in %dms: AUX=0x%02X MAIN=0x%02X TGT=0x%02X", i, aux, mi, ti);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_OSC_POLL_MS));
  }
  ESP_LOGW(TAG, "Osc timeout - continuing");
  return false;
}

static const t2t_profile_t *profile_for_type(t2t_tag_type_t type) {
  for (size_t i = 0; i < sizeof(k_profiles) / sizeof(k_profiles[0]); i++) {
    if (k_profiles[i].type == type)
      return &k_profiles[i];
  }
  return NULL;
}

static void t2t_reset_auth(void) {
  s_authenticated = false;
  s_ulc_state = ULC_AUTH_IDLE;
  s_comp_write_pending = false;
  memset(s_ulc_rndb, 0x00, sizeof(s_ulc_rndb));
  memset(s_ulc_last_iv, 0x00, sizeof(s_ulc_last_iv));
}

static uint16_t get_static_lock_bits(void) {
  return (uint16_t)s_mem[T2T_STATIC_LOCK_BYTE0] | ((uint16_t)s_mem[T2T_STATIC_LOCK_BYTE1] << 8);
}

static uint16_t get_dynamic_lock_bits(void) {
  if (s_prof.dyn_lock_page >= s_page_count)
    return 0;
  size_t off = (size_t)s_prof.dyn_lock_page * T2T_PAGE_SIZE;
  return (uint16_t)s_mem[off] | ((uint16_t)s_mem[off + 1] << 8);
}

static bool is_page_locked(uint16_t page) {
  if (page < T2T_FIRST_LOCKED_PAGE)
    return true;

  uint16_t static_bits = get_static_lock_bits();
  if (page >= T2T_STATIC_LOCK_START && page < T2T_STATIC_LOCK_END) {
    uint8_t bit = (uint8_t)(page - T2T_STATIC_LOCK_START);
    return ((static_bits >> bit) & 0x01U) != 0;
  }

  uint16_t dyn_bits = get_dynamic_lock_bits();
  if (page >= T2T_DYN_LOCK_BASE) {
    int idx = (int)((page - T2T_DYN_LOCK_BASE) / T2T_DYN_LOCK_PAGES_PER_BIT);
    if (idx >= 0 && idx < T2T_DYN_LOCK_BIT_COUNT)
      return ((dyn_bits >> idx) & 0x01U) != 0;
  }
  return false;
}

static bool is_page_protected(uint16_t page) {
  if (s_ulc_auth_enabled)
    return page >= s_prof.user_start;
  if (!s_pwd_enabled || s_auth0 == 0xFF)
    return false;
  return page >= s_auth0;
}

static bool can_read_page(uint16_t page) {
  if (page >= s_page_count)
    return false;
  if (!is_page_protected(page))
    return true;
  if ((s_access & NTAG_ACCESS_PROT) == 0)
    return true;
  return s_authenticated;
}

static bool can_write_page(uint16_t page) {
  if (page >= s_page_count)
    return false;
  if (is_page_locked(page))
    return false;
  if (is_page_protected(page) && !s_authenticated)
    return false;
  return true;
}

static void otp_write_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  for (size_t i = 0; i < len; i++)
    dst[i] |= src[i];
}

static void ulc_random(uint8_t *out, size_t len) {
  for (size_t i = 0; i < len; i += sizeof(uint32_t)) {
    uint32_t r = esp_random();
    size_t left = len - i;
    size_t n = left < sizeof(uint32_t) ? left : sizeof(uint32_t);
    memcpy(&out[i], &r, n);
  }
}

static void rotate_left_8(uint8_t *out, const uint8_t *in) {
  out[0] = in[1];
  out[1] = in[2];
  out[2] = in[3];
  out[3] = in[4];
  out[4] = in[5];
  out[5] = in[6];
  out[6] = in[7];
  out[7] = in[0];
}

static bool
des3_cbc_crypt(const des3_cbc_params_t *p, const uint8_t *in, size_t len, uint8_t *out) {
  if (p == NULL || (len % T2T_DES_BLOCK_SIZE) != 0 || p->key == NULL || p->iv_in == NULL ||
      in == NULL || out == NULL)
    return false;

  mbedtls_des3_context ctx;
  mbedtls_des3_init(&ctx);
  int rc =
      p->encrypt ? mbedtls_des3_set2key_enc(&ctx, p->key) : mbedtls_des3_set2key_dec(&ctx, p->key);
  if (rc != 0) {
    mbedtls_des3_free(&ctx);
    return false;
  }

  uint8_t iv[T2T_DES_BLOCK_SIZE];
  memcpy(iv, p->iv_in, T2T_DES_BLOCK_SIZE);
  rc = mbedtls_des3_crypt_cbc(
      &ctx, p->encrypt ? MBEDTLS_DES_ENCRYPT : MBEDTLS_DES_DECRYPT, len, iv, in, out);
  if (p->iv_out != NULL)
    memcpy(p->iv_out, iv, T2T_DES_BLOCK_SIZE);
  mbedtls_des3_free(&ctx);
  return rc == 0;
}

static hb_nfc_err_t load_pt_memory(void) {
  uint8_t ptm[PT_MEM_SIZE] = {0};

  if (s_uid_len == T2T_UID_LEN_LONG) {
    ptm[0] = s_uid[0];
    ptm[1] = s_uid[1];
    ptm[2] = s_uid[2];
    ptm[3] = s_uid[3];
    ptm[4] = s_uid[4];
    ptm[5] = s_uid[5];
    ptm[6] = s_uid[6];
    ptm[PT_IDX_ATQA_0] = k_atqa[0];
    ptm[PT_IDX_ATQA_1] = k_atqa[1];
    ptm[PT_IDX_CT] = ISO14443A_CT_BYTE;
    ptm[PT_IDX_SAK] = k_sak;
  } else {
    ptm[0] = s_uid[0];
    ptm[1] = s_uid[1];
    ptm[2] = s_uid[2];
    ptm[3] = s_uid[3];
    ptm[PT_IDX_ATQA_0] = k_atqa[0];
    ptm[PT_IDX_ATQA_1] = k_atqa[1];
    ptm[PT_IDX_SAK] = k_sak;
  }

  ESP_LOGI(TAG, "PT Memory (write):");
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, ptm, PT_MEM_SIZE, ESP_LOG_INFO);

  hb_nfc_err_t err = hb_nfc_spi_pt_mem_write(ST25R3916_SPI_PT_MEM_A_WRITE, ptm, PT_MEM_SIZE);
  if (err != HB_NFC_OK) {
    ESP_LOGE(TAG, "PT write fail: %d", err);
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_SHORT_MS));

  uint8_t rb[PT_MEM_SIZE] = {0};
  err = hb_nfc_spi_pt_mem_read(rb, PT_MEM_SIZE);
  if (err != HB_NFC_OK) {
    ESP_LOGE(TAG, "PT readback fail: %d", err);
    return err;
  }
  ESP_LOGI(TAG, "PT Memory (readback):");
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, rb, PT_MEM_SIZE, ESP_LOG_INFO);

  if (memcmp(ptm, rb, PT_MEM_SIZE) != 0) {
    ESP_LOGE(TAG, "PT Memory mismatch!");
    return HB_NFC_ERR_INTERNAL;
  }

  if (s_uid_len == T2T_UID_LEN_LONG) {
    ESP_LOGI(TAG,
             "PT OK: UID=%02X%02X%02X%02X%02X%02X%02X ATQA=%02X%02X SAK=%02X",
             ptm[0],
             ptm[1],
             ptm[2],
             ptm[3],
             ptm[4],
             ptm[5],
             ptm[6],
             ptm[PT_IDX_ATQA_0],
             ptm[PT_IDX_ATQA_1],
             ptm[PT_IDX_SAK]);
  } else {
    ESP_LOGI(TAG,
             "PT OK: UID=%02X%02X%02X%02X ATQA=%02X%02X SAK=%02X",
             ptm[0],
             ptm[1],
             ptm[2],
             ptm[3],
             ptm[PT_IDX_ATQA_0],
             ptm[PT_IDX_ATQA_1],
             ptm[PT_IDX_SAK]);
  }
  return HB_NFC_OK;
}

static void build_memory(const char *text) {
  memset(s_mem, 0x00, s_mem_size);

  uint8_t bcc0 = ISO14443A_CASCADE_TAG ^ s_uid[0] ^ s_uid[1] ^ s_uid[2];
  uint8_t bcc1 = s_uid[3] ^ s_uid[4] ^ s_uid[5] ^ s_uid[6];

  s_mem[0] = s_uid[0];
  s_mem[1] = s_uid[1];
  s_mem[2] = s_uid[2];
  s_mem[3] = bcc0;
  s_mem[4] = s_uid[3];
  s_mem[5] = s_uid[4];
  s_mem[6] = s_uid[5];
  s_mem[7] = s_uid[6];
  s_mem[8] = bcc1;
  s_mem[9] = T2T_INTERNAL_BYTE;

  s_mem[12] = T2T_CC_NDEF_MAGIC;
  s_mem[13] = T2T_CC_VERSION_10;
  s_mem[14] = s_prof.cc_size;
  s_mem[15] = 0x00;

  if (s_prof.supports_pwd) {
    if (s_prof.cfg_page < s_page_count) {
      size_t off = (size_t)s_prof.cfg_page * T2T_PAGE_SIZE;
      if (off + 3 < s_mem_size) {
        s_mem[off + 2] = s_auth0;
        s_mem[off + 3] = s_access;
      }
    }
    if (s_prof.pwd_page < s_page_count) {
      size_t off = (size_t)s_prof.pwd_page * T2T_PAGE_SIZE;
      memcpy(&s_mem[off], s_pwd, sizeof(s_pwd));
    }
    if (s_prof.pack_page < s_page_count) {
      size_t off = (size_t)s_prof.pack_page * T2T_PAGE_SIZE;
      s_mem[off] = s_pack[0];
      s_mem[off + 1] = s_pack[1];
    }
  }

  if (s_prof.supports_ulc_auth) {
    uint16_t key_page = (s_prof.total_pages >= T2T_COMP_WRITE_PAGES)
                            ? (s_prof.total_pages - T2T_COMP_WRITE_PAGES)
                            : 0;
    if (key_page + 3 < s_page_count) {
      size_t off = (size_t)key_page * T2T_PAGE_SIZE;
      memcpy(&s_mem[off], s_ulc_key, sizeof(s_ulc_key));
    }
  }

  if (text == NULL || text[0] == '\0')
    return;

  size_t tl = strlen(text);
  size_t pl = NDEF_TEXT_PREFIX_LEN + tl;
  size_t rl = NDEF_REC_OVERHEAD + pl;
  size_t max_user = (size_t)((s_prof.user_end - s_prof.user_start + 1) * T2T_PAGE_SIZE);
  if (NDEF_TLV_HEADER_LEN + rl + 1 > max_user) {
    ESP_LOGW(TAG, "NDEF too long");
    return;
  }

  size_t p = (size_t)(s_prof.user_start * T2T_PAGE_SIZE);
  s_mem[p++] = NDEF_TLV_MESSAGE;
  s_mem[p++] = (uint8_t)rl;
  s_mem[p++] = NDEF_REC_HDR_TEXT;
  s_mem[p++] = NDEF_TYPE_LEN_TEXT;
  s_mem[p++] = (uint8_t)pl;
  s_mem[p++] = 'T';
  s_mem[p++] = NDEF_TEXT_UTF8_LANG2;
  s_mem[p++] = 'p';
  s_mem[p++] = 't';
  memcpy(&s_mem[p], text, tl);
  p += tl;
  s_mem[p] = NDEF_TLV_TERMINATOR;
}

static void tx_with_crc(const uint8_t *data, int len) {
  if (data == NULL || len <= 0)
    return;
  st25r3916_fifo_clear();
  st25r3916_fifo_set_tx_bytes((uint16_t)len, 0);
  st25r3916_fifo_load(data, (size_t)len);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WITH_CRC);
}

static void tx_4bit_nack(uint8_t code) {
  uint8_t v = code & 0x0F;
  st25r3916_fifo_clear();
  st25r3916_fifo_set_tx_bytes(0, 4);
  st25r3916_fifo_load(&v, 1);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC);
}

static void tx_ack(void) {
  uint8_t ack = T2T_ACK;
  st25r3916_fifo_clear();
  st25r3916_fifo_set_tx_bytes(0, 4);
  st25r3916_fifo_load(&ack, 1);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_TX_WO_CRC);
}

static int rx_poll(uint8_t *buf, int max, uint8_t main_irq) {
  if (main_irq & ST25R3916_IRQ_MAIN_COL)
    return -1;
  if (!(main_irq & ST25R3916_IRQ_MAIN_RXE))
    return 0;
  uint8_t fs1 = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS1, &fs1);
  int n = fs1 & FIFO_LEN_MASK;
  if (n == 0)
    return 0;
  if (n > max)
    n = max;
  hb_nfc_spi_fifo_read(buf, (uint8_t)n);
  return n;
}

static bool write_page_internal(uint8_t page, const uint8_t *data) {
  if (page >= s_page_count)
    return false;
  if (page < T2T_PAGE_UID_END)
    return false;

  if (page == T2T_PAGE_UID_END) {
    size_t off = (size_t)page * T2T_PAGE_SIZE;
    s_mem[off + 2] |= data[2];
    s_mem[off + 3] |= data[3];
    return true;
  }

  if (page == s_prof.dyn_lock_page) {
    size_t off = (size_t)page * T2T_PAGE_SIZE;
    s_mem[off] |= data[0];
    s_mem[off + 1] |= data[1];
    return true;
  }

  if (!can_write_page(page))
    return false;

  size_t off = (size_t)page * T2T_PAGE_SIZE;
  if (page == T2T_PAGE_OTP) {
    otp_write_bytes(&s_mem[off], data, T2T_PAGE_SIZE);
  } else {
    memcpy(&s_mem[off], data, T2T_PAGE_SIZE);
  }
  return true;
}

static void handle_read(uint8_t page) {
  if (page + 3 >= s_page_count) {
    tx_4bit_nack(0x00);
    return;
  }
  for (uint8_t p = page; p < page + T2T_COMP_WRITE_PAGES; p++) {
    if (!can_read_page(p)) {
      tx_4bit_nack(0x00);
      return;
    }
  }

  uint8_t resp[T2T_READ_RESP_SIZE];
  for (uint8_t i = 0; i < T2T_READ_RESP_SIZE; i++) {
    resp[i] = s_mem[(page + (i / T2T_PAGE_SIZE)) * T2T_PAGE_SIZE + (i % T2T_PAGE_SIZE)];
  }
  ESP_LOGI(TAG, "READ pg=%d %02X %02X %02X %02X", page, resp[0], resp[1], resp[2], resp[3]);
  tx_with_crc(resp, T2T_READ_RESP_SIZE);
}

static void handle_fast_read(uint8_t start, uint8_t end) {
  if (end < start || end >= s_page_count) {
    tx_4bit_nack(0x00);
    return;
  }

  uint16_t pages = (uint16_t)(end - start + 1);
  uint16_t len = pages * T2T_PAGE_SIZE;
  if (len > T2T_FAST_READ_MAX_LEN) {
    tx_4bit_nack(0x00);
    return;
  }

  uint8_t resp[T2T_FAST_READ_MAX_LEN];
  uint16_t pos = 0;
  for (uint16_t p = start; p <= end; p++) {
    if (!can_read_page((uint8_t)p)) {
      tx_4bit_nack(0x00);
      return;
    }
    memcpy(&resp[pos], &s_mem[p * T2T_PAGE_SIZE], T2T_PAGE_SIZE);
    pos += T2T_PAGE_SIZE;
  }
  tx_with_crc(resp, (int)len);
}

static void handle_write(uint8_t page, const uint8_t *data) {
  if (!write_page_internal(page, data)) {
    tx_4bit_nack(0x00);
    return;
  }
  ESP_LOGI(TAG, "WRITE pg=%d: %02X %02X %02X %02X", page, data[0], data[1], data[2], data[3]);
  tx_ack();
}

static void handle_comp_write_start(uint8_t page) {
  if (page >= s_page_count) {
    tx_4bit_nack(0x00);
    return;
  }
  s_comp_write_pending = true;
  s_comp_write_page = page;
  tx_ack();
}

static void handle_comp_write_data(const uint8_t *data, int len) {
  if (!s_comp_write_pending)
    return;
  s_comp_write_pending = false;

  if (len < T2T_COMP_WRITE_LEN) {
    tx_4bit_nack(0x00);
    return;
  }
  for (uint8_t i = 0; i < T2T_COMP_WRITE_PAGES; i++) {
    uint8_t page = (uint8_t)(s_comp_write_page + i);
    if (page >= s_page_count) {
      tx_4bit_nack(0x00);
      return;
    }
    if (page != T2T_PAGE_UID_END && page != s_prof.dyn_lock_page && !can_write_page(page)) {
      tx_4bit_nack(0x00);
      return;
    }
  }
  for (uint8_t i = 0; i < T2T_COMP_WRITE_PAGES; i++) {
    if (!write_page_internal((uint8_t)(s_comp_write_page + i), &data[i * T2T_PAGE_SIZE])) {
      tx_4bit_nack(0x00);
      return;
    }
  }
  tx_ack();
}

static void handle_get_version(void) {
  if (!s_prof.supports_get_version) {
    tx_4bit_nack(0x00);
    return;
  }
  tx_with_crc(s_prof.version, sizeof(s_prof.version));
}

static void handle_pwd_auth(const uint8_t *cmd, int len) {
  if (!s_prof.supports_pwd || !s_pwd_enabled) {
    tx_4bit_nack(0x00);
    return;
  }
  if (len < 5) {
    tx_4bit_nack(0x00);
    return;
  }

  if (memcmp(&cmd[1], s_pwd, sizeof(s_pwd)) == 0) {
    s_authenticated = true;
    uint8_t resp[2] = {s_pack[0], s_pack[1]};
    tx_with_crc(resp, sizeof(resp));
    return;
  }
  s_authenticated = false;
  tx_4bit_nack(0x00);
}

static void handle_ulc_auth_start(void) {
  if (!s_prof.supports_ulc_auth || !s_ulc_auth_enabled) {
    tx_4bit_nack(0x00);
    return;
  }
  ulc_random(s_ulc_rndb, sizeof(s_ulc_rndb));
  uint8_t iv[T2T_DES_BLOCK_SIZE] = {0};
  uint8_t enc[T2T_DES_BLOCK_SIZE] = {0};
  if (!des3_cbc_crypt(
          &(des3_cbc_params_t){
              .encrypt = true, .key = s_ulc_key, .iv_in = iv, .iv_out = s_ulc_last_iv},
          s_ulc_rndb,
          sizeof(s_ulc_rndb),
          enc)) {
    tx_4bit_nack(0x00);
    return;
  }
  s_ulc_state = ULC_AUTH_WAIT;
  tx_with_crc(enc, sizeof(enc));
}

static void handle_ulc_auth_step2(const uint8_t *data, int len) {
  if (len < T2T_COMP_WRITE_LEN) {
    tx_4bit_nack(0x00);
    s_ulc_state = ULC_AUTH_IDLE;
    return;
  }

  uint8_t plain[T2T_ULC_KEY_LEN] = {0};
  if (!des3_cbc_crypt(
          &(des3_cbc_params_t){
              .encrypt = false, .key = s_ulc_key, .iv_in = s_ulc_last_iv, .iv_out = NULL},
          data,
          T2T_COMP_WRITE_LEN,
          plain)) {
    tx_4bit_nack(0x00);
    s_ulc_state = ULC_AUTH_IDLE;
    return;
  }

  uint8_t rndb_rot[T2T_DES_BLOCK_SIZE];
  rotate_left_8(rndb_rot, s_ulc_rndb);
  if (memcmp(&plain[T2T_DES_BLOCK_SIZE], rndb_rot, T2T_DES_BLOCK_SIZE) != 0) {
    tx_4bit_nack(0x00);
    s_ulc_state = ULC_AUTH_IDLE;
    return;
  }

  uint8_t rnda_rot[T2T_DES_BLOCK_SIZE];
  rotate_left_8(rnda_rot, plain);
  uint8_t iv_enc[T2T_DES_BLOCK_SIZE];
  memcpy(iv_enc, &data[T2T_DES_BLOCK_SIZE], T2T_DES_BLOCK_SIZE);
  uint8_t enc[T2T_DES_BLOCK_SIZE] = {0};
  if (!des3_cbc_crypt(
          &(des3_cbc_params_t){.encrypt = true, .key = s_ulc_key, .iv_in = iv_enc, .iv_out = NULL},
          rnda_rot,
          T2T_DES_BLOCK_SIZE,
          enc)) {
    tx_4bit_nack(0x00);
    s_ulc_state = ULC_AUTH_IDLE;
    return;
  }

  s_authenticated = true;
  s_ulc_state = ULC_AUTH_IDLE;
  tx_with_crc(enc, sizeof(enc));
}
