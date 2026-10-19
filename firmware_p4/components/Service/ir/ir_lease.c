// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ir_lease.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "IR_LEASE";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_owner;
static uint32_t s_operations;
static bool s_is_active;

esp_err_t ir_lease_acquire(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  bool can_acquire = s_owner == caller || (s_owner == NULL && s_operations == 0 && !s_is_active);
  if (can_acquire)
    s_owner = caller;
  portEXIT_CRITICAL(&s_lock);
  return can_acquire ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t ir_lease_release(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  bool can_release = s_owner == caller && s_operations == 0 && !s_is_active;
  if (can_release)
    s_owner = NULL;
  portEXIT_CRITICAL(&s_lock);
  if (!can_release)
    ESP_LOGW(TAG, "IR lease retained until owned hardware is released");
  return can_release ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool ir_lease_enter(void) {
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  bool can_enter = s_owner == NULL || s_owner == caller;
  if (can_enter)
    ++s_operations;
  portEXIT_CRITICAL(&s_lock);
  return can_enter;
}

void ir_lease_leave(bool is_active) {
  portENTER_CRITICAL(&s_lock);
  s_is_active = is_active;
  if (s_operations != 0)
    --s_operations;
  portEXIT_CRITICAL(&s_lock);
}
