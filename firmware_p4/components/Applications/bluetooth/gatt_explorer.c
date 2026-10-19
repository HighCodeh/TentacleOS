// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gatt_explorer.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"

#define EXPLORER_ADDRESS_SIZE     6
#define EXPLORER_ADDRESS_TYPE_MAX 3
#define EXPLORER_START_SIZE       7
#define EXPLORER_TIMEOUT_MS       10000

static const char *TAG = "GATT_EXPLORER";

static esp_err_t
command(uint16_t id, const uint8_t *payload, uint8_t length, spi_gatt_status_t *out);

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type) {
  if (addr == NULL || addr_type > EXPLORER_ADDRESS_TYPE_MAX)
    return false;
  uint8_t payload[EXPLORER_START_SIZE];
  memcpy(payload, addr, EXPLORER_ADDRESS_SIZE);
  payload[EXPLORER_ADDRESS_SIZE] = addr_type;
  return command(SPI_ID_BT_APP_GATT_EXP, payload, sizeof(payload), NULL) == ESP_OK;
}

bool gatt_explorer_is_busy(void) {
  spi_gatt_status_t status;
  return gatt_explorer_get_status(&status) != ESP_OK ||
         (status.state != SPI_GATT_IDLE && status.state != SPI_GATT_ERROR);
}

esp_err_t gatt_explorer_get_status(spi_gatt_status_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;
  return command(SPI_ID_BT_GATT_STATUS, NULL, 0, out);
}

esp_err_t gatt_explorer_get_record(uint8_t index, spi_gatt_record_t *out) {
  if (out == NULL || index >= SPI_GATT_RECORD_MAX)
    return ESP_ERR_INVALID_ARG;
  spi_header_t header;
  uint8_t response[SPI_MAX_PAYLOAD];
  esp_err_t error = spi_bridge_send_command(SPI_ID_BT_GATT_RECORD,
                                            &index,
                                            sizeof(index),
                                            &header,
                                            response,
                                            sizeof(response),
                                            EXPLORER_TIMEOUT_MS);
  if (error != ESP_OK)
    return error;
  if (header.length != sizeof(*out) + SPI_RESP_STATUS_SIZE || response[0] != SPI_STATUS_OK)
    return ESP_ERR_INVALID_RESPONSE;
  memcpy(out, response + SPI_RESP_STATUS_SIZE, sizeof(*out));
  return ESP_OK;
}

esp_err_t gatt_explorer_read(uint16_t handle) {
  return command(SPI_ID_BT_GATT_READ, (const uint8_t *)&handle, sizeof(handle), NULL);
}

esp_err_t gatt_explorer_stop(void) {
  return command(SPI_ID_BT_GATT_STOP, NULL, 0, NULL);
}

static esp_err_t
command(uint16_t id, const uint8_t *payload, uint8_t length, spi_gatt_status_t *out) {
  spi_header_t header;
  uint8_t response[SPI_MAX_PAYLOAD];
  esp_err_t error = spi_bridge_send_command(
      id, payload, length, &header, response, sizeof(response), EXPLORER_TIMEOUT_MS);
  if (error != ESP_OK)
    return error;
  size_t expected = SPI_RESP_STATUS_SIZE + (out != NULL ? sizeof(*out) : 0);
  if (header.length != expected || response[0] != SPI_STATUS_OK) {
    ESP_LOGW(TAG, "C5 GATT command failed: %u", (unsigned)id);
    return ESP_ERR_INVALID_RESPONSE;
  }
  if (out != NULL) {
    memcpy(out, response + SPI_RESP_STATUS_SIZE, sizeof(*out));
    if (out->state >= SPI_GATT_STATE_COUNT || out->count > SPI_GATT_RECORD_MAX ||
        out->value_length > SPI_GATT_VALUE_MAX)
      return ESP_ERR_INVALID_RESPONSE;
  }
  return ESP_OK;
}
