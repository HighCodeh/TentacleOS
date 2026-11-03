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

#include "ir.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ir_lease.h"
#include "led_control.h"
#include "resource_mgr.h"
#include "spi_bridge.h"

#define IR_RPC_MS     250
#define IR_POLL_MS    20
#define IR_CONTROL_MS 1500
#define IR_US_PER_MS  1000
#define IR_BYTE_BITS  8
#define IR_BYTE_MASK  0xff
#define IR_MODE_NONE  0
#define IR_MODE_RX    1
#define IR_MODE_TX    2
#define IR_MODE_TORCH 3

_Static_assert(sizeof(spi_ir_request_t) == SPI_IR_HEADER_SIZE, "IR request size");
_Static_assert(sizeof(spi_ir_reply_t) == SPI_IR_HEADER_SIZE, "IR reply size");

static const char *TAG = "IR";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_busy;
static TaskHandle_t s_owner;
static uint8_t s_mode;
static uint64_t s_client;
static uint64_t s_epoch;
static uint32_t s_operation;
static res_handle_t s_resource = RES_HANDLE_NONE;
static rmt_symbol_word_t s_last_raw[IR_MAX_SYMBOLS];
static size_t s_last_raw_count;

static bool enter(void) {
  if (xPortInIsrContext() || !ir_lease_enter())
    return false;
  portENTER_CRITICAL(&s_lock);
  bool allowed = !s_busy && (s_owner == NULL || s_owner == xTaskGetCurrentTaskHandle());
  if (allowed)
    s_busy = true;
  portEXIT_CRITICAL(&s_lock);
  if (!allowed)
    ir_lease_leave(true);
  return allowed;
}

static esp_err_t leave(esp_err_t result) {
  ir_lease_leave(s_mode != IR_MODE_NONE);
  portENTER_CRITICAL(&s_lock);
  s_busy = false;
  portEXIT_CRITICAL(&s_lock);
  return result;
}

static esp_err_t exchange(const spi_ir_request_t *request,
                          const uint8_t *chunk,
                          spi_ir_reply_t *out_reply,
                          uint8_t *out_chunk) {
  uint8_t payload[SPI_IR_HEADER_SIZE + SPI_IR_CHUNK_SYMBOLS * SPI_IR_SYMBOL_BYTES];
  uint8_t response[sizeof(payload) + 1];
  uint8_t length = sizeof(*request);
  memcpy(payload, request, length);
  if (request->action == SPI_IR_UPLOAD) {
    length += request->count * SPI_IR_SYMBOL_BYTES;
    memcpy(payload + sizeof(*request), chunk, length - sizeof(*request));
  }
  spi_header_t header = {0};
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_IR_REMOTE, payload, length, &header, response, sizeof(response), IR_RPC_MS);
  if (ret != ESP_OK)
    return ret;
  size_t expected = sizeof(*out_reply);
  if (request->action == SPI_IR_FETCH)
    expected += request->count * SPI_IR_SYMBOL_BYTES;
  if (header.length != expected)
    return ESP_ERR_INVALID_RESPONSE;
  memcpy(out_reply, response, sizeof(*out_reply));
  if (out_reply->revision != SPI_IR_REVISION || out_reply->reserved != 0 || out_reply->epoch == 0 ||
      out_reply->state >= SPI_IR_STATE_COUNT)
    return ESP_ERR_INVALID_RESPONSE;
  if (out_chunk != NULL)
    memcpy(out_chunk, response + sizeof(*out_reply), expected - sizeof(*out_reply));
  return ESP_OK;
}

static spi_ir_request_t request_for(uint8_t action) {
  return (spi_ir_request_t){
      .epoch = s_epoch, .operation = s_operation, .action = action, .revision = SPI_IR_REVISION};
}

static esp_err_t command(uint8_t action, spi_ir_reply_t *out_reply) {
  spi_ir_request_t request = request_for(action);
  return exchange(&request, NULL, out_reply, NULL);
}

static void release_local(void) {
  (void)resource_release(s_resource);
  s_resource = RES_HANDLE_NONE;
  s_mode = IR_MODE_NONE;
  s_epoch = 0;
  s_client = 0;
  portENTER_CRITICAL(&s_lock);
  s_owner = NULL;
  portEXIT_CRITICAL(&s_lock);
}

