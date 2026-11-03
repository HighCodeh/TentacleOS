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

#include "ir_remote.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#include "pin_def.h"
#include "sys_prio.h"

#define IR_WORKER_STACK   4096
#define IR_POLL_MS        10
#define IR_US_PER_MS      1000
#define IR_RESOLUTION_HZ  1000000
#define IR_GLITCH_NS      1250
#define IR_IDLE_NS        12000000
#define IR_CARRIER_DUTY   0.33f
#define IR_BYTE_BITS      8
#define IR_BYTE_MASK      0xff
#define IR_TX_DEADLINE_MS 1200

_Static_assert(sizeof(spi_ir_request_t) == SPI_IR_HEADER_SIZE, "IR request size");
_Static_assert(sizeof(spi_ir_reply_t) == SPI_IR_HEADER_SIZE, "IR reply size");
_Static_assert(SPI_IR_HEADER_SIZE + SPI_IR_CHUNK_SYMBOLS * SPI_IR_SYMBOL_BYTES < SPI_MAX_PAYLOAD,
               "IR response fits status and payload");

static const char *TAG = "IR_REMOTE";
static SemaphoreHandle_t s_mutex;
static atomic_bool s_disconnected;
static uint64_t s_client;
static uint64_t s_epoch;
static uint32_t s_operation;
static spi_ir_state_t s_state = SPI_IR_CLOSED;
static esp_err_t s_result = ESP_OK;
static esp_err_t s_stop_result;
static bool s_close;
static bool s_is_tx;
static uint8_t s_kind;
static uint16_t s_requested_count;
static bool s_enabled;
static uint32_t s_value;
static uint16_t s_count;
static uint16_t s_uploaded;
static int64_t s_lease_deadline;
static int64_t s_operation_deadline;
static rmt_channel_handle_t s_channel;
static rmt_encoder_handle_t s_encoder;
// Owned by this service until checked RMT teardown; never references SPI storage.
static rmt_symbol_word_t *s_symbols;
static atomic_uint s_received;

static uint64_t new_epoch(void) {
  uint64_t epoch;
  do {
    esp_fill_random(&epoch, sizeof(epoch));
  } while (epoch == 0 || epoch == s_epoch);
  return epoch;
}

static uint16_t read_word(const uint8_t *data) {
  return data[0] | ((uint16_t)data[1] << IR_BYTE_BITS);
}

static void encode_symbol(uint8_t *out, const rmt_symbol_word_t *symbol) {
  uint16_t first = symbol->duration0 | (symbol->level0 << SPI_IR_LEVEL_SHIFT);
  uint16_t second = symbol->duration1 | (symbol->level1 << SPI_IR_LEVEL_SHIFT);
  out[0] = first & IR_BYTE_MASK;
  out[1] = first >> IR_BYTE_BITS;
  out[2] = second & IR_BYTE_MASK;
  out[3] = second >> IR_BYTE_BITS;
}

static bool
rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *context) {
  (void)channel;
  (void)context;
  // A full buffer is conservatively overflow: the driver may truncate silently.
  atomic_store(&s_received, event->num_symbols + 1);
  return false;
}

static esp_err_t release_hardware(void) {
  esp_err_t ret;
  if (s_channel != NULL) {
    if (s_enabled) {
      ret = rmt_disable(s_channel);
      if (ret != ESP_OK)
        return ret;
      s_enabled = false;
    }
    ret = rmt_del_channel(s_channel);
    if (ret != ESP_OK)
      return ret;
    s_channel = NULL;
  }
  if (s_encoder != NULL) {
    ret = rmt_del_encoder(s_encoder);
    if (ret != ESP_OK)
      return ret;
    s_encoder = NULL;
  }
  ret = gpio_set_level(GPIO_IR_TX_PIN, 0);
  if (ret != ESP_OK)
    return ret;
  return gpio_set_direction(GPIO_IR_TX_PIN, GPIO_MODE_OUTPUT);
}

static void stop(esp_err_t result, bool close) {
  s_state = SPI_IR_STOPPING;
  s_stop_result = result;
  s_result = ESP_ERR_NOT_FINISHED;
  s_close = s_close || close;
}

