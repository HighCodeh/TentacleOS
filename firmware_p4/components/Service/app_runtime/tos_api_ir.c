// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tos_api.h"

#include <string.h>

#include "esp_log.h"

#include "host_link_local_ir.h"
#include "host_link_local_rf.h"
#include "spi_protocol.h"
#include "tos_app_ctx.h"

#define IR_LOCK_MS        3000
#define IR_TIMEOUT_SIZE   2
#define IR_BYTE_BITS      8
#define IR_ADDRESS_OFFSET 1
#define IR_COMMAND_OFFSET 5
#define IR_REPEAT_OFFSET  9

static const char *TAG = "TOS_IR";

static esp_err_t execute(host_rf_request_t *request, uint32_t capability) {
  if (!tos_app_cap_check(capability))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(IR_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error =
      tos_api_get()->should_stop() ? ESP_ERR_INVALID_STATE : host_local_rf_execute(request);
  host_local_rf_unlock();
  if (error != ESP_OK)
    ESP_LOGD(TAG, "IR operation ended: %s", esp_err_to_name(error));
  return error;
}

static esp_err_t receive_code(tos_ir_code_t *out, uint32_t timeout_ms) {
  if (out == NULL || timeout_ms == 0 || timeout_ms > HOST_LOCAL_IR_TIMEOUT_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  uint8_t payload[IR_TIMEOUT_SIZE] = {(uint8_t)timeout_ms, (uint8_t)(timeout_ms >> IR_BYTE_BITS)};
  uint8_t response[HOST_RF_RESPONSE_MAX] = {0};
  host_rf_request_t request = {.command = SPI_ID_IR_RX_FRAME,
                               .payload = payload,
                               .length = sizeof(payload),
                               .out = response,
                               .capacity = sizeof(response)};
  esp_err_t error = execute(&request, TOS_CAP_RADIO_RX);
  if (error != ESP_OK)
    return error;
  if (request.out_length != HOST_LOCAL_IR_CODE_SIZE || response[0] > TOS_IR_NEC42 ||
      response[IR_REPEAT_OFFSET] > 1)
    return ESP_ERR_INVALID_SIZE;
  out->protocol = response[0];
  memcpy(&out->address, response + IR_ADDRESS_OFFSET, sizeof(out->address));
  memcpy(&out->command, response + IR_COMMAND_OFFSET, sizeof(out->command));
  out->repeat = response[IR_REPEAT_OFFSET] != 0;
  return ESP_OK;
}

static esp_err_t send_code(const tos_ir_code_t *code) {
  if (code == NULL || code->protocol > TOS_IR_NEC42)
    return ESP_ERR_INVALID_ARG;
  uint8_t payload[HOST_LOCAL_IR_CODE_SIZE] = {code->protocol};
  memcpy(payload + IR_ADDRESS_OFFSET, &code->address, sizeof(code->address));
  memcpy(payload + IR_COMMAND_OFFSET, &code->command, sizeof(code->command));
  payload[IR_REPEAT_OFFSET] = code->repeat;
  uint8_t response[HOST_RF_RESPONSE_MAX];
  host_rf_request_t request = {.command = SPI_ID_IR_TX,
                               .payload = payload,
                               .length = sizeof(payload),
                               .out = response,
                               .capacity = sizeof(response)};
  return execute(&request, TOS_CAP_RADIO_TX);
}

const tos_ir_api_t tos_ir_api_impl = {.receive = receive_code, .send = send_code};