static esp_err_t close_remote(void) {
  if (s_mode == IR_MODE_NONE)
    return ESP_OK;
  int64_t deadline = esp_timer_get_time() + IR_CONTROL_MS * IR_US_PER_MS;
  esp_err_t ret = ESP_ERR_TIMEOUT;
  do {
    spi_ir_reply_t reply;
    ret = command(SPI_IR_STATUS, &reply);
    if (ret == ESP_OK) {
      if (reply.state == SPI_IR_CLOSED || (reply.epoch != s_epoch && reply.state == SPI_IR_IDLE)) {
        release_local();
        return ESP_OK;
      }
      if (reply.epoch == s_epoch)
        ret = command(SPI_IR_CLOSE, &reply);
    }
    vTaskDelay(pdMS_TO_TICKS(IR_POLL_MS));
  } while (esp_timer_get_time() < deadline);
  ESP_LOGW(TAG, "Remote release unconfirmed; IR ownership retained");
  return ret == ESP_OK ? ESP_ERR_TIMEOUT : ret;
}

static esp_err_t open_remote(uint8_t mode) {
  if (s_mode != IR_MODE_NONE)
    return s_mode == mode ? ESP_OK : ESP_ERR_INVALID_STATE;
  const res_request_t resource = {
      .id = RES_IR, .lane = RES_LANE_MAIN, .owner_kind = RES_OWNER_UI, .allow_preempt = false};
  esp_err_t ret = resource_acquire(&resource, &s_resource);
  if (ret != ESP_OK)
    return ret;
  do {
    esp_fill_random(&s_client, sizeof(s_client));
  } while (s_client == 0);
  s_mode = mode;
  portENTER_CRITICAL(&s_lock);
  s_owner = xTaskGetCurrentTaskHandle();
  portEXIT_CRITICAL(&s_lock);
  s_operation = 0;
  s_epoch = 0;
  const spi_ir_request_t request = {
      .epoch = s_client, .action = SPI_IR_OPEN, .revision = SPI_IR_REVISION};
  int64_t deadline = esp_timer_get_time() + IR_CONTROL_MS * IR_US_PER_MS;
  do {
    spi_ir_reply_t reply;
    ret = exchange(&request, NULL, &reply, NULL);
    if (ret == ESP_OK) {
      s_epoch = reply.epoch;
      if (reply.state == SPI_IR_IDLE && reply.operation == 0)
        return ESP_OK;
      ret = ESP_ERR_INVALID_STATE;
      break;
    }
    if (ret == ESP_ERR_NOT_SUPPORTED)
      break;
    vTaskDelay(pdMS_TO_TICKS(IR_POLL_MS));
  } while (esp_timer_get_time() < deadline);
  // OPEN never starts hardware; a lost OPEN reply cannot leave an active emitter.
  release_local();
  return ret;
}

static esp_err_t next_operation(void) {
  if (s_operation == UINT32_MAX)
    return ESP_ERR_INVALID_STATE;
  ++s_operation;
  return ESP_OK;
}

static esp_err_t wait_done(uint32_t timeout_ms, spi_ir_reply_t *out_reply) {
  int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * IR_US_PER_MS;
  do {
    esp_err_t ret = command(SPI_IR_STATUS, out_reply);
    if (ret == ESP_OK) {
      if (out_reply->epoch != s_epoch || out_reply->operation != s_operation ||
          out_reply->state == SPI_IR_CLOSED)
        return ESP_ERR_INVALID_STATE;
      if (out_reply->state == SPI_IR_DONE)
        return out_reply->result;
    } else if (ret == ESP_ERR_NOT_SUPPORTED)
      return ret;
    vTaskDelay(pdMS_TO_TICKS(IR_POLL_MS));
  } while (esp_timer_get_time() < deadline);
  return ESP_ERR_TIMEOUT;
}

