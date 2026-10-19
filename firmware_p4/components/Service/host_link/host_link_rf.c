// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_link_rf.h"

#include <string.h>

#include "esp_log.h"

#include "highboy_nfc.h"
#include "lf_rfid.h"
#include "nfc_manager.h"
#include "power_manager.h"
#include "rf_frontend.h"
#include "spi_protocol.h"

#define HOST_RF_OP_READ              1
#define HOST_RF_OP_RECOVER           2
#define HOST_RF_OP_STATUS            3
#define HOST_RF_OP_MASK              0xFF
#define HOST_RF_CATEGORY_SHIFT       8
#define HOST_RF_TIMEOUT_MIN_MS       40
#define HOST_RF_TIMEOUT_MAX_MS       500
#define HOST_RF_RFID_RESPONSE_SIZE   9
#define HOST_RF_STATUS_SIZE          2
#define HOST_RF_NFC_UID_SIZE         10
#define HOST_RF_NFC_UID_OFFSET       4
#define HOST_RF_NFC_ATQA_OFFSET      14
#define HOST_RF_NFC_SAK_OFFSET       16
#define HOST_RF_RFID_INVERTED_OFFSET 6
#define HOST_RF_RFID_SAMPLES_OFFSET  7
#define HOST_RF_BYTE_SHIFT           8
#define HOST_RF_TAG_FOUND            1
#define HOST_RF_TECH_OFFSET          1
#define HOST_RF_PROTOCOL_OFFSET      2
#define HOST_RF_UID_LENGTH_OFFSET    3

static const char *TAG = "HOST_LINK_RF";

// Accessed exclusively by the host-link core worker, including deferred disconnect cleanup.
static rf_frontend_mode_t s_reserved_mode = RF_FRONTEND_IDLE;
static bool s_has_no_sleep;

static esp_err_t release_antenna(void) {
  esp_err_t error = ESP_OK;
  if (s_reserved_mode != RF_FRONTEND_IDLE) {
    if (s_reserved_mode == RF_FRONTEND_NFC_13MHZ)
      error = highboy_nfc_field_off();
    else
      lf_rfid_field_stop();
    if (error == ESP_OK)
      error = rf_frontend_unreserve(s_reserved_mode);
    if (error == ESP_OK)
      s_reserved_mode = RF_FRONTEND_IDLE;
  }
  if (error == ESP_OK && s_has_no_sleep) {
    error = power_manager_no_sleep_release();
    if (error == ESP_OK)
      s_has_no_sleep = false;
  }
  return error;
}

void host_rf_stop(void) {
  if (release_antenna() != ESP_OK)
    ESP_LOGW(TAG, "RF cleanup failed; recovery remains pending");
}

static esp_err_t read_nfc(host_rf_request_t *request) {
  highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
  esp_err_t error = highboy_nfc_init(&config);
  if (error != ESP_OK)
    return error;
  hb_nfc_card_data_t card = {0};
  nfc_manager_tech_t tech = (nfc_manager_tech_t)request->payload[0];
  error = nfc_manager_poll(tech, &card);
  if (error != ESP_OK)
    return error;
  uint8_t *out = request->out;
  out[0] = HOST_RF_TAG_FOUND;
  out[HOST_RF_TECH_OFFSET] = (uint8_t)tech;
  out[HOST_RF_PROTOCOL_OFFSET] = (uint8_t)card.protocol;
  const uint8_t *uid = NULL;
  uint8_t length = 0;
  switch (tech) {
    case NFC_MANAGER_TECH_A:
      uid = card.iso14443a.uid;
      length = card.iso14443a.uid_len;
      memcpy(out + HOST_RF_NFC_ATQA_OFFSET, card.iso14443a.atqa, sizeof(card.iso14443a.atqa));
      out[HOST_RF_NFC_SAK_OFFSET] = card.iso14443a.sak;
      break;
    case NFC_MANAGER_TECH_B:
      uid = card.data.iso14443b.pupi;
      length = sizeof(card.data.iso14443b.pupi);
      break;
    case NFC_MANAGER_TECH_F:
      uid = card.data.felica.idm;
      length = sizeof(card.data.felica.idm);
      break;
    case NFC_MANAGER_TECH_V:
      uid = card.data.iso15693.uid;
      length = sizeof(card.data.iso15693.uid);
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }
  if (length == 0 || length > HOST_RF_NFC_UID_SIZE)
    return ESP_ERR_INVALID_SIZE;
  out[HOST_RF_UID_LENGTH_OFFSET] = length;
  memcpy(out + HOST_RF_NFC_UID_OFFSET, uid, length);
  request->out_length = HOST_RF_RESPONSE_MAX;
  return ESP_OK;
}

