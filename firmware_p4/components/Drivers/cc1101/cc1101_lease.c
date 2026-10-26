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

#include "cc1101_lease.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "resource_mgr.h"

static const char *TAG = "CC1101_LEASE";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_owner;
static uint32_t s_operations;
static res_handle_t s_grant;
static esp_err_t s_error;
static bool s_is_access_enabled;

esp_err_t cc1101_lease_acquire(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  bool is_owner = caller != NULL && s_owner == caller;
  if (is_owner)
    s_is_access_enabled = true;
  bool can_acquire = caller != NULL && s_owner == NULL && s_operations == 0;
  portEXIT_CRITICAL(&s_lock);
  if (is_owner)
    return ESP_OK;
  if (!can_acquire)
    return ESP_ERR_INVALID_STATE;
  res_request_t request = {.id = RES_SUBGHZ,
                           .lane = RES_LANE_MAIN,
                           .owner_kind = RES_OWNER_SYSTEM,
                           .allow_preempt = false};
  res_handle_t grant = RES_HANDLE_NONE;
  esp_err_t error = resource_acquire(&request, &grant);
  if (error != ESP_OK)
    return error;
  portENTER_CRITICAL(&s_lock);
  can_acquire = s_owner == NULL && s_operations == 0;
  if (can_acquire) {
    s_owner = caller;
    s_grant = grant;
    s_error = ESP_OK;
    s_is_access_enabled = true;
  }
  portEXIT_CRITICAL(&s_lock);
  if (!can_acquire)
    resource_release(grant);
  return can_acquire ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cc1101_lease_release(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  bool can_release = caller != NULL && s_owner == caller && s_operations == 0;
  res_handle_t grant = s_grant;
  if (can_release) {
    s_owner = NULL;
    s_grant = RES_HANDLE_NONE;
    s_is_access_enabled = false;
  }
  portEXIT_CRITICAL(&s_lock);
  if (!can_release) {
    ESP_LOGW(TAG, "CC1101 lease still in use");
    return ESP_ERR_INVALID_STATE;
  }
  return resource_release(grant);
}

void cc1101_lease_pause(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  if (caller != NULL && s_owner == caller)
    s_is_access_enabled = false;
  portEXIT_CRITICAL(&s_lock);
}

bool cc1101_lease_enter(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  bool can_enter = s_owner == NULL || (s_owner == caller && s_is_access_enabled);
  if (can_enter)
    ++s_operations;
  portEXIT_CRITICAL(&s_lock);
  return can_enter;
}

void cc1101_lease_leave(void) {
  portENTER_CRITICAL(&s_lock);
  if (s_operations != 0)
    --s_operations;
  portEXIT_CRITICAL(&s_lock);
}

void cc1101_lease_record_error(esp_err_t error) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  if (caller != NULL && s_owner == caller && s_is_access_enabled && s_error == ESP_OK)
    s_error = error;
  portEXIT_CRITICAL(&s_lock);
}

esp_err_t cc1101_lease_take_error(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  esp_err_t error = caller != NULL && s_owner == caller ? s_error : ESP_ERR_INVALID_STATE;
  if (caller != NULL && s_owner == caller)
    s_error = ESP_OK;
  portEXIT_CRITICAL(&s_lock);
  return error;
}
