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
#include "t2t_aes.h"

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
#include "iso14443a.h"
#include "hb_nfc_timer.h"

static const char *TAG = "NFC_T2T_EMU";

#define T2T_ULC_ALL_KEY_BYTES    UINT16_MAX
#define T2T_ULC_COUNTER_MAX      UINT16_MAX
#define T2T_ULC_AUTH0_PAGE       42U
#define T2T_ULC_AUTH1_PAGE       43U
#define T2T_ULC_KEY_PAGE         44U
#define T2T_ULC_AUTH_DISABLED    48U
#define T2T_ULC_READ_FREE        1U
#define T2T_ULC_LOCK_GROUP_PAGES 4U
#define T2T_ULC_LOCK_PAGE        40U
#define T2T_ULC_COUNTER_PAGE     41U
#define T2T_ULC_CONFIG_LOCK_BASE 4U
#define T2T_ULC_LOCK_LOW_MASK    0x0EU
#define T2T_ULC_LOCK_HIGH_MASK   0xE0U
#define T2T_ULC_LOCK_HIGH_FREEZE 0x10U
#define T2T_PAGE_SIZE            4
#define T2T_MAX_PAGES            512
#define T2T_MEM_SIZE             (T2T_PAGE_SIZE * T2T_MAX_PAGES)

#define T2T_CMD_INCR_CNT               0xA5U
#define T2T_CMD_CHECK_TEARING          0x3EU
#define T2T_CMD_VCSL                   0x4BU
#define T2T_INCR_FRAME_LENGTH          6U
#define T2T_VCSL_FRAME_LENGTH          21U
#define T2T_TEARING_OK                 0xBDU
#define T2T_UL_PAGES                   16U
#define T2T_EV11_PAGES                 20U
#define T2T_EV21_PAGES                 41U
#define T2T_EV11_CONFIG                16U
#define T2T_EV21_CONFIG                37U
#define T2T_CFG_VCTID_OFFSET           5U
#define T2T_CMD_READ_SIG               0x3CU
#define T2T_CMD_WRITE_SIG              0xA9U
#define T2T_CMD_LOCK_SIG               0xACU
#define T2T_SIGNATURE_WRITE_LENGTH     6U
#define T2T_SIGNATURE_LOCK_LENGTH      2U
#define T2T_SIGNATURE_ACTION_UNLOCK    0U
#define T2T_SIGNATURE_ACTION_LOCK      1U
#define T2T_SIGNATURE_ACTION_PERMANENT 2U
#define T2T_CMD_READ_CNT               0x39U
#define T2T_COUNTER_INDEX              2U
#define T2T_COUNTER_BYTES              3U
#define T2T_COUNTER_ENABLE             0x10U
#define T2T_COUNTER_PROTECTED          0x08U
#define T2T_BYTE_BITS                  8U
#define T2T_MIRROR_MODE_SHIFT          6U
#define T2T_MIRROR_OFFSET_SHIFT        4U
#define T2T_MIRROR_MASK                3U
#define T2T_MIRROR_UID                 1U
#define T2T_MIRROR_COUNTER             2U
#define T2T_MIRROR_BOTH                3U
#define T2T_MIRROR_UID_LENGTH          14U
#define T2T_MIRROR_COUNTER_LENGTH      6U
#define T2T_MIRROR_LENGTH              21U
#define T2T_MIRROR_PAGE_OFFSET         2U
#define T2T_HEX_SHIFT                  4U
#define T2T_CMD_READ                   0x30
#define T2T_CMD_WRITE                  0xA2
#define T2T_CMD_COMP_WRITE             0xA0
#define T2T_CMD_GET_VERSION            0x60
#define T2T_CMD_FAST_READ              0x3A
#define T2T_CMD_PWD_AUTH               0x1B
#define T2T_CMD_ULC_AUTH               0x1A
#define T2T_CMD_HALT                   0x50

#define T2T_ACK                  0x0A
#define T2T_CRC_BYTES            2U
#define T2T_FIFO_INVALID_MASK    0x3FU
#define T2T_TX_BYTE_SHIFT        5U
#define T2T_TX_BIT_SHIFT         3U
#define T2T_TX_LOW_MASK          31U
#define T2T_TX_POLL_COUNT        300U
#define T2T_TX_POLL_US           100U
#define T2T_NIBBLE_BITS          4U
#define T2T_NIBBLE_MASK          0x0FU
#define T2T_CFG_AUTH0_OFFSET     3U
#define T2T_NTAG213_LOCK_STEP    2U
#define T2T_NTAG_LARGE_LOCK_STEP 16U
#define T2T_ULC_MORE             0xAFU

#define T2T_CONFIG_LOCK             0x40U
#define T2T_STATIC_LOCK_OTP_MASK    0x0008U
#define T2T_STATIC_LOCK_LOW_MASK    0x03F0U
#define T2T_STATIC_LOCK_HIGH_MASK   0xFC00U
#define T2T_STATIC_LOCK_OTP_FREEZE  1U
#define T2T_STATIC_LOCK_LOW_FREEZE  2U
#define T2T_STATIC_LOCK_HIGH_FREEZE 4U
#define T2T_DYNAMIC_FREEZE_BITS     8U
#define T2T_DYNAMIC_LOCK_BYTES      3U
#define T2T_AUTH_LIMIT_MASK         0x07U
#define T2T_PWD_FRAME_LENGTH        5U
#define T2T_NACK_AUTH_LIMIT         0x04U
#define NTAG_ACCESS_PROT            0x80

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
#define T2T_CMD_BUF_SIZE      64

#define T2T_STATIC_LOCK_BYTE0      10
#define T2T_STATIC_LOCK_BYTE1      11
#define T2T_FIRST_LOCKED_PAGE      2
#define T2T_STATIC_LOCK_START      3
#define T2T_STATIC_LOCK_END        16
#define T2T_DYN_LOCK_BASE          16
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

#define T2T_AES_RID_LENGTH 4U
#define T2T_AES_RID_PREFIX 0x08U
#define T2T_AES_SHORT_ATQA 0x04U

#define T2T_AES_PAGES                60U
#define T2T_AES_CFG_PAGE             41U
#define T2T_AES_ACCESS_PAGE          42U
#define T2T_AES_LOCK_PAGE            45U
#define T2T_AES_KEY_PAGE             48U
#define T2T_AES_KEY_END              56U
#define T2T_AES_SECURE_MASK          0x02U
#define T2T_AES_RID_MASK             0x01U
#define T2T_AES_AUTH_LIMIT_HIGH_MASK 0x03U
#define T2T_AES_AUTH_RECOVERY        16U
#define T2T_AES_KEY_LOCK_BASE        6U
#define T2T_AES_KEY_LOCK_FREEZE      0x20U
#define T2T_AES_COUNTER_READ         0x04U
#define T2T_AES_COUNTER_INCREMENT    0x08U

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

#define T2T_I2C_SECTOR_SELECT  0xC2U
#define T2T_I2C_SELECT_MARKER  0xFFU
#define T2T_I2C_SECTOR_PAGES   256U
#define T2T_I2C_SESSION_SECTOR 3U
#define T2T_I2C_SESSION_PAGE   0xF8U
#define T2T_I2C_CONFIG_PAGE    0xE8U
#define T2T_I2C_PLUS_SESSION   0xECU
#define T2T_I2C_PLUS_BASE      236U
#define T2T_I2C_LOCK_PAGE      0xE2U
#define T2T_I2C_PT_PAGE        0xE7U
#define T2T_I2C_AUTH_END       0xEBU
#define T2T_I2C_DISABLE_SECTOR 0x20U
#define T2T_I2C_PROTECT_SECTOR 0x08U
#define T2T_I2C_READ_PROTECT   0x80U
#define T2T_I2C_LOCK_STEP_1K   16U
#define T2T_I2C_LOCK_STEP_2K   32U
#define T2T_I2C_LOCK_BITS      16U

