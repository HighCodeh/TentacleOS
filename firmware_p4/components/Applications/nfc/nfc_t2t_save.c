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

#include "nfc_t2t_file.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_atomic.h"

#define TEXT_CAPACITY    16384U
#define PAGE_SIZE        4U
#define ULC_PUBLIC_PAGES 44U
#define UID_PREFIX_SIZE  3U
#define BYTE_BITS        8U

static const char *TAG = "NFC_T2T_SAVE";

static bool append(char *text, size_t *offset, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int count = vsnprintf(text + *offset, TEXT_CAPACITY - *offset, format, args);
  va_end(args);
  if (count < 0 || (size_t)count >= TEXT_CAPACITY - *offset)
    return false;
  *offset += (size_t)count;
  return true;
}

static bool append_hex(char *text, size_t *offset, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++)
    if (!append(text, offset, "%02X%s", data[i], i + 1 == length ? "\n" : " "))
      return false;
  return true;
}

esp_err_t nfc_t2t_file_save(const char *path, const nfc_t2t_file_t *card) {
  static const char *names[T2T_TAG_COUNT] = {"NTAG213",
                                             "NTAG215",
                                             "NTAG216",
                                             "Mifare Ultralight C",
                                             "Mifare Ultralight",
                                             "Mifare Ultralight 11",
                                             "Mifare Ultralight 21",
                                             "NTAG210",
                                             "NTAG212",
                                             "Mifare Ultralight AES",
                                             "NTAG I2C 1K",
                                             "NTAG I2C 2K",
                                             "NTAG I2C Plus 1K",
                                             "NTAG I2C Plus 2K"};
  static const uint16_t pages[T2T_TAG_COUNT] = {
      45, 135, 231, 48, 16, 20, 41, 20, 41, 60, 231, 485, 236, 492};
  if (path == NULL || card == NULL || (unsigned)card->config.type >= T2T_TAG_COUNT)
    return ESP_ERR_INVALID_ARG;
  const t2t_emu_config_t *config = &card->config;
  size_t total = pages[config->type];
  if (card->length != total * PAGE_SIZE || config->uid_len != sizeof(config->uid) ||
      memcmp(card->memory, config->uid, UID_PREFIX_SIZE) ||
      memcmp(card->memory + PAGE_SIZE, config->uid + UID_PREFIX_SIZE, PAGE_SIZE))
    return ESP_ERR_INVALID_ARG;
  if ((config->type == T2T_TAG_ULTRALIGHT_EV1_11 || config->type == T2T_TAG_ULTRALIGHT_EV1_21 ||
       config->type == T2T_TAG_ULTRALIGHT_AES || config->type >= T2T_TAG_NTAG_I2C_1K) &&
      !config->has_version)
    return ESP_ERR_INVALID_ARG;
  if (config->type >= T2T_TAG_NTAG_I2C_1K &&
      !t2t_i2c_version_matches(config->type, config->version))
    return ESP_ERR_INVALID_ARG;
  for (size_t i = 0; i < T2T_COUNTER_COUNT; i++)
    if (config->counters[i] > T2T_COUNTER_MAX)
      return ESP_ERR_INVALID_ARG;
  if ((config->aes_signature_known & ~T2T_AES_SIGNATURE_KNOWN) ||
      (unsigned)config->aes_signature_lock >= T2T_SIGNATURE_LOCK_COUNT)
    return ESP_ERR_INVALID_ARG;
  if (config->counter > T2T_COUNTER_MAX)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = ESP_ERR_NO_MEM;
  char *text = malloc(TEXT_CAPACITY);
  if (text == NULL) {
    ESP_LOGE(TAG, "No memory for Type 2 export");
    goto cleanup;
  }
  size_t offset = 0;
  size_t read =
      config->type == T2T_TAG_ULTRALIGHT_C && !config->enable_ulc_auth ? ULC_PUBLIC_PAGES : total;
  uint16_t known = config->enable_ulc_auth ? UINT16_MAX : config->ulc_key_known;
  uint8_t secrets[] = {config->enable_pwd || config->pwd_known,
                       config->enable_pwd || config->pack_known,
                       (uint8_t)known,
                       (uint8_t)(known >> BYTE_BITS)};
  error = ESP_ERR_INVALID_SIZE;
  if (!append(text,
              &offset,
              "Filetype: Flipper NFC device\nVersion: 4\nDevice type: NTAG/Ultralight\nUID: ") ||
      !append_hex(text, &offset, config->uid, sizeof(config->uid)) ||
      !append(text,
              &offset,
              "ATQA: 00 44\nSAK: 00\nNTAG/Ultralight type: %s\nPages total: %zu\nPages read: "
              "%zu\nHighboy known secrets: ",
              names[config->type],
              total,
              read) ||
      !append_hex(text, &offset, secrets, sizeof(secrets)) ||
      !append(text,
              &offset,
              "Failed authentication attempts: %lu\n",
              (unsigned long)config->failed_auth_attempts))
    goto cleanup;
  if (config->has_version && (!append(text, &offset, "Mifare version: ") ||
                              !append_hex(text, &offset, config->version, sizeof(config->version))))
    goto cleanup;
  if (config->has_signature &&
      (!append(text, &offset, "Signature: ") ||
       !append_hex(text, &offset, config->signature, sizeof(config->signature))))
    goto cleanup;
  for (size_t i = 0; i < T2T_COUNTER_COUNT; i++) {
    bool is_ntag_counter = i == T2T_COUNTER_COUNT - 1 && config->has_counter;
    if (((config->counter_known & (1U << i)) || is_ntag_counter) &&
        !append(text,
                &offset,
                "Counter %zu: %lu\n",
                i,
                (unsigned long)(is_ntag_counter ? config->counter : config->counters[i])))
      goto cleanup;
    if ((config->tearing_known & (1U << i)) &&
        !append(text, &offset, "Tearing %zu: %02X\n", i, config->tearing[i]))
      goto cleanup;
  }
  if (config->type == T2T_TAG_ULTRALIGHT_AES) {
    uint8_t keys[T2T_AES_KEY_COUNT * 2];
    for (size_t i = 0; i < T2T_AES_KEY_COUNT; i++) {
      keys[i * 2] = (uint8_t)config->aes_key_known[i];
      keys[i * 2 + 1] = (uint8_t)(config->aes_key_known[i] >> BYTE_BITS);
    }
    if (!append(text, &offset, "Highboy AES known keys: ") ||
        !append_hex(text, &offset, keys, sizeof(keys)))
      goto cleanup;
    uint16_t signature_known =
        config->has_aes_signature ? T2T_AES_SIGNATURE_KNOWN : config->aes_signature_known;
    uint8_t signature_state[] = {(uint8_t)signature_known,
                                 (uint8_t)(signature_known >> BYTE_BITS),
                                 (uint8_t)config->aes_signature_lock};
    if (!append(text, &offset, "Highboy AES signature state: ") ||
        !append_hex(text, &offset, signature_state, sizeof(signature_state)))
      goto cleanup;
    if (signature_known &&
        (!append(text, &offset, "AES signature: ") ||
         !append_hex(text, &offset, config->aes_signature, sizeof(config->aes_signature))))
      goto cleanup;
  }
  for (size_t i = 0; i < total; i++)
    if (!append(text, &offset, "Page %zu: ", i) ||
        !append_hex(text, &offset, card->memory + i * PAGE_SIZE, PAGE_SIZE))
      goto cleanup;
  error = storage_write_atomic(path, text, offset);
cleanup:
  free(text);
  return error;
}
