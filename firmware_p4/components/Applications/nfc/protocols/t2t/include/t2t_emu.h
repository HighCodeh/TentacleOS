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
 * @file t2t_emu.h
 * @brief NFC-A Type 2 Tag (T2T) Emulation ST25R3916.
 */
#ifndef T2T_EMU_H
#define T2T_EMU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported T2T tag types for emulation.
 */
typedef enum {
  T2T_TAG_NTAG213 = 0,  /**< NTAG213 (144 bytes user memory). */
  T2T_TAG_NTAG215,      /**< NTAG215 (504 bytes user memory). */
  T2T_TAG_NTAG216,      /**< NTAG216 (888 bytes user memory). */
  T2T_TAG_ULTRALIGHT_C, /**< MIFARE Ultralight C. */
  T2T_TAG_ULTRALIGHT,
  T2T_TAG_ULTRALIGHT_EV1_11,
  T2T_TAG_ULTRALIGHT_EV1_21,
  T2T_TAG_NTAG210,
  T2T_TAG_NTAG212,
  T2T_TAG_ULTRALIGHT_AES,
  T2T_TAG_NTAG_I2C_1K,
  T2T_TAG_NTAG_I2C_2K,
  T2T_TAG_NTAG_I2C_PLUS_1K,
  T2T_TAG_NTAG_I2C_PLUS_2K,
  T2T_TAG_COUNT,
} t2t_tag_type_t;

#define T2T_I2C_VERSION_BYTES    8U
#define T2T_I2C_VERSION_VENDOR   0x04U
#define T2T_I2C_VERSION_FAMILY   0x07U
#define T2T_I2C_VERSION_SUBTYPE  0x05U
#define T2T_I2C_VERSION_MAJOR    2U
#define T2T_I2C_VERSION_ORIGINAL 1U
#define T2T_I2C_VERSION_PLUS     2U
#define T2T_I2C_VERSION_1K       0x13U
#define T2T_I2C_VERSION_2K       0x15U
#define T2T_I2C_VERSION_PROTOCOL 3U

#define T2T_AES_SIGNATURE_SIZE   48U
#define T2T_AES_KEY_COUNT        2U
#define T2T_AES_SIGNATURE_BLOCKS 12U
#define T2T_AES_SIGNATURE_KNOWN  0x0FFFU

typedef enum {
  T2T_SIGNATURE_LOCK_UNKNOWN = 0,
  T2T_SIGNATURE_UNLOCKED,
  T2T_SIGNATURE_LOCKED,
  T2T_SIGNATURE_PERMANENT,
  T2T_SIGNATURE_LOCK_COUNT,
} t2t_signature_lock_t;

#define T2T_SIGNATURE_SIZE 32U
#define T2T_VERSION_SIZE   8U
#define T2T_COUNTER_COUNT  3U
#define T2T_COUNTER_MAX    0xFFFFFFU

/**
 * @brief T2T emulation configuration.
 */
typedef struct {
  t2t_tag_type_t type;   /**< Tag type to emulate. */
  const char *ndef_text; /**< NDEF text record payload (NULL for empty). */

  uint8_t uid[7];  /**< Tag UID (4 or 7 bytes). */
  uint8_t uid_len; /**< Actual UID length. */

  uint16_t aes_signature_known;
  t2t_signature_lock_t aes_signature_lock;
  bool has_aes_signature;
  uint8_t aes_signature[T2T_AES_SIGNATURE_SIZE];
  uint16_t aes_key_known[T2T_AES_KEY_COUNT];
  bool has_signature;
  uint8_t signature[T2T_SIGNATURE_SIZE];
  bool has_version;
  uint8_t version[T2T_VERSION_SIZE];
  uint8_t counter_known;
  uint32_t counters[T2T_COUNTER_COUNT];
  uint8_t tearing_known;
  uint8_t tearing[T2T_COUNTER_COUNT];
  bool has_counter;
  uint32_t counter; /**< NTAG counter 2, persisted only in session RAM. */

  uint32_t failed_auth_attempts;
  bool enable_pwd; /**< Enable NTAG password authentication. */
  bool pwd_known;
  bool pack_known;
  uint8_t pwd[4];  /**< NTAG password bytes. */
  uint8_t pack[2]; /**< NTAG password acknowledge bytes. */
  uint8_t auth0;   /**< First page requiring authentication. */
  uint8_t access;  /**< Access configuration byte. */

  bool enable_ulc_auth; /**< Enable Ultralight C 3DES authentication. */
  uint16_t ulc_key_known;
  uint8_t ulc_key[16]; /**< Ultralight C 3DES key (16 bytes). */
} t2t_emu_config_t;

static inline bool t2t_i2c_version_matches(t2t_tag_type_t type, const uint8_t *version) {
  if (type < T2T_TAG_NTAG_I2C_1K || type >= T2T_TAG_COUNT)
    return false;
  bool is_plus = type >= T2T_TAG_NTAG_I2C_PLUS_1K;
  bool is_large = type == T2T_TAG_NTAG_I2C_2K || type == T2T_TAG_NTAG_I2C_PLUS_2K;
  return version != NULL && version[0] == 0 && version[1] == T2T_I2C_VERSION_VENDOR &&
         (version[2] == T2T_I2C_VERSION_VENDOR || version[2] == T2T_I2C_VERSION_FAMILY) &&
         version[3] == T2T_I2C_VERSION_SUBTYPE && version[4] == T2T_I2C_VERSION_MAJOR &&
         version[5] == (is_plus ? T2T_I2C_VERSION_PLUS : T2T_I2C_VERSION_ORIGINAL) &&
         version[6] == (is_large ? T2T_I2C_VERSION_2K : T2T_I2C_VERSION_1K) &&
         version[7] == T2T_I2C_VERSION_PROTOCOL;
}

/**
 * @brief Initialise T2T emulator with a custom configuration.
 *
 * @param cfg  Emulation configuration (copied internally).
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INVALID_ARG if cfg is NULL
 */
hb_nfc_err_t t2t_emu_init(const t2t_emu_config_t *cfg);

/**
 * @brief Initialise T2T emulator with default NTAG213 + sample NDEF.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INTERNAL on communication failure
 */
hb_nfc_err_t t2t_emu_init_default(void);

/**
 * @brief Configure ST25R3916 as NFC-A target for T2T emulation.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INTERNAL on communication failure
 */
hb_nfc_err_t t2t_emu_configure_target(void);

/**
 * @brief Start emulation (enter SENSE state, field detector on).
 *
 * @return
 *   - HB_NFC_OK on success
 *   - HB_NFC_ERR_INTERNAL on communication failure
 */
hb_nfc_err_t t2t_emu_start(void);

/**
 * @brief Stop emulation and power down.
 */
void t2t_emu_stop(void);

/**
 * @brief Return pointer to internal tag memory (for live inspection).
 *
 * @return Pointer to the emulated tag memory array.
 */
uint8_t *t2t_emu_get_mem(void);
/* Call from the emulator worker after its final run_step, before stop. */
hb_nfc_err_t t2t_emu_snapshot(t2t_emu_config_t *out_config, uint8_t *out_memory, size_t length);

/**
 * @brief Run one emulation step (call in tight loop). Non-blocking.
 */
void t2t_emu_run_step(void);
bool t2t_emu_has_error(void);
/* Full memory image; unknown credentials are never inferred from masked pages. */
hb_nfc_err_t t2t_emu_load_memory(const uint8_t *memory, size_t length, bool read_only);

#ifdef __cplusplus
}
#endif

#endif /* T2T_EMU_H */