static const t2t_profile_t k_profiles[] = {
    {.type = T2T_TAG_NTAG_I2C_1K,
     .total_pages = 231,
     .user_start = 4,
     .user_end = 225,
     .dyn_lock_page = 226,
     .supports_get_version = true},
    {.type = T2T_TAG_NTAG_I2C_2K,
     .total_pages = 485,
     .user_start = 4,
     .user_end = 479,
     .dyn_lock_page = 480,
     .supports_get_version = true},
    {.type = T2T_TAG_NTAG_I2C_PLUS_1K,
     .total_pages = 236,
     .user_start = 4,
     .user_end = 225,
     .dyn_lock_page = 226,
     .cfg_page = 227,
     .pwd_page = 229,
     .pack_page = 230,
     .supports_pwd = true,
     .supports_get_version = true},
    {.type = T2T_TAG_NTAG_I2C_PLUS_2K,
     .total_pages = 492,
     .user_start = 4,
     .user_end = 225,
     .dyn_lock_page = 226,
     .cfg_page = 227,
     .pwd_page = 229,
     .pack_page = 230,
     .supports_pwd = true,
     .supports_get_version = true},
    {
        .type = T2T_TAG_ULTRALIGHT_AES,
        .total_pages = T2T_AES_PAGES,
        .user_start = 4,
        .user_end = 39,
        .dyn_lock_page = 40,
        .cfg_page = T2T_AES_CFG_PAGE,
        .supports_get_version = true,
    },
    {
        .type = T2T_TAG_ULTRALIGHT,
        .total_pages = T2T_UL_PAGES,
        .user_start = T2T_COMP_WRITE_PAGES,
        .user_end = T2T_UL_PAGES - 1,
        .dyn_lock_page = T2T_MAX_PAGES,
    },
    {
        .type = T2T_TAG_ULTRALIGHT_EV1_11,
        .total_pages = T2T_EV11_PAGES,
        .user_start = T2T_COMP_WRITE_PAGES,
        .user_end = T2T_EV11_CONFIG - 1,
        .dyn_lock_page = T2T_MAX_PAGES,
        .cfg_page = T2T_EV11_CONFIG,
        .pwd_page = T2T_EV11_PAGES - 2,
        .pack_page = T2T_EV11_PAGES - 1,
        .supports_get_version = true,
        .supports_pwd = true,
    },
    {
        .type = T2T_TAG_ULTRALIGHT_EV1_21,
        .total_pages = T2T_EV21_PAGES,
        .user_start = T2T_COMP_WRITE_PAGES,
        .user_end = T2T_EV21_CONFIG - 2,
        .dyn_lock_page = T2T_EV21_CONFIG - 1,
        .cfg_page = T2T_EV21_CONFIG,
        .pwd_page = T2T_EV21_PAGES - 2,
        .pack_page = T2T_EV21_PAGES - 1,
        .supports_get_version = true,
        .supports_pwd = true,
    },
    {
        .type = T2T_TAG_NTAG210,
        .total_pages = T2T_EV11_PAGES,
        .user_start = T2T_COMP_WRITE_PAGES,
        .user_end = T2T_EV11_CONFIG - 1,
        .dyn_lock_page = T2T_MAX_PAGES,
        .cfg_page = T2T_EV11_CONFIG,
        .pwd_page = T2T_EV11_PAGES - 2,
        .pack_page = T2T_EV11_PAGES - 1,
        .supports_pwd = true,
        .supports_get_version = true,
        .cc_size = 0x06,
        .version = {0, 4, 4, 1, 1, 0, 0x0B, 3},
    },
    {
        .type = T2T_TAG_NTAG212,
        .total_pages = T2T_EV21_PAGES,
        .user_start = T2T_COMP_WRITE_PAGES,
        .user_end = T2T_EV21_CONFIG - 2,
        .dyn_lock_page = T2T_EV21_CONFIG - 1,
        .cfg_page = T2T_EV21_CONFIG,
        .pwd_page = T2T_EV21_PAGES - 2,
        .pack_page = T2T_EV21_PAGES - 1,
        .supports_pwd = true,
        .supports_get_version = true,
        .cc_size = 0x10,
        .version = {0, 4, 4, 1, 1, 0, 0x0E, 3},
    },
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
        .user_end = 39,
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
static t2t_emu_config_t s_config;

static uint8_t s_uid[T2T_UID_LEN_LONG];
static uint8_t s_uid_len = T2T_UID_LEN_LONG;

static bool s_pwd_known;
static bool s_pack_known;
static bool s_pwd_enabled = false;
static uint32_t s_failed_auth_attempts;
static uint8_t s_pwd[4];
static uint8_t s_pack[2];
static uint8_t s_auth0 = 0xFF;
static uint8_t s_access = 0x00;

static bool s_authenticated = false;
static ulc_auth_state_t s_ulc_state = ULC_AUTH_IDLE;
static bool s_ulc_auth_enabled = false;
static uint8_t s_ulc_key[T2T_ULC_KEY_LEN];
static uint16_t s_ulc_key_known;
static bool s_ulc_key_pending;
static bool s_ulc_counter_pending;
static uint16_t s_ulc_counter_next;
static uint8_t s_ulc_rndb[T2T_DES_BLOCK_SIZE];
static uint8_t s_ulc_last_iv[T2T_DES_BLOCK_SIZE];

static bool s_comp_write_pending = false;
static uint8_t s_comp_write_page = 0;

static t2t_state_t s_state = T2T_STATE_SLEEP;
static bool s_init_done = false;
static bool s_transport_error;
static uint8_t s_i2c_sector;
static bool s_i2c_select_pending;
static bool s_read_only;
static bool s_has_signature;
static uint8_t s_signature[T2T_SIGNATURE_SIZE];
static bool s_has_counter;
static uint32_t s_counter;
static bool s_counter_increased;
static uint32_t s_counters[T2T_COUNTER_COUNT];
static uint8_t s_counter_known;
static uint8_t s_tearing[T2T_COUNTER_COUNT];
static uint8_t s_tearing_known;
static uint8_t s_pending_main;
static t2t_aes_session_t s_aes_session;
static uint8_t s_aes_key_number;
static bool s_aes_has_rid;
static bool s_aes_field_seen;
static uint16_t s_aes_key_known[T2T_AES_KEY_COUNT];
static bool s_has_aes_signature;
static uint16_t s_aes_signature_known;
static t2t_signature_lock_t s_aes_signature_lock;
static uint8_t s_aes_signature[T2T_AES_SIGNATURE_SIZE];

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
static bool is_aes(void);
static bool is_i2c(void);
static void i2c_select(const uint8_t *frame, size_t length);
static bool i2c_address(uint8_t sector, uint16_t page, uint16_t *out_page);
static bool i2c_write(uint8_t page, const uint8_t *data);
static void i2c_read(uint8_t start, uint8_t end, bool is_fast);
static void aes_identity(void);
static void handle_aes_auth(const uint8_t *frame, size_t length);
static void t2t_reset_auth(void);
static void ulc_activate(bool is_field_reset);
static bool ulc_write_counter(const uint8_t *data);
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

static bool write_page_internal(uint8_t page, const uint8_t *data);
static void handle_read(uint8_t page);
static bool is_ntag(void);
static bool is_ev1(void);
static void handle_increment(const uint8_t *cmd);
static void handle_tearing(uint8_t index);
static bool can_read_counter(void);
static void increase_counter(void);
static void apply_mirror(uint16_t page, uint8_t *out);
static void handle_counter(uint8_t index);
static void handle_signature(uint8_t parameter, size_t length);
static void handle_write_signature(const uint8_t *frame, size_t length);
static void handle_lock_signature(const uint8_t *frame, size_t length);
static void handle_vcsl(size_t length);
static void handle_fast_read(uint8_t start, uint8_t end);
static void handle_write(uint8_t page, const uint8_t *data);
static void handle_comp_write_start(uint8_t page);
static void handle_comp_write_data(const uint8_t *data, int len);
static void handle_get_version(void);
static void handle_pwd_auth(const uint8_t *cmd, int len);
static void handle_ulc_auth_start(void);
static void handle_ulc_auth_step2(const uint8_t *data, int len);

hb_nfc_err_t t2t_emu_init(const t2t_emu_config_t *cfg) {
  if (cfg == NULL || (cfg->aes_signature_known & ~T2T_AES_SIGNATURE_KNOWN) ||
      (unsigned)cfg->aes_signature_lock >= T2T_SIGNATURE_LOCK_COUNT)
    return HB_NFC_ERR_PARAM;

  const t2t_profile_t *prof = profile_for_type(cfg->type);
  if (prof == NULL)
    return HB_NFC_ERR_PARAM;

  memcpy(s_aes_key_known, cfg->aes_key_known, sizeof(s_aes_key_known));
  s_aes_signature_known =
      cfg->has_aes_signature ? T2T_AES_SIGNATURE_KNOWN : cfg->aes_signature_known;
  s_has_aes_signature = s_aes_signature_known == T2T_AES_SIGNATURE_KNOWN;
  s_aes_signature_lock = cfg->aes_signature_lock;
  memcpy(s_aes_signature, cfg->aes_signature, sizeof(s_aes_signature));
  s_aes_field_seen = false;
  s_aes_has_rid = false;
  s_config = *cfg;
  s_config.ndef_text = NULL;
  s_pending_main = 0;
  s_transport_error = false;
  s_read_only = false;
  s_i2c_sector = 0;
  s_i2c_select_pending = false;
  s_prof = *prof;
  if ((is_ev1() || is_aes() || is_i2c()) && !cfg->has_version)
    return HB_NFC_ERR_PARAM;
  if (is_i2c() && !t2t_i2c_version_matches(cfg->type, cfg->version))
    return HB_NFC_ERR_PARAM;
  for (size_t i = 0; i < T2T_COUNTER_COUNT; i++)
    if (cfg->counters[i] > T2T_COUNTER_MAX)
      return HB_NFC_ERR_PARAM;
  memcpy(s_counters, cfg->counters, sizeof(s_counters));
  s_counter_known = cfg->counter_known;
  memcpy(s_tearing, cfg->tearing, sizeof(s_tearing));
  s_tearing_known = cfg->tearing_known;
  if (cfg->has_counter && cfg->counter > T2T_COUNTER_MAX)
    return HB_NFC_ERR_PARAM;
  s_has_signature = cfg->has_signature;
  memcpy(s_signature, cfg->signature, sizeof(s_signature));
  s_has_counter = cfg->has_counter && is_ntag();
  s_counter = cfg->counter;
  s_counter_increased = false;
  if (cfg->has_version)
    memcpy(s_prof.version, cfg->version, sizeof(s_prof.version));
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

  s_failed_auth_attempts = cfg->failed_auth_attempts;
  s_pwd_enabled = cfg->enable_pwd && s_prof.supports_pwd;
  s_pwd_known = s_pwd_enabled || cfg->pwd_known;
  s_pack_known = s_pwd_enabled || cfg->pack_known;
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
  s_ulc_key_known = s_ulc_auth_enabled ? T2T_ULC_ALL_KEY_BYTES : cfg->ulc_key_known;
  s_ulc_key_pending = false;
  s_ulc_counter_pending = false;
  s_ulc_counter_next = 0;
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

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_OFF) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_MEDIUM_MS));
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_SET_DEFAULT) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_LONG_MS));

  if (hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, T2T_BIT_RATE_RX_212) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_SHORT_MS));

  uint8_t ic = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &ic) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (ic == IC_IDENTITY_NOT_RESPONDING_LO || ic == IC_IDENTITY_NOT_RESPONDING_HI) {
    ESP_LOGE(TAG, "Chip not responding: IC=0x%02X", ic);
    return HB_NFC_ERR_INTERNAL;
  }
  ESP_LOGI(TAG, "Chip OK: IC=0x%02X", ic);

  if (hb_nfc_spi_reg_write(ST25R3916_REG_IO_CONF2, T2T_IO_CONF2_SUP_AAT) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_SHORT_MS));

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_EN) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (!wait_oscillator(T2T_OSC_TIMEOUT_MS))
    return HB_NFC_ERR_TIMEOUT;

  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_LONG_MS));

  if (hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_TARGET_NFCA) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  if (hb_nfc_spi_reg_write(ST25R3916_REG_AUX_DEF,
                           (s_uid_len == T2T_UID_LEN_LONG) ? T2T_AUX_DEF_NFC_N : 0x00) != HB_NFC_OK)
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

  if (hb_nfc_spi_reg_write(ST25R3916_REG_PT_MOD, T2T_PT_MOD_OOK) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  hb_nfc_err_t err = load_pt_memory();
  if (err != HB_NFC_OK)
    return err;

  {
    st25r3916_irq_status_t s;
    if (st25r3916_irq_read(&s) != HB_NFC_OK)
      return HB_NFC_ERR_INTERNAL;
  }

  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_MAIN_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TIMER_NFC_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_ERROR_WUP_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_write(ST25R3916_REG_MASK_TARGET_INT, 0x00) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;

  uint8_t mode_rb = 0, aux_rb = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_MODE, &mode_rb) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DEF, &aux_rb) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
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
    if (st25r3916_irq_read(&s) != HB_NFC_OK)
      return HB_NFC_ERR_INTERNAL;
  }

  if (hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, OP_CTRL_TARGET) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_MEDIUM_MS));

  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  vTaskDelay(pdMS_TO_TICKS(T2T_DELAY_SHORT_MS));

  s_state = T2T_STATE_SENSE;
  s_i2c_sector = 0;
  t2t_reset_auth();

  uint8_t op = 0, aux = 0, pts = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &op) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts) != HB_NFC_OK)
    return HB_NFC_ERR_INTERNAL;
  ESP_LOGI(TAG, "Start (SENSE): OP=0x%02X AUX=0x%02X PT_STS=0x%02X", op, aux, pts);
  ESP_LOGI(TAG, "T2T emulation active - present the phone!");
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

