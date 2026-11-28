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

#include "tos_api_header.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "header_spi.h"
#include "host_link_local_rf.h"
#include "led_control.h"
#include "tos_api.h"
#include "tos_app_ctx.h"

#define HEADER_GATE_MS          1000U
#define HEADER_CLEANUP_RETRY_MS 10U
static const char *TAG = "TOS_HEADER";
typedef enum {
  HEADER_OPEN,
  HEADER_TRANSFER,
  HEADER_WRITE,
  HEADER_CLOSE,
  HEADER_OP_COUNT
} tos_header_op_t;
typedef struct {
  tos_header_op_t operation;
  header_spi_config_t config;
  uint8_t tx[TOS_HEADER_TRANSFER_MAX];
  uint8_t rx[TOS_HEADER_TRANSFER_MAX];
  size_t length;
  uint8_t pin;
  bool level;
} tos_header_request_t;
static void *s_owner;
static uint32_t s_session;
static uint32_t s_sequence;

static esp_err_t execute(void *context) {
  tos_header_request_t *request = context;
  switch (request->operation) {
    case HEADER_OPEN:
      return header_spi_open(&request->config);
    case HEADER_TRANSFER:
      return header_spi_transfer(request->tx, request->rx, request->length);
    case HEADER_WRITE:
      return header_spi_write(request->pin, request->level);
    case HEADER_CLOSE:
      return header_spi_close();
    default:
      return ESP_ERR_INVALID_ARG;
  }
}

static esp_err_t acquire(uint32_t session, bool is_closing) {
  if (!tos_app_ctx_current() || !tos_app_cap_check(TOS_CAP_HEADER))
    return ESP_ERR_NOT_SUPPORTED;
  if (!host_local_rf_is_ready() || (!is_closing && tos_api_get()->should_stop()))
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(HEADER_GATE_MS))
    return ESP_ERR_TIMEOUT;
  if ((!is_closing && tos_api_get()->should_stop()) ||
      (session && (s_owner != xTaskGetCurrentTaskHandle() || session != s_session))) {
    host_local_rf_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

static esp_err_t open_session(const tos_header_spi_config_t *config, uint32_t *out_session) {
  if (!config || !out_session)
    return ESP_ERR_INVALID_ARG;
  *out_session = 0;
  esp_err_t error = acquire(0, false);
  if (error != ESP_OK)
    return error;
  if (s_owner || s_sequence == UINT32_MAX) {
    error = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  tos_header_request_t request = {
      .operation = HEADER_OPEN,
      .config = {.clock_hz = config->clock_hz,
                 .cs_pin = config->cs_pin,
                 .control_pin = config->control_pin,
                 .mode = config->mode},
  };
  error = host_local_rf_call(execute, &request);
  if (error == ESP_OK) {
    s_owner = xTaskGetCurrentTaskHandle();
    s_session = ++s_sequence;
    *out_session = s_session;
    ESP_LOGI(TAG, "App owns external header session %lu", (unsigned long)s_session);
    led_signal_info();
  } else {
    ESP_LOGE(TAG, "Cannot open external header: %s", esp_err_to_name(error));
    led_signal_error();
  }
cleanup:
  host_local_rf_unlock();
  return error;
}

static esp_err_t transfer(uint32_t session, const uint8_t *tx, uint8_t *out_rx, size_t length) {
  if (!session || !tx || !out_rx || !length || length > TOS_HEADER_TRANSFER_MAX)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = acquire(session, false);
  if (error != ESP_OK)
    return error;
  tos_header_request_t request = {.operation = HEADER_TRANSFER, .length = length};
  memcpy(request.tx, tx, length);
  error = host_local_rf_call(execute, &request);
  if (error == ESP_OK)
    memcpy(out_rx, request.rx, length);
  host_local_rf_unlock();
  return error;
}

static esp_err_t write_pin(uint32_t session, uint8_t pin, bool level) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = acquire(session, false);
  if (error != ESP_OK)
    return error;
  tos_header_request_t request = {.operation = HEADER_WRITE, .pin = pin, .level = level};
  error = host_local_rf_call(execute, &request);
  host_local_rf_unlock();
  return error;
}

static esp_err_t close_session(uint32_t session) {
  if (!session)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = acquire(session, true);
  if (error != ESP_OK)
    return error;
  tos_header_request_t request = {.operation = HEADER_CLOSE};
  error = host_local_rf_call(execute, &request);
  if (error == ESP_OK) {
    s_owner = NULL;
    s_session = 0;
  }
  host_local_rf_unlock();
  return error;
}

void tos_api_header_cancel_owner(void *owner) {
  if (!owner || !host_local_rf_is_ready())
    return;
  bool has_warned = false;
  for (;;) {
    if (host_local_rf_lock(HEADER_GATE_MS)) {
      if (s_owner != owner) {
        host_local_rf_unlock();
        return;
      }
      tos_header_request_t request = {.operation = HEADER_CLOSE};
      esp_err_t error = host_local_rf_call(execute, &request);
      if (error == ESP_OK) {
        s_owner = NULL;
        s_session = 0;
        host_local_rf_unlock();
        return;
      }
      host_local_rf_unlock();
    }
    if (!has_warned) {
      ESP_LOGW(TAG, "Waiting for external header cleanup");
      led_signal_warning();
      has_warned = true;
    }
    vTaskDelay(pdMS_TO_TICKS(HEADER_CLEANUP_RETRY_MS));
  }
}

const tos_header_api_t tos_header_api_impl = {.spi_open = open_session,
                                              .spi_transfer = transfer,
                                              .gpio_write = write_pin,
                                              .close = close_session};
