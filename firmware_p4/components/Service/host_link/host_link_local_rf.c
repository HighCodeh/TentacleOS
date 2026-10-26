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

#include "host_link_local_rf.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host_link.h"
#include "host_link_local_ir.h"
#include "spi_protocol.h"

#define LOCAL_RF_PAYLOAD_MAX        HOST_LOCAL_IR_CODE_SIZE
#define LOCAL_RF_LEGACY_PAYLOAD_MAX 2

static const char *TAG = "LOCAL_RF";
static SemaphoreHandle_t s_call_lock;
static SemaphoreHandle_t s_done;
static host_rf_request_t s_request;
static uint8_t s_payload[LOCAL_RF_PAYLOAD_MAX];
static uint8_t s_output[HOST_RF_RESPONSE_MAX];
static uint8_t s_status;
static esp_err_t s_local_error;
static host_local_rf_cb_t s_callback;
static void *s_callback_context;

esp_err_t host_local_rf_init(void) {
  if (s_call_lock == NULL)
    s_call_lock = xSemaphoreCreateMutex();
  if (s_done == NULL)
    s_done = xSemaphoreCreateBinary();
  if (s_call_lock == NULL || s_done == NULL) {
    ESP_LOGE(TAG, "Cannot allocate local RF synchronization");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool host_local_rf_is_ready(void) {
  return s_call_lock != NULL && s_done != NULL;
}

bool host_local_rf_lock(uint32_t timeout_ms) {
  return s_call_lock == NULL || xSemaphoreTake(s_call_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void host_local_rf_unlock(void) {
  if (s_call_lock != NULL)
    xSemaphoreGive(s_call_lock);
}

esp_err_t host_local_rf_execute(host_rf_request_t *request) {
  if (request == NULL || request->length > LOCAL_RF_PAYLOAD_MAX || request->out == NULL ||
      request->capacity < HOST_RF_RESPONSE_MAX ||
      (!host_local_ir_is_command(request->command) &&
       request->length > LOCAL_RF_LEGACY_PAYLOAD_MAX) ||
      (request->length != 0 && request->payload == NULL)) {
    return ESP_ERR_INVALID_ARG;
  }
  request->out_length = 0;
  if (s_call_lock == NULL || s_done == NULL)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = host_link_start_worker();
  if (error != ESP_OK)
    return error;
  if (request->length != 0)
    memcpy(s_payload, request->payload, request->length);
  s_request = *request;
  s_callback = NULL;
  s_request.payload = s_payload;
  s_request.out = s_output;
  s_request.capacity = sizeof(s_output);
  error = host_link_schedule_local_rf();
  if (error != ESP_OK)
    return error;
  // The kill path takes s_call_lock before deleting a native app task.
  xSemaphoreTake(s_done, portMAX_DELAY);
  if (s_local_error != ESP_OK)
    return s_local_error;
  if (s_status == SPI_STATUS_BUSY)
    return ESP_ERR_INVALID_STATE;
  if (s_status != SPI_STATUS_OK)
    return ESP_FAIL;
  if (s_request.out_length > sizeof(s_output))
    return ESP_ERR_INVALID_SIZE;
  request->out_length = s_request.out_length;
  memcpy(request->out, s_output, s_request.out_length);
  return ESP_OK;
}

esp_err_t host_local_rf_call(host_local_rf_cb_t callback, void *context) {
  if (callback == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!host_local_rf_is_ready())
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = host_link_start_worker();
  if (error != ESP_OK)
    return error;
  s_callback = callback;
  s_callback_context = context;
  error = host_link_schedule_local_rf();
  if (error != ESP_OK) {
    s_callback = NULL;
    s_callback_context = NULL;
    return error;
  }
  xSemaphoreTake(s_done, portMAX_DELAY);
  return s_local_error;
}

void host_local_rf_process(void) {
  if (s_callback != NULL) {
    s_local_error = s_callback(s_callback_context);
    s_callback = NULL;
    s_callback_context = NULL;
    xSemaphoreGive(s_done);
    return;
  }
  s_local_error = ESP_OK;
  if (host_local_ir_is_command(s_request.command)) {
    s_local_error = host_local_ir_execute(&s_request);
    s_status = SPI_STATUS_OK;
    xSemaphoreGive(s_done);
    return;
  }
  if (s_request.command == SPI_ID_NFC_READ || s_request.command == SPI_ID_RFID_READ)
    host_rf_stop();
  s_status = host_rf_handle(&s_request);
  xSemaphoreGive(s_done);
}