static esp_err_t start_tx(void) {
  const rmt_tx_channel_config_t config = {
      .gpio_num = GPIO_IR_TX_PIN,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = IR_RESOLUTION_HZ,
      .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
      .trans_queue_depth = 1,
  };
  esp_err_t ret = rmt_new_tx_channel(&config, &s_channel);
  if (ret != ESP_OK)
    return ret;
  const rmt_copy_encoder_config_t encoder = {};
  ret = rmt_new_copy_encoder(&encoder, &s_encoder);
  if (ret != ESP_OK)
    return ret;
  const rmt_carrier_config_t carrier = {.frequency_hz = s_value, .duty_cycle = IR_CARRIER_DUTY};
  ret = rmt_apply_carrier(s_channel, s_value != 0 ? &carrier : NULL);
  if (ret != ESP_OK)
    return ret;
  ret = rmt_enable(s_channel);
  if (ret != ESP_OK)
    return ret;
  s_enabled = true;
  const rmt_transmit_config_t tx = {.loop_count = 0, .flags.eot_level = 0};
  ret = rmt_transmit(s_channel, s_encoder, s_symbols, s_count * sizeof(*s_symbols), &tx);
  if (ret == ESP_OK) {
    s_state = SPI_IR_TX_RUNNING;
    s_operation_deadline = esp_timer_get_time() + IR_TX_DEADLINE_MS * IR_US_PER_MS;
  }
  return ret;
}

static esp_err_t start_rx(void) {
  s_symbols = calloc(SPI_IR_RX_SYMBOLS + 1, sizeof(*s_symbols));
  if (s_symbols == NULL) {
    ESP_LOGE(TAG, "RX allocation failed");
    return ESP_ERR_NO_MEM;
  }
  const rmt_rx_channel_config_t config = {
      .gpio_num = GPIO_IR_RX_PIN,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = IR_RESOLUTION_HZ,
      .mem_block_symbols = SOC_RMT_MEM_WORDS_PER_CHANNEL,
      .flags.invert_in = true, // TSOP output is active low; wire level 1 is an optical mark.
  };
  esp_err_t ret = rmt_new_rx_channel(&config, &s_channel);
  if (ret != ESP_OK)
    return ret;
  const rmt_rx_event_callbacks_t callbacks = {.on_recv_done = rx_done};
  ret = rmt_rx_register_event_callbacks(s_channel, &callbacks, NULL);
  if (ret != ESP_OK)
    return ret;
  atomic_store(&s_received, 0);
  ret = rmt_enable(s_channel);
  if (ret != ESP_OK)
    return ret;
  s_enabled = true;
  const rmt_receive_config_t rx = {.signal_range_min_ns = IR_GLITCH_NS,
                                   .signal_range_max_ns = IR_IDLE_NS};
  ret = rmt_receive(s_channel, s_symbols, (SPI_IR_RX_SYMBOLS + 1) * sizeof(*s_symbols), &rx);
  if (ret == ESP_OK) {
    s_state = SPI_IR_RX_RUNNING;
    s_operation_deadline = esp_timer_get_time() + (int64_t)s_value * IR_US_PER_MS;
  }
  return ret;
}