hb_nfc_err_t t2t_emu_snapshot(t2t_emu_config_t *out_config, uint8_t *out_memory, size_t length) {
  if (!s_init_done || out_config == NULL || out_memory == NULL || length != s_mem_size)
    return HB_NFC_ERR_PARAM;
  ulc_activate(true);
  *out_config = s_config;
  memcpy(out_memory, s_mem, length);
  memcpy(out_config->aes_key_known, s_aes_key_known, sizeof(s_aes_key_known));
  out_config->has_aes_signature = s_has_aes_signature;
  out_config->aes_signature_known = s_aes_signature_known;
  out_config->aes_signature_lock = s_aes_signature_lock;
  memcpy(out_config->aes_signature, s_aes_signature, sizeof(s_aes_signature));
  out_config->ndef_text = NULL;
  out_config->failed_auth_attempts = s_failed_auth_attempts;
  out_config->enable_pwd = s_pwd_enabled;
  out_config->pwd_known = s_pwd_known;
  out_config->pack_known = s_pack_known;
  memcpy(out_config->pwd, s_pwd, sizeof(s_pwd));
  memcpy(out_config->pack, s_pack, sizeof(s_pack));
  out_config->auth0 = s_auth0;
  out_config->access = s_access;
  out_config->enable_ulc_auth = s_ulc_auth_enabled;
  out_config->ulc_key_known = s_ulc_key_known;
  memcpy(out_config->ulc_key, s_ulc_key, sizeof(s_ulc_key));
  out_config->counter_known = s_counter_known;
  memcpy(out_config->counters, s_counters, sizeof(s_counters));
  out_config->has_counter = s_has_counter;
  out_config->counter = s_counter;
  if (s_has_counter) {
    out_config->counter_known |= 1U << (T2T_COUNTER_COUNT - 1);
    out_config->counters[T2T_COUNTER_COUNT - 1] = s_counter;
  }
  out_config->tearing_known = s_tearing_known;
  memcpy(out_config->tearing, s_tearing, sizeof(s_tearing));
  return HB_NFC_OK;
}

bool t2t_emu_has_error(void) {
  return s_transport_error;
}

hb_nfc_err_t t2t_emu_load_memory(const uint8_t *memory, size_t length, bool read_only) {
  if (!s_init_done || memory == NULL || length != s_mem_size)
    return HB_NFC_ERR_PARAM;
  memcpy(s_mem, memory, length);
  s_read_only = read_only;
  if (is_aes()) {
    aes_identity();
    s_auth0 = s_mem[T2T_AES_CFG_PAGE * T2T_PAGE_SIZE + T2T_CFG_AUTH0_OFFSET];
    s_access = s_mem[T2T_AES_ACCESS_PAGE * T2T_PAGE_SIZE];
  }
  if (s_prof.supports_pwd) {
    s_auth0 = s_mem[s_prof.cfg_page * T2T_PAGE_SIZE + T2T_CFG_AUTH0_OFFSET];
    s_access = s_mem[(s_prof.cfg_page + 1) * T2T_PAGE_SIZE];
  }
  if (s_prof.supports_pwd) {
    if (s_pwd_known)
      memcpy(s_pwd, memory + s_prof.pwd_page * T2T_PAGE_SIZE, sizeof(s_pwd));
    if (s_pack_known)
      memcpy(s_pack, memory + s_prof.pack_page * T2T_PAGE_SIZE, sizeof(s_pack));
    s_pwd_enabled = s_pwd_known && s_pack_known;
  }
  return HB_NFC_OK;
}

