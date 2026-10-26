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

#include "host_link_local_ir.h"

#include <string.h>

#include "esp_log.h"

#include "ir.h"
#include "ir_lease.h"
#include "spi_protocol.h"

#define IR_TIMEOUT_SIZE   2
#define IR_BYTE_BITS      8
#define IR_ADDRESS_OFFSET 1
#define IR_COMMAND_OFFSET 5
#define IR_REPEAT_OFFSET  9
#define IR_PROTOCOL_FIRST IR_PROTO_NEC
#define IR_PROTOCOL_LAST  IR_PROTO_NEC42

static const char *TAG = "LOCAL_IR";

bool host_local_ir_is_command(uint16_t command) {
  return command == SPI_ID_IR_TX || command == SPI_ID_IR_RX_FRAME;
}

esp_err_t host_local_ir_execute(host_rf_request_t *request) {
  if (request == NULL || request->payload == NULL || request->out == NULL ||
      request->capacity < HOST_LOCAL_IR_CODE_SIZE || !host_local_ir_is_command(request->command))
    return ESP_ERR_INVALID_ARG;
  request->out_length = 0;
  bool is_receive = request->command == SPI_ID_IR_RX_FRAME;
  ir_data_t data = {0};
  uint16_t timeout_ms = 0;
  if (is_receive) {
    if (request->length != IR_TIMEOUT_SIZE)
      return ESP_ERR_INVALID_ARG;
    timeout_ms = request->payload[0] | ((uint16_t)request->payload[1] << IR_BYTE_BITS);
    if (timeout_ms == 0 || timeout_ms > HOST_LOCAL_IR_TIMEOUT_MAX_MS)
      return ESP_ERR_INVALID_ARG;
  } else {
    if (request->length != HOST_LOCAL_IR_CODE_SIZE ||
        request->payload[0] > IR_PROTOCOL_LAST - IR_PROTOCOL_FIRST ||
        request->payload[IR_REPEAT_OFFSET] > 1)
      return ESP_ERR_INVALID_ARG;
    data.protocol = request->payload[0] + IR_PROTOCOL_FIRST;
    memcpy(&data.address, request->payload + IR_ADDRESS_OFFSET, sizeof(data.address));
    memcpy(&data.command, request->payload + IR_COMMAND_OFFSET, sizeof(data.command));
    data.repeat = request->payload[IR_REPEAT_OFFSET] != 0;
  }
  esp_err_t error = ir_lease_acquire();
  if (error != ESP_OK)
    return error;
  ir_rx_deinit();
  ir_tx_deinit();
  error = ir_lease_release();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Previous IR cleanup still pending");
    return error;
  }
  error = ir_lease_acquire();
  if (error != ESP_OK)
    return error;
  if (is_receive) {
    error = ir_rx_init();
    if (error == ESP_OK) {
      ir_rx_prime();
      error = ir_receive(&data, timeout_ms);
    }
    if (error == ESP_OK) {
      if (data.protocol < IR_PROTOCOL_FIRST || data.protocol > IR_PROTOCOL_LAST) {
        error = ESP_ERR_NOT_SUPPORTED;
      } else {
        request->out[0] = data.protocol - IR_PROTOCOL_FIRST;
        memcpy(request->out + IR_ADDRESS_OFFSET, &data.address, sizeof(data.address));
        memcpy(request->out + IR_COMMAND_OFFSET, &data.command, sizeof(data.command));
        request->out[IR_REPEAT_OFFSET] = data.repeat;
        request->out_length = HOST_LOCAL_IR_CODE_SIZE;
      }
    }
  } else {
    error = ir_tx_init();
    if (error == ESP_OK)
      error = ir_send(&data);
  }
  ir_rx_deinit();
  ir_tx_deinit();
  esp_err_t cleanup_error = ir_lease_release();
  return cleanup_error != ESP_OK ? cleanup_error : error;
}
