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
#include "slix.h"

#include <string.h>

#include "esp_log.h"

#define SLIX_VENDOR              0x04U
#define SLIX_COMMAND_SYSTEM_INFO 0xABU
#define SLIX_COMMAND_RANDOM      0xB2U
#define SLIX_COMMAND_AUTH        0xB3U
#define SLIX_COMMAND_SIGNATURE   0xBDU
#define SLIX_REQUEST_HEADER      3U
#define SLIX_RESPONSE_HEADER     1U
#define SLIX_CRC_BYTES           2U
#define SLIX_NONCE_BYTES         2U
#define SLIX_SYSTEM_INFO_BYTES   7U
#define SLIX_MAX_ARGUMENT_BYTES  5U
#define SLIX_TIMEOUT_MS          50U
#define SLIX_BYTE_BITS           8U
#define SLIX_HIGH_RATE_FLAGS     0x02U

static const char *TAG = "SLIX";

static hb_nfc_err_t exchange(const iso15693_tag_t *tag,
                             uint8_t command,
                             const uint8_t *args,
                             size_t args_len,
                             uint8_t *out,
                             size_t expected) {
  if (args_len > SLIX_MAX_ARGUMENT_BYTES || expected > SLIX_SIGNATURE_BYTES ||
      (args_len && args == NULL) || (expected && out == NULL))
    return HB_NFC_ERR_PARAM;
  uint8_t request[SLIX_REQUEST_HEADER + ISO15693_UID_LEN + SLIX_MAX_ARGUMENT_BYTES] = {
      tag ? ISO15693_FLAGS_ADDRESSED : SLIX_HIGH_RATE_FLAGS, command, SLIX_VENDOR};
  size_t size = SLIX_REQUEST_HEADER;
  if (tag) {
    memcpy(&request[size], tag->uid, sizeof(tag->uid));
    size += sizeof(tag->uid);
  }
  if (args_len)
    memcpy(&request[size], args, args_len);
  size += args_len;
  uint8_t response[SLIX_RESPONSE_HEADER + SLIX_SIGNATURE_BYTES + SLIX_CRC_BYTES];
  int received = iso15693_transceive(request,
                                     size,
                                     true,
                                     response,
                                     sizeof(response),
                                     SLIX_RESPONSE_HEADER + SLIX_CRC_BYTES,
                                     SLIX_TIMEOUT_MS);
  if (received <= 0)
    return HB_NFC_ERR_TIMEOUT;
  if ((size_t)received != expected + SLIX_RESPONSE_HEADER + SLIX_CRC_BYTES || response[0] != 0 ||
      !iso15693_check_crc(response, (size_t)received)) {
    ESP_LOGD(TAG, "Command %02X rejected or malformed", command);
    return HB_NFC_ERR_PROTOCOL;
  }
  if (expected)
    memcpy(out, &response[SLIX_RESPONSE_HEADER], expected);
  return HB_NFC_OK;
}

hb_nfc_err_t slix_get_system_info(const iso15693_tag_t *tag, slix_system_info_t *out_info) {
  if (tag == NULL || out_info == NULL)
    return HB_NFC_ERR_PARAM;
  uint8_t bytes[SLIX_SYSTEM_INFO_BYTES];
  hb_nfc_err_t err = exchange(tag, SLIX_COMMAND_SYSTEM_INFO, NULL, 0, bytes, sizeof(bytes));
  if (err != HB_NFC_OK)
    return err;
  slix_system_info_t info = {
      .protection_pointer = bytes[0], .protection_condition = bytes[1], .lock_bits = bytes[2]};
  for (size_t i = 0; i < sizeof(info.features); i++)
    info.features |= (uint32_t)bytes[SLIX_REQUEST_HEADER + i] << (i * SLIX_BYTE_BITS);
  *out_info = info;
  return HB_NFC_OK;
}

hb_nfc_err_t slix_read_signature(const iso15693_tag_t *tag, uint8_t *out_signature) {
  if (tag == NULL || out_signature == NULL)
    return HB_NFC_ERR_PARAM;
  return exchange(tag, SLIX_COMMAND_SIGNATURE, NULL, 0, out_signature, SLIX_SIGNATURE_BYTES);
}

hb_nfc_err_t slix_authenticate(const iso15693_tag_t *tag,
                               slix_password_t type,
                               const uint8_t password[SLIX_PASSWORD_BYTES]) {
  if ((unsigned)type >= SLIX_PASSWORD_COUNT || password == NULL ||
      (type != SLIX_PASSWORD_PRIVACY && tag == NULL))
    return HB_NFC_ERR_PARAM;
  uint8_t nonce[SLIX_NONCE_BYTES];
  hb_nfc_err_t err = exchange(NULL, SLIX_COMMAND_RANDOM, NULL, 0, nonce, sizeof(nonce));
  if (err != HB_NFC_OK)
    return err;
  uint8_t args[1 + SLIX_PASSWORD_BYTES] = {(uint8_t)(1U << type)};
  for (size_t i = 0; i < SLIX_PASSWORD_BYTES; i++)
    args[1 + i] = password[i] ^ nonce[i % SLIX_NONCE_BYTES];
  err = exchange(
      type == SLIX_PASSWORD_PRIVACY ? NULL : tag, SLIX_COMMAND_AUTH, args, sizeof(args), NULL, 0);
  memset(args, 0, sizeof(args));
  return err;
}