void t2t_emu_run_step(void) {
  if (!s_init_done || s_transport_error)
    return;
  st25r3916_irq_status_t irq;
  uint8_t pts = 0, aux = 0;
  if (st25r3916_irq_read(&irq) != HB_NFC_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_PASSIVE_TARGET_STS, &pts) != HB_NFC_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux) != HB_NFC_OK) {
    s_transport_error = true;
    return;
  }
  irq.main |= s_pending_main;
  s_pending_main = 0;
  if (!(aux & ST25R3916_AUX_DISP_EFD_BIT)) {
    s_i2c_sector = 0;
    s_i2c_select_pending = false;
    ulc_activate(true);
    if (is_aes()) {
      if (s_aes_field_seen) {
        s_aes_field_seen = false;
        aes_identity();
        if (hb_nfc_spi_reg_write(ST25R3916_REG_AUX_DEF, s_aes_has_rid ? 0 : T2T_AUX_DEF_NFC_N) !=
                HB_NFC_OK ||
            load_pt_memory() != HB_NFC_OK) {
          s_transport_error = true;
          return;
        }
        t2t_reset_auth();
      }
      s_auth0 = s_mem[T2T_AES_CFG_PAGE * T2T_PAGE_SIZE + T2T_CFG_AUTH0_OFFSET];
      s_access = s_mem[T2T_AES_ACCESS_PAGE * T2T_PAGE_SIZE];
    }
    s_counter_increased = false;
    if (s_state != T2T_STATE_SENSE) {
      t2t_reset_auth();
      s_state = T2T_STATE_SENSE;
      s_transport_error = hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE) != HB_NFC_OK;
    }
    return;
  }
  if (is_aes())
    s_aes_field_seen = true;
  if ((irq.target & ST25R3916_IRQ_TGT_SDD_C) != 0) {
    ulc_activate(false);
    t2t_reset_auth();
    s_i2c_sector = 0;
    s_i2c_select_pending = false;
    s_state = T2T_STATE_ACTIVE;
  }
  if (!(irq.main & ST25R3916_IRQ_MAIN_RXE))
    return;
  uint8_t fs1 = 0, fs2 = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS1, &fs1) != HB_NFC_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_FIFO_STATUS2, &fs2) != HB_NFC_OK) {
    s_transport_error = true;
    return;
  }
  uint8_t buf[T2T_CMD_BUF_SIZE];
  size_t count = fs1 | ((size_t)(fs2 & ST25R3916_FIFO_STATUS2_MSB_MASK) << 2);
  if (count <= T2T_CRC_BYTES || count > sizeof(buf) || (fs2 & T2T_FIFO_INVALID_MASK) != 0 ||
      irq.error != 0) {
    st25r3916_fifo_clear();
    t2t_reset_auth();
    return;
  }
  if (hb_nfc_spi_fifo_read(buf, count) != HB_NFC_OK) {
    s_transport_error = true;
    return;
  }
  if (!iso14443a_check_crc(buf, count)) {
    t2t_reset_auth();
    return;
  }
  int len = (int)(count - T2T_CRC_BYTES);
  if (s_i2c_select_pending) {
    i2c_select(buf, (size_t)len);
    return;
  }
  if (is_aes() && (buf[0] == T2T_CMD_ULC_AUTH || s_aes_session.is_pending)) {
    handle_aes_auth(buf, (size_t)len);
    return;
  }
  if (is_aes() && s_aes_session.is_secure) {
    size_t plain_length;
    if (t2t_aes_verify(&s_aes_session, buf, (size_t)len, &plain_length) != ESP_OK) {
      tx_4bit_nack(0);
      return;
    }
    if (plain_length == 0) {
      t2t_reset_auth();
      tx_4bit_nack(0);
      return;
    }
    len = (int)plain_length;
  }
  if (s_comp_write_pending) {
    handle_comp_write_data(buf, len);
    return;
  }

  if (s_ulc_state == ULC_AUTH_WAIT) {
    handle_ulc_auth_step2(buf, len);
    return;
  }

  switch (buf[0]) {
    case T2T_I2C_SECTOR_SELECT:
      if (is_i2c() && len == 2 && buf[1] == T2T_I2C_SELECT_MARKER) {
        s_i2c_select_pending = true;
        tx_ack();
      } else
        tx_4bit_nack(0);
      break;
    case T2T_CMD_READ:
      if (len == 2)
        handle_read(buf[1]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_WRITE:
      if (len == 6)
        handle_write(buf[1], &buf[2]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_COMP_WRITE:
      if (len == 2)
        handle_comp_write_start(buf[1]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_WRITE_SIG:
      handle_write_signature(buf, (size_t)len);
      break;
    case T2T_CMD_LOCK_SIG:
      handle_lock_signature(buf, (size_t)len);
      break;
    case T2T_CMD_READ_SIG:
      handle_signature(len > 1 ? buf[1] : 0, (size_t)len);
      break;
    case T2T_CMD_INCR_CNT:
      if (len == T2T_INCR_FRAME_LENGTH)
        handle_increment(buf);
      else
        tx_4bit_nack(0);
      break;
    case T2T_CMD_CHECK_TEARING:
      if (len == 2)
        handle_tearing(buf[1]);
      else
        tx_4bit_nack(0);
      break;
    case T2T_CMD_VCSL:
      handle_vcsl((size_t)len);
      break;
    case T2T_CMD_READ_CNT:
      if (len == 2)
        handle_counter(buf[1]);
      else
        tx_4bit_nack(0);
      break;
    case T2T_CMD_GET_VERSION:
      if (len == 1)
        handle_get_version();
      else
        tx_4bit_nack(0);
      break;
    case T2T_CMD_FAST_READ:
      if (len == 3)
        handle_fast_read(buf[1], buf[2]);
      else
        tx_4bit_nack(0x00);
      break;
    case T2T_CMD_PWD_AUTH:
      handle_pwd_auth(buf, len);
      break;
    case T2T_CMD_ULC_AUTH:
      if (len == 2 && buf[1] == 0)
        handle_ulc_auth_start();
      else {
        s_authenticated = false;
        s_ulc_state = ULC_AUTH_IDLE;
        tx_4bit_nack(0);
      }
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

static void aes_identity(void) {
  s_aes_has_rid = (s_mem[T2T_AES_CFG_PAGE * T2T_PAGE_SIZE] & T2T_AES_RID_MASK) != 0;
  if (s_aes_has_rid) {
    s_uid_len = T2T_AES_RID_LENGTH;
    s_uid[0] = T2T_AES_RID_PREFIX;
    esp_fill_random(s_uid + 1, T2T_AES_RID_LENGTH - 1);
  } else {
    s_uid_len = T2T_UID_LEN_LONG;
    memcpy(s_uid, s_config.uid, sizeof(s_uid));
  }
}

static bool is_aes(void) {
  return s_prof.type == T2T_TAG_ULTRALIGHT_AES;
}

static void t2t_reset_auth(void) {
  s_i2c_select_pending = false;
  t2t_aes_reset(&s_aes_session);
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
    uint8_t bit = (uint8_t)page;
    return ((static_bits >> bit) & 0x01U) != 0;
  }

  if (s_prof.supports_ulc_auth) {
    uint16_t bits = get_dynamic_lock_bits();
    if (page >= T2T_DYN_LOCK_BASE && page <= s_prof.user_end) {
      uint8_t group = (page - T2T_DYN_LOCK_BASE) / T2T_ULC_LOCK_GROUP_PAGES;
      uint8_t bit = group + 1 + (group >= T2T_ULC_CONFIG_LOCK_BASE - 1);
      return (bits & (1U << bit)) != 0;
    }
    if (page >= T2T_ULC_COUNTER_PAGE && page < s_page_count) {
      uint8_t group = page >= T2T_ULC_KEY_PAGE ? T2T_ULC_KEY_PAGE : page;
      uint8_t bit = T2T_BYTE_BITS + T2T_ULC_CONFIG_LOCK_BASE + group - T2T_ULC_COUNTER_PAGE;
      return (bits & (1U << bit)) != 0;
    }
    return false;
  }
  uint16_t dyn_bits = get_dynamic_lock_bits();
  if (page >= T2T_DYN_LOCK_BASE && page <= s_prof.user_end) {
    uint16_t step =
        (s_prof.type == T2T_TAG_NTAG213 || s_prof.type == T2T_TAG_NTAG212 || is_ev1() || is_aes())
            ? T2T_NTAG213_LOCK_STEP
            : T2T_NTAG_LARGE_LOCK_STEP;
    int idx = (int)((page - T2T_DYN_LOCK_BASE) / step);
    if (idx >= 0 && idx < T2T_DYN_LOCK_BIT_COUNT)
      return ((dyn_bits >> idx) & 0x01U) != 0;
  }
  return false;
}

static bool is_page_protected(uint16_t page) {
  if (is_aes())
    return s_auth0 < T2T_AES_PAGES && page >= s_auth0;
  if (s_prof.supports_ulc_auth) {
    uint8_t auth0 = s_mem[T2T_ULC_AUTH0_PAGE * T2T_PAGE_SIZE];
    return auth0 >= T2T_PAGE_OTP && auth0 < T2T_ULC_AUTH_DISABLED && page >= auth0;
  }
  if (!s_prof.supports_pwd || s_auth0 == 0xFF)
    return false;
  return page >= s_auth0;
}

static bool can_read_page(uint16_t page) {
  if (is_aes() && s_aes_has_rid && !s_aes_session.is_authenticated)
    return false;
  if (is_aes() && s_aes_session.is_authenticated && page < T2T_PAGE_OTP)
    return true;
  if (s_prof.supports_ulc_auth) {
    if (page >= T2T_ULC_KEY_PAGE)
      return false;
    return !is_page_protected(page) || s_authenticated ||
           (s_mem[T2T_ULC_AUTH1_PAGE * T2T_PAGE_SIZE] & T2T_ULC_READ_FREE);
  }
  if (page >= s_page_count)
    return false;
  if (!is_page_protected(page))
    return true;
  if ((s_access & NTAG_ACCESS_PROT) == 0)
    return true;
  return s_authenticated;
}

static bool can_write_page(uint16_t page) {
  if (is_aes()) {
    if ((s_mem[T2T_AES_ACCESS_PAGE * T2T_PAGE_SIZE] & T2T_CONFIG_LOCK) &&
        page >= T2T_AES_CFG_PAGE && page <= T2T_AES_ACCESS_PAGE)
      return false;
    if (page >= T2T_AES_KEY_PAGE && page < T2T_AES_KEY_END) {
      size_t key = (page - T2T_AES_KEY_PAGE) / T2T_COMP_WRITE_PAGES;
      if (s_mem[T2T_AES_LOCK_PAGE * T2T_PAGE_SIZE] & (1U << (T2T_AES_KEY_LOCK_BASE + key)))
        return false;
    }
    if (page > T2T_AES_ACCESS_PAGE && page != T2T_AES_LOCK_PAGE &&
        !(page >= T2T_AES_KEY_PAGE && page < T2T_AES_KEY_END))
      return false;
  }
  if (s_prof.supports_pwd && (s_access & T2T_CONFIG_LOCK) &&
      (page == s_prof.cfg_page || page == s_prof.cfg_page + 1))
    return false;
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
    ptm[PT_IDX_ATQA_0] = T2T_AES_SHORT_ATQA;
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
        s_mem[off + T2T_CFG_AUTH0_OFFSET] = s_auth0;
        s_mem[off + T2T_PAGE_SIZE] = s_access;
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
  if (rl >= NDEF_TLV_TERMINATOR || NDEF_TLV_HEADER_LEN + rl + 1 > max_user) {
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

static void transmit(const uint8_t *data, uint16_t length, uint8_t bits) {
  uint8_t main = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main) != HB_NFC_OK)
    goto failed;
  s_pending_main |= main & ST25R3916_IRQ_MAIN_RXE;
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_CLEAR_FIFO) != HB_NFC_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES1, length >> T2T_TX_BYTE_SHIFT) != HB_NFC_OK ||
      hb_nfc_spi_reg_write(ST25R3916_REG_NUM_TX_BYTES2,
                           ((length & T2T_TX_LOW_MASK) << T2T_TX_BIT_SHIFT) | bits) != HB_NFC_OK ||
      st25r3916_fifo_load(data, bits ? 1 : length) != HB_NFC_OK ||
      hb_nfc_spi_direct_cmd(bits ? ST25R3916_CMD_TX_WO_CRC : ST25R3916_CMD_TX_WITH_CRC) !=
          HB_NFC_OK)
    goto failed;
  for (unsigned i = 0; i < T2T_TX_POLL_COUNT; i++) {
    if (hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &main) != HB_NFC_OK)
      goto failed;
    s_pending_main |= main & ST25R3916_IRQ_MAIN_RXE;
    if (main & ST25R3916_IRQ_MAIN_TXE)
      return;
    hb_nfc_timer_delay_us(T2T_TX_POLL_US);
  }
  t2t_reset_auth();
  s_pending_main = 0;
  s_i2c_sector = 0;
  s_state = T2T_STATE_SENSE;
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_GOTO_SENSE) == HB_NFC_OK)
    return;