static void poll_once(void) {
  if (xSemaphoreTake(s_mutex, 0) != pdTRUE)
    return;
  int64_t now = esp_timer_get_time();
  if (atomic_exchange(&s_disconnected, false)) {
    s_client = 0;
    s_epoch = new_epoch();
    stop(ESP_ERR_INVALID_STATE, true);
  }
  if (s_state != SPI_IR_CLOSED && s_state != SPI_IR_STOPPING && now >= s_lease_deadline)
    stop(ESP_ERR_TIMEOUT, true);
  esp_err_t ret = ESP_OK;
  switch (s_state) {
    case SPI_IR_TX_PENDING:
      ret = start_tx();
      break;
    case SPI_IR_RX_PENDING:
      ret = start_rx();
      break;
    case SPI_IR_TORCH_PENDING:
      ret = gpio_set_level(GPIO_IR_TX_PIN, 1);
      if (ret == ESP_OK) {
        s_state = SPI_IR_TORCH_RUNNING;
        s_operation_deadline = now + SPI_IR_TORCH_MS * IR_US_PER_MS;
      }
      break;
    case SPI_IR_TX_RUNNING:
      ret = rmt_tx_wait_all_done(s_channel, 0);
      if (ret == ESP_OK)
        stop(ESP_OK, false);
      else if (ret == ESP_ERR_TIMEOUT && now < s_operation_deadline)
        ret = ESP_OK;
      break;
    case SPI_IR_RX_RUNNING: {
      unsigned received = atomic_load(&s_received);
      if (received != 0) {
        s_count = received - 1;
        stop(s_count > SPI_IR_RX_SYMBOLS ? ESP_ERR_INVALID_SIZE : ESP_OK, false);
      } else if (now >= s_operation_deadline)
        stop(ESP_ERR_TIMEOUT, false);
      break;
    }
    case SPI_IR_TORCH_RUNNING:
      if (now >= s_operation_deadline)
        stop(ESP_ERR_TIMEOUT, true);
      break;
    default:
      break;
  }
  if (ret != ESP_OK)
    stop(ret, false);
  if (s_state == SPI_IR_STOPPING) {
    ret = release_hardware();
    if (ret == ESP_OK) {
      s_result = s_stop_result;
      s_state = s_close ? SPI_IR_CLOSED : SPI_IR_DONE;
      if (s_is_tx || s_close || s_result != ESP_OK) {
        free(s_symbols);
        s_symbols = NULL;
      }
      s_close = false;
    }
  }
  xSemaphoreGive(s_mutex);
}

static void worker(void *arg) {
  (void)arg;
  for (;;) {
    poll_once();
    vTaskDelay(pdMS_TO_TICKS(IR_POLL_MS));
  }
}

