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
 * @file mf_desfire.h
 * @brief MIFARE DESFire native command interface.
 */
#ifndef MF_DESFIRE_H
#define MF_DESFIRE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "highboy_nfc_error.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MF_DESFIRE_CLA            0x90
#define MF_DESFIRE_CMD_ADDITIONAL 0xAF
#define MF_DESFIRE_SW_OK          0x9100
#define MF_DESFIRE_SW_MORE        0x91AF

#define MF_DESFIRE_CMD_GET_VERSION        0x60
#define MF_DESFIRE_CMD_SELECT_APP         0x5A
#define MF_DESFIRE_CMD_GET_APP_IDS        0x6A
#define MF_DESFIRE_CMD_GET_KEY_SET        0x45
#define MF_DESFIRE_CMD_AUTH_3DES          0x0A
#define MF_DESFIRE_CMD_AUTH_AES           0xAA
#define MF_DESFIRE_CMD_AUTH_EV2_FIRST     0x71
#define MF_DESFIRE_CMD_AUTH_EV2_NONFIRST  0x77
#define MF_DESFIRE_CMD_GET_FILE_IDS       0x6F
#define MF_DESFIRE_CMD_READ_DATA          0xBD
#define MF_DESFIRE_CMD_WRITE_DATA         0x3D
#define MF_DESFIRE_CMD_COMMIT_TRANSACTION 0xC7
#define MF_DESFIRE_CMD_ABORT_TRANSACTION  0xA7

/**
 * @brief DESFire key types.
 */
typedef enum {
  MF_DESFIRE_KEY_DES = 0,
  MF_DESFIRE_KEY_2K3DES,
  MF_DESFIRE_KEY_3K3DES,
  MF_DESFIRE_KEY_AES,
} mf_desfire_key_type_t;

/**
 * @brief DESFire application identifier (3 bytes).
 */
typedef struct {
  uint8_t aid[3];
} mf_desfire_aid_t;

/**
 * @brief DESFire card version information.
 */
typedef struct {
  uint8_t hw[7];
  uint8_t sw[7];
  uint8_t uid[7];
  uint8_t batch[7];
} mf_desfire_version_t;

/**
 * @brief DESFire communication session state.
 */
typedef struct {
  nfc_iso_dep_data_t dep;
  bool dep_ready;
  bool authenticated;
  bool ev2_authenticated;
  mf_desfire_key_type_t key_type;
  uint8_t key_no;
  uint8_t session_key[16];
  uint8_t iv[16];
  uint8_t ev2_ti[4];
  uint16_t ev2_cmd_ctr;
  uint8_t ev2_ses_mac[16];
  uint8_t ev2_ses_enc[16];
  uint8_t ev2_pd_cap2[6];
  uint8_t ev2_pcd_cap2[6];
} mf_desfire_session_t;

/**
 * @brief Initialize DESFire poller session with ISO-DEP activation.
 *
 * @param card     ISO 14443A card data.
 * @param[out] session  Session state to initialize.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_poller_init(const nfc_iso14443a_data_t *card,
                                    mf_desfire_session_t *session);

/**
 * @brief Transceive a raw APDU via ISO-DEP.
 *
 * @param session   Active session.
 * @param apdu      APDU buffer to send.
 * @param apdu_len  Length of APDU.
 * @param[out] rx   Receive buffer.
 * @param rx_max    Maximum receive buffer size.
 * @param timeout_ms  Timeout in milliseconds.
 * @return Number of bytes received, or negative on error.
 */
int mf_desfire_apdu_transceive(mf_desfire_session_t *session,
                               const uint8_t *apdu,
                               size_t apdu_len,
                               uint8_t *rx,
                               size_t rx_max,
                               int timeout_ms);