failed:
  s_transport_error = true;
}

static void tx_with_crc(const uint8_t *data, int len) {
  if (is_aes() && s_aes_session.is_secure && len >= 0 && len <= T2T_AES_DATA_MAX) {
    uint8_t response[T2T_AES_DATA_MAX + T2T_AES_MAC_SIZE];
    if (len)
      memcpy(response, data, (size_t)len);
    if (t2t_aes_sign(&s_aes_session, data, (size_t)len, response + len) != ESP_OK) {
      tx_4bit_nack(0);
      return;
    }
    transmit(response, (uint16_t)(len + T2T_AES_MAC_SIZE), 0);
    return;
  }
  if (data != NULL && len > 0)
    transmit(data, (uint16_t)len, 0);
}

static void tx_4bit_nack(uint8_t code) {
  if (is_aes() && code != T2T_ACK)
    t2t_reset_auth();
  uint8_t value = code & T2T_NIBBLE_MASK;
  transmit(&value, 0, T2T_NIBBLE_BITS);
}

static void tx_ack(void) {
  if (is_aes() && s_aes_session.is_secure) {
    tx_with_crc(NULL, 0);
    return;
  }
  tx_4bit_nack(T2T_ACK);
}

static void ulc_activate(bool is_field_reset) {
  if (!s_prof.supports_ulc_auth)
    return;
  if (s_ulc_key_pending) {
    s_ulc_auth_enabled = s_ulc_key_known == T2T_ULC_ALL_KEY_BYTES;
    if (s_ulc_auth_enabled)
      memcpy(s_ulc_key, &s_mem[T2T_ULC_KEY_PAGE * T2T_PAGE_SIZE], sizeof(s_ulc_key));
    s_ulc_key_pending = false;
  }
  if (is_field_reset && s_ulc_counter_pending) {
    size_t offset = T2T_ULC_COUNTER_PAGE * T2T_PAGE_SIZE;
    s_mem[offset] = (uint8_t)s_ulc_counter_next;
    s_mem[offset + 1] = (uint8_t)(s_ulc_counter_next >> T2T_BYTE_BITS);
    s_ulc_counter_pending = false;
  }
}

static bool ulc_write_counter(const uint8_t *data) {
  size_t offset = T2T_ULC_COUNTER_PAGE * T2T_PAGE_SIZE;
  uint16_t current = s_ulc_counter_pending
                         ? s_ulc_counter_next
                         : s_mem[offset] | ((uint16_t)s_mem[offset + 1] << T2T_BYTE_BITS);
  uint16_t increment =
      current ? (data[0] & T2T_NIBBLE_MASK) : data[0] | ((uint16_t)data[1] << T2T_BYTE_BITS);
  if (increment > T2T_ULC_COUNTER_MAX - current)
    return false;
  s_ulc_counter_next = current + increment;
  s_ulc_counter_pending = true;
  return true;
}

