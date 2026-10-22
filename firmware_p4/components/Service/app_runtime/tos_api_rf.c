// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tos_api.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "host_link_local_rf.h"
#include "spi_protocol.h"
#include "tos_app_ctx.h"

#define RF_LOCK_WAIT_MS        3000
#define RF_RFID_TIMEOUT_MIN_MS 40
#define RF_RFID_TIMEOUT_MAX_MS 500
#define RF_NFC_TIMEOUT_MAX_MS  2000
#define RF_US_PER_MS           1000
#define RF_TECH_COUNT          4
#define RF_TECH_A              0
#define RF_TECH_B              1
#define RF_TECH_F              2
#define RF_TECH_V              3
#define RF_UID_LENGTH_OFFSET   3
#define RF_UID_OFFSET          4
#define RF_ATQA_OFFSET         14
#define RF_SAK_OFFSET          16
#define RF_RFID_RESULT_SIZE    9
#define RF_ID_BYTES            5
#define RF_ID_BITS             40
#define RF_BYTE_BITS           8

static const char *TAG = "TOS_RF";

static esp_err_t request_read(host_rf_request_t *request) {
  if (!tos_app_cap_check(TOS_CAP_RADIO_RX))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(RF_LOCK_WAIT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error =
      tos_api_get()->should_stop() ? ESP_ERR_INVALID_STATE : host_local_rf_execute(request);
  host_local_rf_unlock();
  if (error != ESP_OK)
    return error;
  if (request->out_length == 1 && request->out[0] == 0)
    return ESP_ERR_NOT_FOUND;
  return ESP_OK;
}

static esp_err_t read_rfid(tos_rfid_tag_t *out, uint32_t timeout_ms) {
  if (out == NULL || timeout_ms < RF_RFID_TIMEOUT_MIN_MS || timeout_ms > RF_RFID_TIMEOUT_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  uint8_t payload[] = {(uint8_t)timeout_ms, (uint8_t)(timeout_ms >> RF_BYTE_BITS)};
  uint8_t response[HOST_RF_RESPONSE_MAX] = {0};
  host_rf_request_t request = {.command = SPI_ID_RFID_READ,
                               .payload = payload,
                               .length = sizeof(payload),
                               .out = response,
                               .capacity = sizeof(response)};
  esp_err_t error = request_read(&request);
  if (error != ESP_OK)
    return error;
  if (request.out_length != RF_RFID_RESULT_SIZE || response[0] != 1)
    return ESP_ERR_INVALID_SIZE;
  const uint8_t *id = response + 1;
  snprintf(out->id, sizeof(out->id), "%02X%02X%02X%02X%02X", id[0], id[1], id[2], id[3], id[4]);
  memcpy(out->protocol, "EM4100", sizeof("EM4100"));
  for (uint32_t index = 0; index < RF_ID_BYTES; ++index)
    out->raw_value = (out->raw_value << RF_BYTE_BITS) | id[index];
  out->card_number = (uint32_t)out->raw_value;
  out->bit_count = RF_ID_BITS;
  return ESP_OK;
}

static esp_err_t poll_nfc(tos_nfc_tag_t *out, uint32_t timeout_ms) {
  if (out == NULL || timeout_ms == 0 || timeout_ms > RF_NFC_TIMEOUT_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * RF_US_PER_MS;
  for (uint8_t tech = RF_TECH_A; tech < RF_TECH_COUNT; ++tech) {
    uint8_t response[HOST_RF_RESPONSE_MAX] = {0};
    host_rf_request_t request = {.command = SPI_ID_NFC_READ,
                                 .payload = &tech,
                                 .length = sizeof(tech),
                                 .out = response,
                                 .capacity = sizeof(response)};
    esp_err_t error = request_read(&request);
    if (error == ESP_ERR_NOT_FOUND) {
      if (esp_timer_get_time() >= deadline)
        break;
      continue;
    }
    if (error != ESP_OK)
      return error;
    if (request.out_length != HOST_RF_RESPONSE_MAX || response[0] != 1 ||
        response[RF_UID_LENGTH_OFFSET] == 0 || response[RF_UID_LENGTH_OFFSET] > sizeof(out->uid)) {
      ESP_LOGW(TAG, "Invalid NFC backend result");
      return ESP_ERR_INVALID_SIZE;
    }
    out->uid_len = response[RF_UID_LENGTH_OFFSET];
    memcpy(out->uid, response + RF_UID_OFFSET, out->uid_len);
    memcpy(out->atqa, response + RF_ATQA_OFFSET, sizeof(out->atqa));
    out->sak = response[RF_SAK_OFFSET];
    switch (tech) {
      case RF_TECH_A:
        out->protocol = TOS_NFC_ISO14443A;
        break;
      case RF_TECH_B:
        out->protocol = TOS_NFC_ISO14443B;
        break;
      case RF_TECH_F:
        out->protocol = TOS_NFC_FELICA;
        break;
      case RF_TECH_V:
        out->protocol = TOS_NFC_ISO15693;
        break;
      default:
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
  }
  return ESP_ERR_NOT_FOUND;
}

static esp_err_t block_worker(void *context) {
  return host_rf_nfc_block(context);
}

static esp_err_t access_block(host_rf_nfc_block_t *request) {
  uint32_t caps = request->is_write ? TOS_CAP_RADIO_TX : TOS_CAP_RADIO_RX;
  if (!tos_app_cap_check(caps))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(RF_LOCK_WAIT_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error = tos_api_get()->should_stop() ? ESP_ERR_INVALID_STATE
                                                 : host_local_rf_call(block_worker, request);
  host_local_rf_unlock();
  return error;
}

static esp_err_t read_block(uint8_t block,
                            const uint8_t key[HOST_RF_NFC_KEY_SIZE],
                            uint8_t out[HOST_RF_NFC_BLOCK_SIZE]) {
  if (key == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, HOST_RF_NFC_BLOCK_SIZE);
  host_rf_nfc_block_t request = {.block = block};
  memcpy(request.key, key, sizeof(request.key));
  esp_err_t error = access_block(&request);
  if (error == ESP_OK)
    memcpy(out, request.data, sizeof(request.data));
  return error;
}

static esp_err_t write_block(uint8_t block,
                             const uint8_t key[HOST_RF_NFC_KEY_SIZE],
                             const uint8_t data[HOST_RF_NFC_BLOCK_SIZE]) {
  if (key == NULL || data == NULL)
    return ESP_ERR_INVALID_ARG;
  host_rf_nfc_block_t request = {.block = block, .is_write = true};
  memcpy(request.key, key, sizeof(request.key));
  memcpy(request.data, data, sizeof(request.data));
  return access_block(&request);
}

const tos_nfc_api_t tos_nfc_api_impl = {
    .poll = poll_nfc, .read_block = read_block, .write_block = write_block};
const tos_rfid_api_t tos_rfid_api_impl = {.read = read_rfid};
