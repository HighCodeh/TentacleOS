// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/**
 * @file mf_classic_emu.h
 * @brief MIFARE Classic card emulation.
 */
#ifndef MF_CLASSIC_EMU_H
#define MF_CLASSIC_EMU_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MFC_EMU_MAX_SECTORS 40
#define MFC_EMU_MAX_BLOCKS  256
#define MFC_EMU_BLOCK_SIZE  16

#define MFC_ACK             0x0A
#define MFC_NACK_INVALID_OP 0x00
#define MFC_NACK_PARITY_CRC 0x01
#define MFC_NACK_OTHER      0x04
#define MFC_NACK_NAK        0x05

#define MFC_CMD_AUTH_KEY_A 0x60
#define MFC_CMD_AUTH_KEY_B 0x61
#define MFC_CMD_READ       0x30
#define MFC_CMD_WRITE      0xA0
#define MFC_CMD_DECREMENT  0xC0
#define MFC_CMD_INCREMENT  0xC1
#define MFC_CMD_RESTORE    0xC2
#define MFC_CMD_TRANSFER   0xB0
#define MFC_CMD_HALT       0x50

/**
 * @brief Data block access condition levels.
 */
typedef enum {
  MFC_AC_DATA_RD_AB = 0,
  MFC_AC_DATA_RD_AB_W_NONE = 2,
  MFC_AC_DATA_RD_AB_W_B = 4,
  MFC_AC_DATA_NEVER = 7,
} mfc_ac_data_t;

/**
 * @brief Card data for MIFARE Classic emulation.
 */
typedef struct {
  uint8_t uid[10];
  uint8_t uid_len;
  uint8_t atqa[2];
  uint8_t sak;
  mf_classic_type_t type;

  uint8_t blocks[MFC_EMU_MAX_BLOCKS][MFC_EMU_BLOCK_SIZE];
  int total_blocks;

  struct {
    uint8_t key_a[6];
    uint8_t key_b[6];
    bool key_a_known;
    bool key_b_known;
  } keys[MFC_EMU_MAX_SECTORS];

  int sector_count;
} mfc_emu_card_data_t;

/**
 * @brief Emulator state machine states.
 */
typedef enum {
  MFC_EMU_STATE_IDLE = 0,
  MFC_EMU_STATE_LISTEN,
  MFC_EMU_STATE_ACTIVATED,
  MFC_EMU_STATE_AUTH_SENT_NT,
  MFC_EMU_STATE_AUTHENTICATED,
  MFC_EMU_STATE_WRITE_PENDING,
  MFC_EMU_STATE_VALUE_PENDING,
  MFC_EMU_STATE_HALTED,
  MFC_EMU_STATE_ERROR,
} mfc_emu_state_t;

/**
 * @brief Emulation statistics counters.
 */
typedef struct {
  int total_auths;
  int successful_auths;
  int failed_auths;
  int reads_served;
  int writes_served;
  int writes_blocked;
  int value_ops;
  int halts;
  int nacks_sent;
  int unknown_cmds;
  int field_losses;
  int cycles;
  int parity_errors;
} mfc_emu_stats_t;

/**
 * @brief Emulation event types.
 */
typedef enum {
  MFC_EMU_EVT_ACTIVATED,
  MFC_EMU_EVT_AUTH_SUCCESS,
  MFC_EMU_EVT_AUTH_FAIL,
  MFC_EMU_EVT_READ,
  MFC_EMU_EVT_WRITE,
  MFC_EMU_EVT_WRITE_BLOCKED,
  MFC_EMU_EVT_VALUE_OP,
  MFC_EMU_EVT_HALT,
  MFC_EMU_EVT_FIELD_LOST,
  MFC_EMU_EVT_ERROR,
} mfc_emu_event_type_t;

/**
 * @brief Emulation event data.
 */
typedef struct {
  mfc_emu_event_type_t type;
  union {
    struct {
      int sector;
      mf_key_type_t key_type;
    } auth;
    struct {
      uint8_t block;
    } read;
    struct {
      uint8_t block;
    } write;
    struct {
      uint8_t cmd;
      uint8_t block;
      int32_t value;
    } value_op;
  };
} mfc_emu_event_t;

/**
 * @brief Emulation event callback type.
 */
typedef void (*mfc_emu_event_cb_t)(const mfc_emu_event_t *evt, void *ctx);

/**
 * @brief Initialize emulator with card data.
 *
 * Copies the card dump into internal storage.
 *
 * @param card  Card data to emulate.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mfc_emu_init(const mfc_emu_card_data_t *card);

/**
 * @brief Set event callback for UI notifications.
 *
 * @param cb   Callback function.
 * @param ctx  User context passed to callback.
 */
void mfc_emu_set_callback(mfc_emu_event_cb_t cb, void *ctx);