static bool write_page_internal(uint8_t page, const uint8_t *data) {
  if (is_i2c())
    return i2c_write(page, data);
  if (s_read_only || (is_page_protected(page) && !s_authenticated))
    return false;
  if (page >= s_page_count)
    return false;
  if (page < T2T_PAGE_UID_END)
    return false;

  if (page == T2T_PAGE_UID_END) {
    size_t off = (size_t)page * T2T_PAGE_SIZE;
    uint16_t current = get_static_lock_bits();
    uint16_t bits = data[2] | ((uint16_t)data[3] << T2T_BYTE_BITS);
    if (current & T2T_STATIC_LOCK_OTP_FREEZE)
      bits &= ~T2T_STATIC_LOCK_OTP_MASK;
    if (current & T2T_STATIC_LOCK_LOW_FREEZE)
      bits &= ~T2T_STATIC_LOCK_LOW_MASK;
    if (current & T2T_STATIC_LOCK_HIGH_FREEZE)
      bits &= ~T2T_STATIC_LOCK_HIGH_MASK;
    current |= bits;
    s_mem[off + 2] = (uint8_t)current;
    s_mem[off + 3] = (uint8_t)(current >> T2T_BYTE_BITS);
    return true;
  }

  if (s_prof.supports_ulc_auth && page == T2T_ULC_LOCK_PAGE) {
    size_t off = T2T_ULC_LOCK_PAGE * T2T_PAGE_SIZE;
    uint8_t low = data[0], high = data[1];
    if (s_mem[off] & 1U)
      low &= ~T2T_ULC_LOCK_LOW_MASK;
    if (s_mem[off] & T2T_ULC_LOCK_HIGH_FREEZE)
      low &= ~T2T_ULC_LOCK_HIGH_MASK;
    for (uint8_t bit = 0; bit < T2T_ULC_CONFIG_LOCK_BASE; bit++)
      if (s_mem[off + 1] & (1U << bit))
        high &= ~(1U << (bit + T2T_ULC_CONFIG_LOCK_BASE));
    s_mem[off] |= low;
    s_mem[off + 1] |= high;
    return true;
  }
  if (page == s_prof.dyn_lock_page) {
    size_t off = (size_t)page * T2T_PAGE_SIZE;
    uint16_t bits = data[0] | ((uint16_t)data[1] << T2T_BYTE_BITS);
    for (uint8_t i = 0; i < T2T_DYNAMIC_FREEZE_BITS; i++)
      if (s_mem[off + 2] & (1U << i))
        bits &= ~(T2T_MIRROR_MASK << (i * 2));
    s_mem[off] |= (uint8_t)bits;
    s_mem[off + 1] |= (uint8_t)(bits >> T2T_BYTE_BITS);
    s_mem[off + 2] |= data[2];
    return true;
  }

  if (!can_write_page(page))
    return false;

  if (s_prof.supports_ulc_auth && page == T2T_ULC_COUNTER_PAGE)
    return ulc_write_counter(data);
  if (s_prof.supports_ulc_auth && page >= T2T_ULC_KEY_PAGE) {
    size_t start = (page - T2T_ULC_KEY_PAGE) * T2T_PAGE_SIZE;
    for (size_t i = 0; i < T2T_PAGE_SIZE; i++) {
      size_t index = (start + i) ^ (T2T_DES_BLOCK_SIZE - 1);
      s_mem[T2T_ULC_KEY_PAGE * T2T_PAGE_SIZE + index] = data[i];
      s_ulc_key_known |= 1U << index;
    }
    s_ulc_key_pending = true;
    return true;
  }
  size_t off = (size_t)page * T2T_PAGE_SIZE;
  if (is_aes() && page == T2T_AES_LOCK_PAGE) {
    if (!(s_mem[off] & T2T_AES_KEY_LOCK_FREEZE))
      s_mem[off] |= data[0];
    return true;
  }
  if (is_aes() && page >= T2T_AES_KEY_PAGE && page < T2T_AES_KEY_END) {
    size_t key = (page - T2T_AES_KEY_PAGE) / T2T_COMP_WRITE_PAGES;
    size_t first = ((page - T2T_AES_KEY_PAGE) % T2T_COMP_WRITE_PAGES) * T2T_PAGE_SIZE;
    for (size_t i = 0; i < T2T_PAGE_SIZE; i++)
      s_aes_key_known[key] |= 1U << (first + i);
  }
  if (page == T2T_PAGE_OTP) {
    otp_write_bytes(&s_mem[off], data, T2T_PAGE_SIZE);
  } else {
    memcpy(&s_mem[off], data, T2T_PAGE_SIZE);
  }
  if (s_prof.supports_pwd) {
    s_auth0 = s_mem[s_prof.cfg_page * T2T_PAGE_SIZE + T2T_CFG_AUTH0_OFFSET];
    s_access = s_mem[(s_prof.cfg_page + 1) * T2T_PAGE_SIZE];
    if (page == s_prof.pwd_page) {
      memcpy(s_pwd, data, sizeof(s_pwd));
      s_pwd_known = true;
    }
    if (page == s_prof.pack_page) {
      memcpy(s_pack, data, sizeof(s_pack));
      s_pack_known = true;
    }
    s_pwd_enabled = s_pwd_known && s_pack_known;
  }
  return true;
}

static bool is_ev1(void) {
  return s_prof.type == T2T_TAG_ULTRALIGHT_EV1_11 || s_prof.type == T2T_TAG_ULTRALIGHT_EV1_21;
}

static void handle_tearing(uint8_t index) {
  if (index >= T2T_COUNTER_COUNT || !(s_tearing_known & (1U << index)) ||
      (!is_ev1() && !(is_ntag() && index == T2T_COUNTER_INDEX))) {
    tx_4bit_nack(0);
    return;
  }
  tx_with_crc(&s_tearing[index], 1);
}

static void handle_increment(const uint8_t *cmd) {
  uint8_t index = cmd[1];
  if ((!is_ev1() && !is_aes()) || s_read_only || index >= T2T_COUNTER_COUNT ||
      !(s_counter_known & (1U << index)) || cmd[T2T_INCR_FRAME_LENGTH - 1] != 0) {
    tx_4bit_nack(0);
    return;
  }
  if (is_aes() && index == T2T_COUNTER_INDEX && !s_authenticated &&
      !(s_access & T2T_AES_COUNTER_INCREMENT)) {
    tx_4bit_nack(0);
    return;
  }
  uint32_t value = 0;
  for (size_t i = 0; i < T2T_COUNTER_BYTES; i++)
    value |= (uint32_t)cmd[2 + i] << (i * T2T_BYTE_BITS);
  if ((!is_aes() && s_counters[index] == T2T_COUNTER_MAX) ||
      value > T2T_COUNTER_MAX - s_counters[index]) {
    tx_4bit_nack(0);
    return;
  }
  s_counters[index] += value;
  s_tearing[index] = T2T_TEARING_OK;
  s_tearing_known |= 1U << index;
  tx_ack();
}

static bool is_ntag(void) {
  return s_prof.type == T2T_TAG_NTAG213 || s_prof.type == T2T_TAG_NTAG215 ||
         s_prof.type == T2T_TAG_NTAG216;
}

static bool can_read_counter(void) {
  return is_ntag() && s_has_counter && (s_access & T2T_COUNTER_ENABLE) &&
         (!(s_access & T2T_COUNTER_PROTECTED) || s_authenticated);
}

static void increase_counter(void) {
  if (is_ntag() && s_has_counter && (s_access & T2T_COUNTER_ENABLE) && !s_counter_increased) {
    if (s_counter < T2T_COUNTER_MAX)
      s_counter++;
    s_counter_increased = true;
  }
}

static void handle_write_signature(const uint8_t *frame, size_t length) {
  if (!is_aes() || s_read_only || length != T2T_SIGNATURE_WRITE_LENGTH ||
      frame[1] >= T2T_AES_SIGNATURE_BLOCKS || s_aes_signature_lock != T2T_SIGNATURE_UNLOCKED) {
    tx_4bit_nack(0);
    return;
  }
  memcpy(s_aes_signature + frame[1] * T2T_PAGE_SIZE, frame + 2, T2T_PAGE_SIZE);
  s_aes_signature_known |= 1U << frame[1];
  s_has_aes_signature = s_aes_signature_known == T2T_AES_SIGNATURE_KNOWN;
  tx_ack();
}

static void handle_lock_signature(const uint8_t *frame, size_t length) {
  if (!is_aes() || s_read_only || length != T2T_SIGNATURE_LOCK_LENGTH ||
      frame[1] > T2T_SIGNATURE_ACTION_PERMANENT ||
      s_aes_signature_lock == T2T_SIGNATURE_LOCK_UNKNOWN ||
      s_aes_signature_lock == T2T_SIGNATURE_PERMANENT) {
    tx_4bit_nack(0);
    return;
  }
  switch (frame[1]) {
    case T2T_SIGNATURE_ACTION_UNLOCK:
      s_aes_signature_lock = T2T_SIGNATURE_UNLOCKED;
      break;
    case T2T_SIGNATURE_ACTION_LOCK:
      s_aes_signature_lock = T2T_SIGNATURE_LOCKED;
      break;
    case T2T_SIGNATURE_ACTION_PERMANENT:
      s_aes_signature_lock = T2T_SIGNATURE_PERMANENT;
      break;
  }
  tx_ack();
}

static void handle_signature(uint8_t parameter, size_t length) {
  if (is_aes()) {
    if (length == 2 && parameter == 0 && s_has_aes_signature &&
        (!s_aes_has_rid || s_aes_session.is_authenticated))
      tx_with_crc(s_aes_signature, sizeof(s_aes_signature));
    else
      tx_4bit_nack(0);
    return;
  }
  if ((is_ntag() || is_ev1() || s_prof.type == T2T_TAG_NTAG210 || s_prof.type == T2T_TAG_NTAG212 ||
       (is_i2c() && s_prof.supports_pwd)) &&
      length == 2 && parameter == 0 && s_has_signature)
    tx_with_crc(s_signature, sizeof(s_signature));
}

