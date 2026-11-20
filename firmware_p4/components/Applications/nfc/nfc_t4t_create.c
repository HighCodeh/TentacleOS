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

#include "nfc_t4t.h"
#include "mf_desfire.h"
#include "esp_log.h"
#include <string.h>

#define CMD_CREATE_APP     0xCAU
#define CMD_CREATE_FILE    0xCDU
#define CMD_WRITE_DATA     0x3DU
#define CMD_FILE_SETTINGS  0xF5U
#define CMD_READ_DATA      0xBDU
#define SW_DUPLICATE       0x91DEU
#define FILE_SETTINGS_SIZE 7U
#define NXP_VENDOR         0x04U
#define DESFIRE_PRODUCT    0x01U
static const char *TAG = "NFC_T4T_CREATE";

static esp_err_t command(mf_desfire_session_t *session,
                         uint8_t command,
                         const uint8_t *data,
                         size_t length,
                         bool *out_created) {
  uint8_t response[32];
  size_t received = 0;
  uint16_t status = 0;
  esp_err_t error = mf_desfire_transceive_native(
      session, command, data, length, response, sizeof(response), &received, &status);
  if (error != ESP_OK)
    return error;
  if (out_created) {
    *out_created = status == MF_DESFIRE_SW_OK;
    if (status == SW_DUPLICATE && received == 0)
      return ESP_OK;
  }
  return status == MF_DESFIRE_SW_OK && received == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t
ensure_file(mf_desfire_session_t *session, const uint8_t settings[9], bool *out_created) {
  esp_err_t error = command(session, CMD_CREATE_FILE, settings, 9, out_created);
  if (error != ESP_OK || *out_created)
    return error;
  uint8_t actual[FILE_SETTINGS_SIZE];
  size_t length = 0;
  uint16_t status = 0;
  error = mf_desfire_transceive_native(
      session, CMD_FILE_SETTINGS, settings, 1, actual, sizeof(actual), &length, &status);
  if (error != ESP_OK)
    return error;
  if (status != MF_DESFIRE_SW_OK || length != sizeof(actual) || actual[0] != 0 ||
      memcmp(actual + 1, settings + 3, sizeof(actual) - 1))
    return ESP_ERR_INVALID_RESPONSE;
  return ESP_OK;
}

static esp_err_t
ensure_cc(mf_desfire_session_t *session, const uint8_t *write, size_t write_size, bool is_created) {
  if (!is_created) {
    uint8_t actual[NFC_T4T_CC_SIZE];
    size_t length = 0;
    uint16_t status = 0;
    const uint8_t read[] = {1, 0, 0, 0, NFC_T4T_CC_SIZE, 0, 0};
    esp_err_t error = mf_desfire_transceive_native(
        session, CMD_READ_DATA, read, sizeof(read), actual, sizeof(actual), &length, &status);
    if (error != ESP_OK)
      return error;
    if (status != MF_DESFIRE_SW_OK || length != sizeof(actual))
      return ESP_ERR_INVALID_RESPONSE;
    if (!memcmp(actual, write + sizeof(read), sizeof(actual)))
      return ESP_OK;
    /* Resume an interrupted creation only when its CC is still blank. */
    for (size_t i = 0; i < sizeof(actual); i++)
      if (actual[i] != 0)
        return ESP_ERR_INVALID_STATE;
  }
  return command(session, CMD_WRITE_DATA, write, write_size, NULL);
}

esp_err_t nfc_t4t_create(const nfc_iso14443a_data_t *identity, bool (*should_cancel)(void)) {
  if (!identity)
    return ESP_ERR_INVALID_ARG;
  mf_desfire_session_t session = {0};
  esp_err_t error = mf_desfire_poller_init(identity, &session);
  if (error != ESP_OK)
    return error;
  session.should_cancel = should_cancel;
  mf_desfire_version_t version;
  error = mf_desfire_get_version(&session, &version);
  if (error != ESP_OK)
    return error;
  if (version.hw[0] != NXP_VENDOR || version.hw[1] != DESFIRE_PRODUCT)
    return ESP_ERR_NOT_SUPPORTED;
  const mf_desfire_aid_t root = {{0, 0, 0}}, ndef = {{0x10, 0xEE, 0xEE}};
  error = mf_desfire_select_application(&session, &root);
  if (error != ESP_OK)
    return error;
  const uint8_t app[] = {0x10, 0xEE, 0xEE, 0x0F, 0x21, 0x10, 0xE1, 0xD2, 0x76, 0, 0, 0x85, 1, 1};
  bool is_created = false;
  error = command(&session, CMD_CREATE_APP, app, sizeof(app), &is_created);
  if (error != ESP_OK)
    return error;
  error = mf_desfire_select_application(&session, &ndef);
  if (error != ESP_OK)
    return error;
  const uint8_t cc_file[] = {1, 0x03, 0xE1, 0, 0xEE, 0xEE, 15, 0, 0};
  const uint8_t ndef_file[] = {2, 0x04, 0xE1, 0, 0xE0, 0xEE, 0, 8, 0};
  bool cc_created = false, ndef_created = false;
  error = ensure_file(&session, cc_file, &cc_created);
  if (error != ESP_OK)
    return error;
  error = ensure_file(&session, ndef_file, &ndef_created);
  if (error != ESP_OK)
    return error;
  const uint8_t cc_data[] = {1,    0, 0,    0, 15, 0,    0, 0, 15, 0x20, 0,
                             0x3A, 0, 0x34, 4, 6,  0xE1, 4, 8, 0,  0,    0};
  error = ensure_cc(&session, cc_data, sizeof(cc_data), cc_created);
  if (error != ESP_OK)
    return error;
  const uint8_t nlen[] = {2, 0, 0, 0, 2, 0, 0, 0, 0};
  if (ndef_created)
    error = command(&session, CMD_WRITE_DATA, nlen, sizeof(nlen), NULL);
  ESP_LOGI(TAG, "NDEF creation: %s", esp_err_to_name(error));
  return error;
}