/**
 * @brief Transceive a DESFire native command with automatic chaining.
 *
 * @param session    Active session.
 * @param cmd        DESFire command byte.
 * @param data       Command data payload (may be NULL).
 * @param data_len   Length of data payload.
 * @param[out] out       Response data buffer.
 * @param out_max    Maximum response buffer size.
 * @param[out] out_len   Actual response data length.
 * @param[out] sw        Status word (e.g. 0x9100 for OK).
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_transceive_native(mf_desfire_session_t *session,
                                          uint8_t cmd,
                                          const uint8_t *data,
                                          size_t data_len,
                                          uint8_t *out,
                                          size_t out_max,
                                          size_t *out_len,
                                          uint16_t *sw);

/**
 * @brief Get card version information (hardware, software, UID, batch).
 *
 * @param session  Active session.
 * @param[out] out  Version information.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_get_version(mf_desfire_session_t *session, mf_desfire_version_t *out);

/**
 * @brief Select a DESFire application by AID.
 *
 * @param session  Active session.
 * @param aid      Application identifier.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_select_application(mf_desfire_session_t *session,
                                           const mf_desfire_aid_t *aid);

/**
 * @brief Authenticate using EV1 3DES.
 *
 * @param session  Active session.
 * @param key_no   Key number.
 * @param key      16-byte 3DES key.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_authenticate_ev1_3des(mf_desfire_session_t *session,
                                              uint8_t key_no,
                                              const uint8_t key[16]);

/**
 * @brief Authenticate using EV1 AES.
 *
 * @param session  Active session.
 * @param key_no   Key number.
 * @param key      16-byte AES key.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_authenticate_ev1_aes(mf_desfire_session_t *session,
                                             uint8_t key_no,
                                             const uint8_t key[16]);

/**
 * @brief Authenticate using EV2 first authentication.
 *
 * @param session  Active session.
 * @param key_no   Key number.
 * @param key      16-byte AES key.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_authenticate_ev2_first(mf_desfire_session_t *session,
                                               uint8_t key_no,
                                               const uint8_t key[16]);

/**
 * @brief Authenticate using EV2 non-first authentication.
 *
 * @param session  Active session.
 * @param key_no   Key number.
 * @param key      16-byte AES key.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_authenticate_ev2_nonfirst(mf_desfire_session_t *session,
                                                  uint8_t key_no,
                                                  const uint8_t key[16]);

/**
 * @brief Get list of application IDs on the card.
 *
 * @param session      Active session.
 * @param[out] out     Buffer for application IDs (3 bytes each).
 * @param out_max      Maximum buffer size.
 * @param[out] out_len Actual bytes written.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_get_application_ids(mf_desfire_session_t *session,
                                            uint8_t *out,
                                            size_t out_max,
                                            size_t *out_len);

/**
 * @brief Get list of file IDs in the selected application.
 *
 * @param session      Active session.
 * @param[out] out     Buffer for file IDs.
 * @param out_max      Maximum buffer size.
 * @param[out] out_len Actual bytes written.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_get_file_ids(mf_desfire_session_t *session,
                                     uint8_t *out,
                                     size_t out_max,
                                     size_t *out_len);

/**
 * @brief Get key settings for the selected application.
 *
 * @param session          Active session.
 * @param[out] key_settings  Key settings byte.
 * @param[out] max_keys      Maximum number of keys.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_get_key_settings(mf_desfire_session_t *session,
                                         uint8_t *key_settings,
                                         uint8_t *max_keys);

/**
 * @brief Read data from a standard or backup data file.
 *
 * @param session      Active session.
 * @param file_id      File ID to read from.
 * @param offset       Byte offset within the file.
 * @param length       Number of bytes to read (0 = read all).
 * @param[out] out     Output buffer.
 * @param out_max      Maximum output buffer size.
 * @param[out] out_len Actual bytes read.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_read_data(mf_desfire_session_t *session,
                                  uint8_t file_id,
                                  uint32_t offset,
                                  uint32_t length,
                                  uint8_t *out,
                                  size_t out_max,
                                  size_t *out_len);

/**
 * @brief Write data to a standard or backup data file.
 *
 * @param session   Active session.
 * @param file_id   File ID to write to.
 * @param offset    Byte offset within the file.
 * @param data      Data to write.
 * @param data_len  Number of bytes to write.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_write_data(mf_desfire_session_t *session,
                                   uint8_t file_id,
                                   uint32_t offset,
                                   const uint8_t *data,
                                   size_t data_len);

/**
 * @brief Commit a pending transaction.
 *
 * @param session  Active session.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_commit_transaction(mf_desfire_session_t *session);

/**
 * @brief Abort a pending transaction.
 *
 * @param session  Active session.
 * @return
 *   - HB_NFC_OK on success
 *   - Error code on failure
 */
hb_nfc_err_t mf_desfire_abort_transaction(mf_desfire_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* MF_DESFIRE_H */