static void handle_vcsl(size_t length) {
  if ((is_ev1() || is_aes()) && length == T2T_VCSL_FRAME_LENGTH)
    tx_with_crc(&s_mem[s_prof.cfg_page * T2T_PAGE_SIZE + T2T_CFG_VCTID_OFFSET], 1);
}

static void handle_counter(uint8_t index) {
  bool allowed = (is_ev1() || is_aes())
                     ? index < T2T_COUNTER_COUNT && (s_counter_known & (1U << index))
                     : index == T2T_COUNTER_INDEX && can_read_counter();
  if (is_aes() && index == T2T_COUNTER_INDEX && !s_authenticated &&
      !(s_access & T2T_AES_COUNTER_READ))
    allowed = false;
  if (!allowed) {
    tx_4bit_nack(0);
    return;
  }
  uint32_t counter = (is_ev1() || is_aes()) ? s_counters[index] : s_counter;
  uint8_t reply[T2T_COUNTER_BYTES];
  for (size_t i = 0; i < sizeof(reply); i++)
    reply[i] = (uint8_t)(counter >> (i * T2T_BYTE_BITS));
  tx_with_crc(reply, sizeof(reply));
}

static void apply_mirror(uint16_t page, uint8_t *out) {
  bool is_small_ntag = s_prof.type == T2T_TAG_NTAG210 || s_prof.type == T2T_TAG_NTAG212;
  if ((!is_ntag() && !is_small_ntag) || page < s_prof.user_start || page > s_prof.user_end)
    return;
  const uint8_t *cfg = &s_mem[s_prof.cfg_page * T2T_PAGE_SIZE];
  uint8_t mode = is_small_ntag ? T2T_MIRROR_UID : cfg[0] >> T2T_MIRROR_MODE_SHIFT;
  if (!can_read_counter())
    mode &= T2T_MIRROR_UID;
  if (!mode || cfg[T2T_MIRROR_PAGE_OFFSET] < s_prof.user_start)
    return;
  size_t start = cfg[T2T_MIRROR_PAGE_OFFSET] * T2T_PAGE_SIZE +
                 ((cfg[0] >> T2T_MIRROR_OFFSET_SHIFT) & T2T_MIRROR_MASK);
  size_t length = mode == T2T_MIRROR_BOTH  ? T2T_MIRROR_LENGTH
                  : mode == T2T_MIRROR_UID ? T2T_MIRROR_UID_LENGTH
                                           : T2T_MIRROR_COUNTER_LENGTH;
  size_t end = (s_prof.user_end + 1) * T2T_PAGE_SIZE;
  if (start + length > end && mode == T2T_MIRROR_BOTH) {
    mode = T2T_MIRROR_UID;
    length = T2T_MIRROR_UID_LENGTH;
  }
  if (start + length > end)
    return;
  uint8_t text[T2T_MIRROR_LENGTH];
  static const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < T2T_UID_LEN_LONG; i++) {
    text[i * 2] = hex[s_uid[i] >> T2T_HEX_SHIFT];
    text[i * 2 + 1] = hex[s_uid[i] & T2T_NIBBLE_MASK];
  }
  text[T2T_MIRROR_UID_LENGTH] = 'x';
  for (size_t i = 0; i < T2T_MIRROR_COUNTER_LENGTH; i++)
    text[T2T_MIRROR_UID_LENGTH + 1 + i] =
        hex[(s_counter >> ((T2T_MIRROR_COUNTER_LENGTH - 1 - i) * T2T_HEX_SHIFT)) & T2T_NIBBLE_MASK];
  size_t offset = mode == T2T_MIRROR_COUNTER ? T2T_MIRROR_UID_LENGTH + 1 : 0;
  for (size_t i = 0; i < T2T_PAGE_SIZE; i++) {
    size_t address = page * T2T_PAGE_SIZE + i;
    if (address >= start && address - start < length)
      out[i] = text[offset + address - start];
  }
}

static void copy_read_page(uint16_t page, uint8_t *out) {
  if ((is_aes() && page >= T2T_AES_KEY_PAGE && page < T2T_AES_KEY_END) ||
      (s_prof.supports_pwd && (page == s_prof.pwd_page || page == s_prof.pack_page)) ||
      (s_prof.supports_ulc_auth && page >= s_page_count - T2T_COMP_WRITE_PAGES))
    memset(out, 0, T2T_PAGE_SIZE);
  else
    memcpy(out, &s_mem[page * T2T_PAGE_SIZE], T2T_PAGE_SIZE);
  if (s_prof.supports_ulc_auth && page == T2T_ULC_LOCK_PAGE)
    out[T2T_PAGE_SIZE - 1] = T2T_TEARING_OK;
  apply_mirror(page, out);
}

static void handle_read(uint8_t page) {
  if (is_i2c()) {
    i2c_read(page, page, false);
    return;
  }
  if (!can_read_page(page)) {
    tx_4bit_nack(0x00);
    return;
  }
  uint8_t resp[T2T_READ_RESP_SIZE];
  uint16_t rollover = 0;
  for (uint16_t i = 0; i < T2T_COMP_WRITE_PAGES; i++) {
    uint16_t current = page + i;
    if (!can_read_page(current))
      current = rollover++;
    copy_read_page(current, &resp[i * T2T_PAGE_SIZE]);
  }
  increase_counter();
  tx_with_crc(resp, sizeof(resp));
}

static void handle_fast_read(uint8_t start, uint8_t end) {
  if (is_i2c()) {
    i2c_read(start, end, true);
    return;
  }
  if (!s_prof.supports_get_version || end < start || end >= s_page_count) {
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
    copy_read_page(p, &resp[pos]);
    pos += T2T_PAGE_SIZE;
  }
  increase_counter();
  tx_with_crc(resp, (int)len);
}

static void handle_write(uint8_t page, const uint8_t *data) {
  if (!write_page_internal(page, data)) {
    tx_4bit_nack(0x00);
    return;
  }
  ESP_LOGD(TAG, "WRITE page=%u", page);
  tx_ack();
}