static esp_err_t transmit(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier) {
  if (s_mode != IR_MODE_TX)
    return ESP_ERR_INVALID_STATE;
  if (symbols == NULL || count == 0 || count > SPI_IR_TX_SYMBOLS ||
      (carrier != 0 && (carrier < SPI_IR_CARRIER_MIN || carrier > SPI_IR_CARRIER_MAX)))
    return ESP_ERR_INVALID_ARG;
  uint32_t duration = 0;
  for (size_t i = 0; i < count; i++) {
    if (symbols[i].duration0 == 0 || (symbols[i].duration1 == 0 && i + 1 != count))
      return ESP_ERR_INVALID_ARG;
    duration += symbols[i].duration0 + symbols[i].duration1;
    if (duration > SPI_IR_MAX_US)
      return ESP_ERR_INVALID_SIZE;
  }
  esp_err_t ret = next_operation();
  if (ret != ESP_OK)
    return ret;
  spi_ir_reply_t reply;
  spi_ir_request_t request = request_for(SPI_IR_BEGIN);
  request.count = count;
  request.value = carrier;
  ret = exchange(&request, NULL, &reply, NULL);
  if (ret != ESP_OK)
    return ret;
  if (reply.epoch != s_epoch || reply.operation != s_operation || reply.state != SPI_IR_UPLOADING)
    return reply.result != ESP_OK && reply.result != ESP_ERR_NOT_FINISHED
               ? reply.result
               : ESP_ERR_INVALID_RESPONSE;
  for (size_t offset = 0; offset < count;) {
    uint8_t chunk[SPI_IR_CHUNK_SYMBOLS * SPI_IR_SYMBOL_BYTES];
    request = request_for(SPI_IR_UPLOAD);
    request.offset = offset;
    request.count = count - offset > SPI_IR_CHUNK_SYMBOLS ? SPI_IR_CHUNK_SYMBOLS : count - offset;
    for (uint16_t i = 0; i < request.count; i++) {
      const rmt_symbol_word_t *symbol = &symbols[offset + i];
      uint16_t first = symbol->duration0 | (symbol->level0 << SPI_IR_LEVEL_SHIFT);
      uint16_t second = symbol->duration1 | (symbol->level1 << SPI_IR_LEVEL_SHIFT);
      uint8_t *bytes = chunk + i * SPI_IR_SYMBOL_BYTES;
      bytes[0] = first & IR_BYTE_MASK;
      bytes[1] = first >> IR_BYTE_BITS;
      bytes[2] = second & IR_BYTE_MASK;
      bytes[3] = second >> IR_BYTE_BITS;
    }
    ret = exchange(&request, chunk, &reply, NULL);
    if (ret != ESP_OK)
      return ret;
    if (reply.epoch != s_epoch || reply.operation != s_operation ||
        reply.state != SPI_IR_UPLOADING || reply.offset != offset + request.count)
      return ESP_ERR_INVALID_RESPONSE;
    offset += request.count;
  }
  // Never repeat COMMIT after an ambiguous response; reconcile only by STATUS.
  ret = command(SPI_IR_COMMIT, &reply);
  if (ret == ESP_ERR_NOT_SUPPORTED)
    return ret;
  return wait_done(IR_CONTROL_MS, &reply);
}

esp_err_t ir_close(void) {
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  return leave(close_remote());
}

esp_err_t ir_rx_init(void) {
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  return leave(open_remote(IR_MODE_RX));
}

esp_err_t ir_tx_init(void) {
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  return leave(open_remote(IR_MODE_TX));
}

void ir_rx_prime(void) {
  if (!enter())
    return;
  // Each receive starts a new one-shot capture; cached raw remains readable.
  (void)leave(ESP_OK);
}

void ir_rx_deinit(void) {
  if (!enter())
    return;
  if (s_mode == IR_MODE_RX)
    (void)close_remote();
  (void)leave(ESP_OK);
}

void ir_tx_deinit(void) {
  if (!enter())
    return;
  if (s_mode == IR_MODE_TX)
    (void)close_remote();
  (void)leave(ESP_OK);
}

esp_err_t ir_receive(ir_data_t *out_data, uint32_t timeout_ms) {
  if (out_data == NULL || timeout_ms == 0 || timeout_ms > SPI_IR_RX_TIMEOUT_MS)
    return ESP_ERR_INVALID_ARG;
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  if (s_mode != IR_MODE_RX)
    return leave(ESP_ERR_INVALID_STATE);
  s_last_raw_count = 0;
  esp_err_t ret = next_operation();
  if (ret != ESP_OK)
    return leave(ret);
  spi_ir_request_t request = request_for(SPI_IR_CAPTURE);
  request.value = timeout_ms;
  spi_ir_reply_t reply;
  ret = exchange(&request, NULL, &reply, NULL);
  if (ret == ESP_OK)
    ret = wait_done(timeout_ms + IR_CONTROL_MS, &reply);
  if (ret != ESP_OK)
    return leave(ret);
  if (reply.count == 0 || reply.count > IR_MAX_SYMBOLS)
    return leave(ESP_ERR_INVALID_SIZE);
  size_t count = reply.count;
  for (size_t offset = 0; offset < count;) {
    uint8_t chunk[SPI_IR_CHUNK_SYMBOLS * SPI_IR_SYMBOL_BYTES];
    request = request_for(SPI_IR_FETCH);
    request.offset = offset;
    request.count = count - offset > SPI_IR_CHUNK_SYMBOLS ? SPI_IR_CHUNK_SYMBOLS : count - offset;
    ret = exchange(&request, NULL, &reply, chunk);
    if (ret != ESP_OK)
      return leave(ret);
    if (reply.epoch != s_epoch || reply.operation != s_operation || reply.state != SPI_IR_DONE ||
        reply.result != ESP_OK || reply.count != count || reply.offset != offset)
      return leave(ESP_ERR_INVALID_RESPONSE);
    for (uint16_t i = 0; i < request.count; i++) {
      const uint8_t *bytes = chunk + i * SPI_IR_SYMBOL_BYTES;
      uint16_t first = bytes[0] | ((uint16_t)bytes[1] << IR_BYTE_BITS);
      uint16_t second = bytes[2] | ((uint16_t)bytes[3] << IR_BYTE_BITS);
      s_last_raw[offset + i] = (rmt_symbol_word_t){
          .duration0 = first & SPI_IR_DURATION_MASK,
          .level0 = first >> SPI_IR_LEVEL_SHIFT,
          .duration1 = second & SPI_IR_DURATION_MASK,
          .level1 = second >> SPI_IR_LEVEL_SHIFT,
      };
    }
    offset += request.count;
  }
  s_last_raw_count = count;
  bool decoded = ir_decode(s_last_raw, count, out_data);
  decoded ? led_signal_info() : led_signal_warning();
  return leave(decoded ? ESP_OK : ESP_ERR_NOT_FOUND);
}

