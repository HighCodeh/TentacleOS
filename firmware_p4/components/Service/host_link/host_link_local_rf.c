// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_link_local_rf.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host_link.h"
#include "spi_protocol.h"

#define LOCAL_RF_PAYLOAD_MAX 2

static const char *TAG = "LOCAL_RF";
static SemaphoreHandle_t s_call_lock;
static SemaphoreHandle_t s_done;
static host_rf_request_t s_request;
static uint8_t s_payload[LOCAL_RF_PAYLOAD_MAX];
static uint8_t s_output[HOST_RF_RESPONSE_MAX];
static uint8_t s_status;

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
  s_request.payload = s_payload;
  s_request.out = s_output;
  s_request.capacity = sizeof(s_output);
  error = host_link_schedule_local_rf();
  if (error != ESP_OK)
    return error;
  // The kill path takes s_call_lock before deleting a native app task.
  xSemaphoreTake(s_done, portMAX_DELAY);
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

void host_local_rf_process(void) {
  if (s_request.command == SPI_ID_NFC_READ || s_request.command == SPI_ID_RFID_READ)
    host_rf_stop();
  s_status = host_rf_handle(&s_request);
  xSemaphoreGive(s_done);
}