static void handle_comp_write_start(uint8_t page) {
  if (is_i2c() || is_aes() || s_read_only || !can_write_page(page)) {
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

  if (len != T2T_COMP_WRITE_LEN) {
    tx_4bit_nack(0x00);
    return;
  }
  if (!write_page_internal(s_comp_write_page, data)) {
    tx_4bit_nack(0x00);
    return;
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
  if (len != T2T_PWD_FRAME_LENGTH) {
    tx_4bit_nack(0x00);
    return;
  }

  uint8_t limit = s_access & T2T_AUTH_LIMIT_MASK;
  if (limit && s_failed_auth_attempts >= limit) {
    s_authenticated = false;
    tx_4bit_nack(T2T_NACK_AUTH_LIMIT);
    return;
  }
  if (memcmp(&cmd[1], s_pwd, sizeof(s_pwd)) == 0) {
    s_failed_auth_attempts = 0;
    s_authenticated = true;
    uint8_t resp[2] = {s_pack[0], s_pack[1]};
    tx_with_crc(resp, sizeof(resp));
    return;
  }
  s_authenticated = false;
  if (limit)
    s_failed_auth_attempts++;
  tx_4bit_nack(limit && s_failed_auth_attempts >= limit ? T2T_NACK_AUTH_LIMIT : 0);
}

static void handle_ulc_auth_start(void) {
  s_authenticated = false;
  s_ulc_state = ULC_AUTH_IDLE;
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
  uint8_t reply[T2T_DES_BLOCK_SIZE + 1] = {T2T_ULC_MORE};
  memcpy(reply + 1, enc, sizeof(enc));
  tx_with_crc(reply, sizeof(reply));
}

static void handle_ulc_auth_step2(const uint8_t *data, int len) {
  s_authenticated = false;
  if (len != T2T_COMP_WRITE_LEN + 1 || data[0] != T2T_ULC_MORE) {
    tx_4bit_nack(0x00);
    s_ulc_state = ULC_AUTH_IDLE;
    return;
  }

  data++;
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
  uint8_t reply[T2T_DES_BLOCK_SIZE + 1] = {0};
  memcpy(reply + 1, enc, sizeof(enc));
  tx_with_crc(reply, sizeof(reply));
}

static void handle_aes_auth(const uint8_t *frame, size_t length) {
  uint8_t reply[T2T_AES_AUTH_REPLY_SIZE];
  uint16_t limit =
      s_mem[T2T_AES_ACCESS_PAGE * T2T_PAGE_SIZE + 2] |
      ((uint16_t)(s_mem[T2T_AES_ACCESS_PAGE * T2T_PAGE_SIZE + 3] & T2T_AES_AUTH_LIMIT_HIGH_MASK)
       << T2T_BYTE_BITS);
  if (s_aes_session.is_pending && frame[0] != T2T_CMD_ULC_AUTH) {
    bool secure = (s_mem[T2T_AES_CFG_PAGE * T2T_PAGE_SIZE] & T2T_AES_SECURE_MASK) != 0;
    if (t2t_aes_finish(&s_aes_session, frame, length, secure, reply) != ESP_OK) {
      if (limit && s_failed_auth_attempts < UINT32_MAX)
        s_failed_auth_attempts++;
      tx_4bit_nack(0);
      return;
    }
    s_failed_auth_attempts = s_failed_auth_attempts > T2T_AES_AUTH_RECOVERY
                                 ? s_failed_auth_attempts - T2T_AES_AUTH_RECOVERY
                                 : 0;
    s_authenticated = s_aes_key_number == 0;
    transmit(reply, sizeof(reply), 0);
    return;
  }
  t2t_reset_auth();
  if (length != 2 || frame[0] != T2T_CMD_ULC_AUTH || frame[1] >= T2T_AES_KEY_COUNT ||
      (limit && s_failed_auth_attempts >= limit) || s_aes_key_known[frame[1]] != UINT16_MAX) {
    tx_4bit_nack(0);
    return;
  }
  s_aes_key_number = frame[1];
  uint8_t key[T2T_AES_BLOCK_SIZE], nonce[T2T_AES_BLOCK_SIZE];
  size_t base = T2T_AES_KEY_PAGE * T2T_PAGE_SIZE + frame[1] * T2T_AES_BLOCK_SIZE;
  for (size_t i = 0; i < sizeof(key); i++)
    key[i] = s_mem[base + sizeof(key) - 1 - i];
  esp_fill_random(nonce, sizeof(nonce));
  esp_err_t error = t2t_aes_begin(&s_aes_session, key, nonce, reply);
  memset(key, 0, sizeof(key));
  if (error == ESP_OK)
    transmit(reply, sizeof(reply), 0);
  else
    tx_4bit_nack(0);
}

static bool is_i2c(void) {
  return s_prof.type >= T2T_TAG_NTAG_I2C_1K && s_prof.type < T2T_TAG_COUNT;
}

static bool i2c_address(uint8_t sector, uint16_t page, uint16_t *out_page) {
  if (page >= T2T_I2C_SECTOR_PAGES)
    return false;
  bool is_plus = s_prof.type >= T2T_TAG_NTAG_I2C_PLUS_1K;
  bool is_large = s_prof.type == T2T_TAG_NTAG_I2C_2K || s_prof.type == T2T_TAG_NTAG_I2C_PLUS_2K;
  if (sector == T2T_I2C_SESSION_SECTOR && page >= T2T_I2C_SESSION_PAGE &&
      page < T2T_I2C_SESSION_PAGE + 2) {
    *out_page = (is_plus ? T2T_I2C_PLUS_BASE : s_page_count) - 2 + page - T2T_I2C_SESSION_PAGE;
    return true;
  }
  if (is_plus) {
    if (sector == 0 && page <= T2T_I2C_CONFIG_PAGE + 1)
      *out_page = page;
    else if (sector == 0 && page >= T2T_I2C_PLUS_SESSION && page < T2T_I2C_PLUS_SESSION + 2)
      *out_page = T2T_I2C_PLUS_BASE - 2 + page - T2T_I2C_PLUS_SESSION;
    else if (sector == 1 && is_large)
      *out_page = T2T_I2C_PLUS_BASE + page;
    else
      return false;
  } else {
    uint8_t config_sector = is_large ? 1 : 0;
    if (sector == config_sector && page >= T2T_I2C_CONFIG_PAGE && page < T2T_I2C_CONFIG_PAGE + 2)
      *out_page = s_page_count - T2T_PAGE_SIZE + page - T2T_I2C_CONFIG_PAGE;
    else if (sector == 0 && (is_large || page <= T2T_I2C_LOCK_PAGE))
      *out_page = page;
    else if (sector == 1 && is_large && page <= T2T_I2C_LOCK_PAGE - 2)
      *out_page = T2T_I2C_SECTOR_PAGES + page;
    else
      return false;
  }
  return *out_page < s_page_count;
}

static void i2c_read(uint8_t start, uint8_t end, bool is_fast) {
  uint16_t pages = is_fast ? (uint16_t)end - start + 1 : T2T_COMP_WRITE_PAGES;
  if ((is_fast && end < start) || pages * T2T_PAGE_SIZE > T2T_FAST_READ_MAX_LEN) {
    tx_4bit_nack(0);
    return;
  }
  uint8_t response[T2T_FAST_READ_MAX_LEN];
  for (uint16_t i = 0; i < pages; i++) {
    uint16_t page = start + i, mapped;
    if (!i2c_address(s_i2c_sector, page, &mapped)) {
      if (i == 0) {
        tx_4bit_nack(0);
        return;
      }
      memset(response + i * T2T_PAGE_SIZE, 0, T2T_PAGE_SIZE);
      continue;
    }
    if (s_prof.supports_pwd) {
      bool protected = s_auth0 <= T2T_I2C_AUTH_END &&
                       ((s_i2c_sector == 0 && page >= s_auth0 && page <= T2T_I2C_AUTH_END &&
                         page != T2T_I2C_LOCK_PAGE) ||
                        (s_i2c_sector == 1 &&
                         (s_mem[T2T_I2C_PT_PAGE * T2T_PAGE_SIZE] & T2T_I2C_PROTECT_SECTOR)));
      if ((s_i2c_sector == 1 && (s_access & T2T_I2C_DISABLE_SECTOR)) ||
          (protected && (s_access & T2T_I2C_READ_PROTECT) && !s_authenticated)) {
        tx_4bit_nack(0);
        return;
      }
    }
    copy_read_page(mapped, response + i * T2T_PAGE_SIZE);
  }
  tx_with_crc(response, pages * T2T_PAGE_SIZE);
}

static bool i2c_write(uint8_t page, const uint8_t *data) {
  uint16_t mapped;
  if (s_read_only || !i2c_address(s_i2c_sector, page, &mapped))
    return false;
  bool is_plus = s_prof.type >= T2T_TAG_NTAG_I2C_PLUS_1K;
  bool is_large = s_prof.type == T2T_TAG_NTAG_I2C_2K || s_prof.type == T2T_TAG_NTAG_I2C_PLUS_2K;
  uint16_t address = s_i2c_sector * T2T_I2C_SECTOR_PAGES + page;
  uint16_t last = is_plus ? T2T_I2C_LOCK_PAGE - 1 : s_prof.user_end;
  bool is_user = (s_i2c_sector == 0 && page >= T2T_COMP_WRITE_PAGES &&
                  (is_plus ? page <= last : address <= last)) ||
                 (s_i2c_sector == 1 && is_large && (is_plus || address <= last));
  if (!is_user)
    return false;
  if (address < T2T_I2C_LOCK_STEP_1K) {
    if (get_static_lock_bits() & (1U << address))
      return false;
  } else {
    uint16_t step = is_large ? T2T_I2C_LOCK_STEP_2K : T2T_I2C_LOCK_STEP_1K;
    uint16_t bit = (address - T2T_I2C_LOCK_STEP_1K) / step;
    if (bit >= T2T_I2C_LOCK_BITS || (get_dynamic_lock_bits() & (1U << bit)))
      return false;
  }
  if (is_plus) {
    if (s_i2c_sector == 1 && (s_access & T2T_I2C_DISABLE_SECTOR))
      return false;
    bool protected =
        s_auth0 <= T2T_I2C_AUTH_END &&
        ((s_i2c_sector == 0 && page >= s_auth0) ||
         (s_i2c_sector == 1 && (s_mem[T2T_I2C_PT_PAGE * T2T_PAGE_SIZE] & T2T_I2C_PROTECT_SECTOR)));
    if (protected && !s_authenticated)
      return false;
  }
  memcpy(s_mem + mapped * T2T_PAGE_SIZE, data, T2T_PAGE_SIZE);
  return true;
}

static void i2c_select(const uint8_t *frame, size_t length) {
  s_i2c_select_pending = false;
  if (length == T2T_PAGE_SIZE && frame[0] != T2T_I2C_SELECT_MARKER && frame[1] == 0 &&
      frame[2] == 0 && frame[3] == 0)
    s_i2c_sector = frame[0];
  else
    tx_4bit_nack(0);
}