static esp_err_t read_rfid(host_rf_request_t *request) {
  esp_err_t error = lf_rfid_init();
  if (error != ESP_OK)
    return error;
  error = lf_rfid_field_start(LF_RFID_NOMINAL_HZ, LF_RFID_DEFAULT_DUTY_PERMILLE);
  if (error != ESP_OK)
    return error;
  uint32_t timeout =
      (uint32_t)request->payload[0] | ((uint32_t)request->payload[1] << HOST_RF_BYTE_SHIFT);
  lf_rfid_em4100_t tag = {0};
  error = lf_rfid_read_em4100(timeout, &tag);
  if (error != ESP_OK)
    return error;
  request->out[0] = HOST_RF_TAG_FOUND;
  memcpy(request->out + 1, tag.bytes, sizeof(tag.bytes));
  request->out[HOST_RF_RFID_INVERTED_OFFSET] = tag.inverted;
  request->out[HOST_RF_RFID_SAMPLES_OFFSET] = (uint8_t)tag.samples_used;
  request->out[HOST_RF_RFID_SAMPLES_OFFSET + 1] = (uint8_t)(tag.samples_used >> HOST_RF_BYTE_SHIFT);
  request->out_length = HOST_RF_RFID_RESPONSE_SIZE;
  return ESP_OK;
}

uint8_t host_rf_handle(host_rf_request_t *request) {
  if (request == NULL)
    return SPI_STATUS_INVALID_ARG;
  request->out_length = 0;
  uint8_t category = (uint8_t)(request->command >> HOST_RF_CATEGORY_SHIFT);
  uint8_t operation = (uint8_t)(request->command & HOST_RF_OP_MASK);
  if (category != SPI_CAT_NFC && category != SPI_CAT_RFID)
    return SPI_STATUS_UNSUPPORTED;
  if (request->out == NULL || request->capacity < HOST_RF_RESPONSE_MAX ||
      (request->length != 0 && request->payload == NULL))
    return SPI_STATUS_INVALID_ARG;
  rf_frontend_mode_t mode = category == SPI_CAT_NFC ? RF_FRONTEND_NFC_13MHZ : RF_FRONTEND_LF_125KHZ;
  if (operation == HOST_RF_OP_STATUS) {
    if (request->length != 0)
      return SPI_STATUS_INVALID_ARG;
    request->out[0] = s_reserved_mode != RF_FRONTEND_IDLE || s_has_no_sleep;
    request->out[1] = (uint8_t)rf_frontend_get_mode();
    request->out_length = HOST_RF_STATUS_SIZE;
    return SPI_STATUS_OK;
  }
  if (operation == HOST_RF_OP_RECOVER) {
    if (request->length != 0)
      return SPI_STATUS_INVALID_ARG;
    if (s_reserved_mode != RF_FRONTEND_IDLE && s_reserved_mode != mode)
      return SPI_STATUS_BUSY;
    return release_antenna() == ESP_OK ? SPI_STATUS_OK : SPI_STATUS_ERROR;
  }
  if (operation != HOST_RF_OP_READ)
    return SPI_STATUS_UNSUPPORTED;
  if (category == SPI_CAT_NFC) {
    if (request->length != 1 || request->payload[0] >= NFC_MANAGER_TECH_COUNT)
      return SPI_STATUS_INVALID_ARG;
  } else {
    if (request->length != sizeof(uint16_t))
      return SPI_STATUS_INVALID_ARG;
    uint32_t timeout =
        (uint32_t)request->payload[0] | ((uint32_t)request->payload[1] << HOST_RF_BYTE_SHIFT);
    if (timeout < HOST_RF_TIMEOUT_MIN_MS || timeout > HOST_RF_TIMEOUT_MAX_MS)
      return SPI_STATUS_INVALID_ARG;
  }
  if (s_reserved_mode != RF_FRONTEND_IDLE || s_has_no_sleep || rf_frontend_reserve(mode) != ESP_OK)
    return SPI_STATUS_BUSY;
  s_reserved_mode = mode;
  memset(request->out, 0, HOST_RF_RESPONSE_MAX);
  esp_err_t error = power_manager_no_sleep_acquire();
  if (error == ESP_OK) {
    s_has_no_sleep = true;
    error = category == SPI_CAT_NFC ? read_nfc(request) : read_rfid(request);
  }
  esp_err_t cleanup_error = release_antenna();
  if (cleanup_error != ESP_OK || (error != ESP_OK && error != ESP_ERR_NOT_FOUND)) {
    request->out_length = 0;
    return error == ESP_ERR_INVALID_STATE && cleanup_error == ESP_OK ? SPI_STATUS_BUSY
                                                                     : SPI_STATUS_ERROR;
  }
  if (error == ESP_ERR_NOT_FOUND)
    request->out_length = 1;
  return SPI_STATUS_OK;
}