esp_err_t ir_send(const ir_data_t *data) {
  if (data == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  rmt_symbol_word_t symbols[IR_RMT_MEM_SYMBOLS];
  size_t count = ir_encode(data, symbols, IR_RMT_MEM_SYMBOLS);
  esp_err_t ret = transmit(symbols, count, ir_carrier_freq(data->protocol));
  ret == ESP_OK ? led_signal_info() : led_signal_error();
  return leave(ret);
}

esp_err_t ir_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t carrier_hz) {
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  return leave(transmit(symbols, count, carrier_hz));
}

esp_err_t ir_get_last_raw(rmt_symbol_word_t *out_buf, size_t buf_max, size_t *out_count) {
  if (out_buf == NULL || buf_max == 0)
    return ESP_ERR_INVALID_ARG;
  if (out_count != NULL)
    *out_count = 0;
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  if (buf_max < s_last_raw_count)
    return leave(ESP_ERR_INVALID_SIZE);
  memcpy(out_buf, s_last_raw, s_last_raw_count * sizeof(*out_buf));
  if (out_count != NULL)
    *out_count = s_last_raw_count;
  return leave(ESP_OK);
}

esp_err_t ir_flash_on(void) {
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  esp_err_t ret = open_remote(IR_MODE_TORCH);
  if (ret != ESP_OK)
    return leave(ret);
  ret = next_operation();
  if (ret != ESP_OK)
    return leave(ret);
  spi_ir_reply_t reply;
  ret = command(SPI_IR_TORCH, &reply);
  if (ret == ESP_OK) {
    int64_t deadline = esp_timer_get_time() + IR_CONTROL_MS * IR_US_PER_MS;
    do {
      ret = command(SPI_IR_STATUS, &reply);
      if (ret == ESP_OK && reply.epoch == s_epoch && reply.operation == s_operation &&
          reply.state == SPI_IR_TORCH_RUNNING)
        return leave(ESP_OK);
      if (ret == ESP_OK && reply.state == SPI_IR_CLOSED)
        return leave(ESP_ERR_INVALID_STATE);
      vTaskDelay(pdMS_TO_TICKS(IR_POLL_MS));
    } while (esp_timer_get_time() < deadline);
    ret = ESP_ERR_TIMEOUT;
  }
  return leave(ret);
}

esp_err_t ir_flash_off(void) {
  if (!enter())
    return ESP_ERR_INVALID_STATE;
  return leave(s_mode == IR_MODE_TORCH ? close_remote() : ESP_OK);
}

void ir_print_raw(const rmt_symbol_word_t *symbols, size_t count) {
  if (symbols == NULL)
    return;
  size_t limit = count < IR_PRINT_MAX_SYMBOLS ? count : IR_PRINT_MAX_SYMBOLS;
  for (size_t i = 0; i < limit; i++)
    ESP_LOGD(TAG, "[%u] %u %u", (unsigned)i, symbols[i].duration0, symbols[i].duration1);
}

void ir_print_data(const ir_data_t *data) {
  if (data == NULL)
    return;
  ESP_LOGI(TAG,
           "%s addr=0x%08lX cmd=0x%08lX%s",
           ir_protocol_name(data->protocol),
           (unsigned long)data->address,
           (unsigned long)data->command,
           data->repeat ? " repeat" : "");
}