/**
 * @brief Configure ST25R3916 hardware for target mode.
 *
 * Loads PT memory, sets mode, and unmasks interrupts.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mfc_emu_configure_target(void);

/**
 * @brief Load PT Memory for diagnostics.
 *
 * Writes ATQA/UID/BCC/SAK to chip's passive target memory.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mfc_emu_load_pt_memory(void);

/**
 * @brief Start emulation and enter listen state.
 *
 * Issues ST25R3916_CMD_GOTO_SENSE to start listening for a reader.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mfc_emu_start(void);

/**
 * @brief Run one cycle of the emulation loop.
 *
 * Call this repeatedly from the main loop.
 *
 * @return Current emulator state.
 */
mfc_emu_state_t mfc_emu_run_step(void);

/**
 * @brief Stop emulation and return to idle.
 */
void mfc_emu_stop(void);

/**
 * @brief Update card data while emulator is running (hot swap).
 *
 * Used for switching profiles without restarting.
 *
 * @param card  New card data.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mfc_emu_update_card(const mfc_emu_card_data_t *card);

/**
 * @brief Get emulation statistics.
 *
 * @return Current statistics counters.
 */
mfc_emu_stats_t mfc_emu_get_stats(void);

/**
 * @brief Get current emulator state.
 *
 * @return Current state.
 */
mfc_emu_state_t mfc_emu_get_state(void);

/**
 * @brief Get human-readable state name string.
 *
 * @param state  Emulator state.
 * @return State name string.
 */
const char *mfc_emu_state_str(mfc_emu_state_t state);

/**
 * @brief Fill card data from a successful read.
 *
 * @param[out] cd    Card data structure to populate.
 * @param card       ISO 14443A card data from read.
 * @param type       MIFARE Classic card type.
 */
void mfc_emu_card_data_init(mfc_emu_card_data_t *cd,
                            const nfc_iso14443a_data_t *card,
                            mf_classic_type_t type);

/**
 * @brief Initialize Type 2 emulation with a default UID and NDEF text payload.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t t2t_emu_init_default(void);

/**
 * @brief Configure ST25R3916 target mode for Type 2 emulation.
 *
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t t2t_emu_configure_target(void);

/**
 * @brief Start Type 2 emulation (enter listen state).
 *
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t t2t_emu_start(void);

/**
 * @brief Run one step of Type 2 emulation loop (non-blocking).
 */
void t2t_emu_run_step(void);

/**
 * @brief Stop Type 2 emulation.
 */
void t2t_emu_stop(void);

/**
 * @brief Extract access condition bits (C1, C2, C3) for a block within a sector.
 *
 * @param trailer          16-byte sector trailer data.
 * @param block_in_sector  Block index within sector (0-3 for 4-block, 0-15 for 16-block).
 * @param[out] c1          Access condition bit C1.
 * @param[out] c2          Access condition bit C2.
 * @param[out] c3          Access condition bit C3.
 * @return true if access bits parity is valid.
 */
bool mfc_emu_get_access_bits(
    const uint8_t trailer[16], int block_in_sector, uint8_t *c1, uint8_t *c2, uint8_t *c3);

/**
 * @brief Check if a READ is permitted.
 *
 * @param trailer          16-byte sector trailer data.
 * @param block_in_sector  Block index within sector.
 * @param auth_key_type    Key type used for authentication.
 * @return true if read is permitted.
 */
bool mfc_emu_can_read(const uint8_t trailer[16], int block_in_sector, mf_key_type_t auth_key_type);

/**
 * @brief Check if a WRITE is permitted.
 *
 * @param trailer          16-byte sector trailer data.
 * @param block_in_sector  Block index within sector.
 * @param auth_key_type    Key type used for authentication.
 * @return true if write is permitted.
 */
bool mfc_emu_can_write(const uint8_t trailer[16], int block_in_sector, mf_key_type_t auth_key_type);

/**
 * @brief Check if INCREMENT is permitted (data blocks only).
 *
 * @param trailer          16-byte sector trailer data.
 * @param block_in_sector  Block index within sector.
 * @param auth_key_type    Key type used for authentication.
 * @return true if increment is permitted.
 */
bool mfc_emu_can_increment(const uint8_t trailer[16],
                           int block_in_sector,
                           mf_key_type_t auth_key_type);

/**
 * @brief Check if DECREMENT/RESTORE/TRANSFER is permitted (data blocks only).
 *
 * @param trailer          16-byte sector trailer data.
 * @param block_in_sector  Block index within sector.
 * @param auth_key_type    Key type used for authentication.
 * @return true if decrement/restore/transfer is permitted.
 */
bool mfc_emu_can_decrement(const uint8_t trailer[16],
                           int block_in_sector,
                           mf_key_type_t auth_key_type);

#ifdef __cplusplus
}
#endif

#endif