esp_err_t ir_remote_init(void) {
  if (s_mutex != NULL)
    return ESP_OK;
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Mutex allocation failed");
    return ESP_ERR_NO_MEM;
  }
  s_epoch = new_epoch();
  if (xTaskCreatePinnedToCore(
          worker, "ir_remote", IR_WORKER_STACK, NULL, SYS_PRIO_SERVICE_HI, NULL, SYS_CORE_MAIN) !=
      pdPASS) {
    ESP_LOGE(TAG, "Worker allocation failed");
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void ir_remote_disconnect(void) {
  atomic_store(&s_disconnected, true);
}

static bool valid_frame(void) {
  uint32_t duration = 0;
  for (uint16_t i = 0; i < s_count; i++) {
    if (s_symbols[i].duration0 == 0 || (s_symbols[i].duration1 == 0 && i + 1 != s_count))
      return false;
    duration += s_symbols[i].duration0 + s_symbols[i].duration1;
    if (duration > SPI_IR_MAX_US)
      return false;
  }
  return true;
}

static spi_status_t handle(const spi_ir_request_t *request, const uint8_t *chunk, uint8_t length) {
  if (request->action == SPI_IR_OPEN) {
    if (request->epoch == 0 || request->operation != 0 || request->count != 0 ||
        request->offset != 0 || request->value != 0)
      return SPI_STATUS_INVALID_ARG;
    if (request->epoch == s_client)
      return SPI_STATUS_OK;
    if (s_state != SPI_IR_CLOSED) {
      stop(ESP_ERR_INVALID_STATE, true);
      return SPI_STATUS_BUSY;
    }
    s_client = request->epoch;
    s_epoch = new_epoch();
    s_operation = 0;
    s_state = SPI_IR_IDLE;
    s_result = ESP_OK;
    s_count = s_uploaded = 0;
    return SPI_STATUS_OK;
  }
  if ((request->action == SPI_IR_STATUS || request->action == SPI_IR_CLOSE ||
       request->action == SPI_IR_COMMIT || request->action == SPI_IR_CANCEL) &&
      (request->count != 0 || request->offset != 0 || request->value != 0))
    return SPI_STATUS_INVALID_ARG;
  if ((request->action == SPI_IR_UPLOAD || request->action == SPI_IR_FETCH) && request->value != 0)
    return SPI_STATUS_INVALID_ARG;
  if (request->action == SPI_IR_STATUS)
    return SPI_STATUS_OK;
  if (request->epoch != s_epoch || s_client == 0)
    return SPI_STATUS_INVALID_ARG;
  if (request->action == SPI_IR_CLOSE) {
    if (s_state != SPI_IR_CLOSED)
      stop(ESP_ERR_INVALID_STATE, true);
    return SPI_STATUS_OK;
  }
  if (s_state == SPI_IR_CLOSED || s_state == SPI_IR_STOPPING)
    return SPI_STATUS_BUSY;
  if (request->action == SPI_IR_BEGIN || request->action == SPI_IR_CAPTURE ||
      request->action == SPI_IR_TORCH) {
    if (request->operation == 0)
      return SPI_STATUS_INVALID_ARG;
    if (request->operation == s_operation) {
      return request->action == s_kind && request->value == s_value &&
                     request->count == s_requested_count
                 ? SPI_STATUS_OK
                 : SPI_STATUS_INVALID_ARG;
    }
    if (request->operation < s_operation || request->offset != 0)
      return SPI_STATUS_INVALID_ARG;
    if (s_state != SPI_IR_IDLE && s_state != SPI_IR_DONE)
      return SPI_STATUS_BUSY;
    if (request->action == SPI_IR_BEGIN &&
        (request->count == 0 || request->count > SPI_IR_TX_SYMBOLS ||
         (request->value != 0 &&
          (request->value < SPI_IR_CARRIER_MIN || request->value > SPI_IR_CARRIER_MAX))))
      return SPI_STATUS_INVALID_ARG;
    if (request->action != SPI_IR_BEGIN && request->count != 0)
      return SPI_STATUS_INVALID_ARG;
    if (request->action == SPI_IR_TORCH && request->value != 0)
      return SPI_STATUS_INVALID_ARG;
    if (request->action == SPI_IR_CAPTURE &&
        (request->value == 0 || request->value > SPI_IR_RX_TIMEOUT_MS))
      return SPI_STATUS_INVALID_ARG;
    free(s_symbols);
    s_symbols = NULL;
    s_operation = request->operation;
    s_value = request->value;
    s_kind = request->action;
    s_requested_count = request->count;
    s_count = request->count;
    s_uploaded = 0;
    s_is_tx = request->action == SPI_IR_BEGIN;
    s_result = ESP_ERR_NOT_FINISHED;
    if (s_is_tx) {
      s_symbols = calloc(s_count, sizeof(*s_symbols));
      if (s_symbols == NULL) {
        ESP_LOGE(TAG, "TX allocation failed");
        s_state = SPI_IR_DONE;
        s_result = ESP_ERR_NO_MEM;
        return SPI_STATUS_OK;
      }
      s_state = SPI_IR_UPLOADING;
    } else {
      s_count = 0;
      s_state = request->action == SPI_IR_CAPTURE ? SPI_IR_RX_PENDING : SPI_IR_TORCH_PENDING;
    }
    return SPI_STATUS_OK;
  }
  if (request->operation != s_operation || s_operation == 0)
    return SPI_STATUS_INVALID_ARG;
  switch (request->action) {
    case SPI_IR_UPLOAD:
      if (s_state != SPI_IR_UPLOADING || request->count == 0 ||
          request->count > SPI_IR_CHUNK_SYMBOLS || length != request->count * SPI_IR_SYMBOL_BYTES ||
          request->offset > s_uploaded || request->offset + request->count > s_count)
        return SPI_STATUS_INVALID_ARG;
      if (request->offset < s_uploaded) {
        if (request->offset + request->count > s_uploaded)
          return SPI_STATUS_INVALID_ARG;
        for (uint16_t i = 0; i < request->count; i++) {
          uint8_t encoded[SPI_IR_SYMBOL_BYTES];
          encode_symbol(encoded, &s_symbols[request->offset + i]);
          if (memcmp(encoded, chunk + i * SPI_IR_SYMBOL_BYTES, sizeof(encoded)) != 0)
            return SPI_STATUS_INVALID_ARG;
        }
        return SPI_STATUS_OK;
      }
      for (uint16_t i = 0; i < request->count; i++) {
        uint16_t first = read_word(chunk + i * SPI_IR_SYMBOL_BYTES);
        uint16_t second = read_word(chunk + i * SPI_IR_SYMBOL_BYTES + 2);
        s_symbols[s_uploaded + i] = (rmt_symbol_word_t){
            .duration0 = first & SPI_IR_DURATION_MASK,
            .level0 = first >> SPI_IR_LEVEL_SHIFT,
            .duration1 = second & SPI_IR_DURATION_MASK,
            .level1 = second >> SPI_IR_LEVEL_SHIFT,
        };
      }
      s_uploaded += request->count;
      return SPI_STATUS_OK;
    case SPI_IR_COMMIT:
      if (!s_is_tx)
        return SPI_STATUS_INVALID_ARG;
      if (s_state != SPI_IR_UPLOADING)
        return SPI_STATUS_OK; // tombstone: never restart TX
      if (s_uploaded != s_count || !valid_frame())
        return SPI_STATUS_INVALID_ARG;
      s_state = SPI_IR_TX_PENDING;
      return SPI_STATUS_OK;
    case SPI_IR_CANCEL:
      stop(ESP_ERR_INVALID_STATE, false);
      return SPI_STATUS_OK;
    case SPI_IR_FETCH:
      if (s_state != SPI_IR_DONE || s_result != ESP_OK || s_is_tx || s_symbols == NULL ||
          request->count == 0 || request->count > SPI_IR_CHUNK_SYMBOLS ||
          request->offset + request->count > s_count)
        return SPI_STATUS_INVALID_ARG;
      return SPI_STATUS_OK;
    default:
      return SPI_STATUS_INVALID_ARG;
  }
}

spi_status_t
ir_remote_dispatch(const uint8_t *data, uint8_t length, uint8_t *out_data, uint8_t *out_length) {
  if (out_length == NULL || out_data == NULL)
    return SPI_STATUS_INVALID_ARG;
  *out_length = 0;
  if (s_mutex == NULL)
    return SPI_STATUS_UNSUPPORTED;
  if (data == NULL || length < sizeof(spi_ir_request_t))
    return SPI_STATUS_INVALID_ARG;
  spi_ir_request_t request;
  memcpy(&request, data, sizeof(request));
  if (request.revision != SPI_IR_REVISION)
    return SPI_STATUS_UNSUPPORTED;
  if (request.reserved != 0 || request.action >= SPI_IR_ACTION_COUNT ||
      (request.action != SPI_IR_UPLOAD && length != sizeof(request)))
    return SPI_STATUS_INVALID_ARG;
  if (atomic_load(&s_disconnected) || xSemaphoreTake(s_mutex, 0) != pdTRUE)
    return SPI_STATUS_BUSY;
  spi_status_t status = handle(&request, data + sizeof(request), length - sizeof(request));
  if (status == SPI_STATUS_OK) {
    if (request.action == SPI_IR_OPEN || request.epoch == s_epoch)
      s_lease_deadline = esp_timer_get_time() + SPI_IR_LEASE_MS * IR_US_PER_MS;
    spi_ir_reply_t reply = {
        .epoch = s_epoch,
        .operation = s_operation,
        .result = s_result,
        .state = s_state,
        .revision = SPI_IR_REVISION,
        .count = s_count,
        .offset = s_uploaded,
    };
    if (request.action == SPI_IR_FETCH)
      reply.offset = request.offset;
    memcpy(out_data, &reply, sizeof(reply));
    *out_length = sizeof(reply);
    if (request.action == SPI_IR_FETCH) {
      for (uint16_t i = 0; i < request.count; i++)
        encode_symbol(out_data + sizeof(reply) + i * SPI_IR_SYMBOL_BYTES,
                      &s_symbols[request.offset + i]);
      *out_length += request.count * SPI_IR_SYMBOL_BYTES;
    }
  }
  xSemaphoreGive(s_mutex);
  return status;
}
