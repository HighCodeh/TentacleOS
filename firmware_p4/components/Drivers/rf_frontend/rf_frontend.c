// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rf_frontend.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RF_FRONTEND";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rf_frontend_mode_t s_mode = RF_FRONTEND_IDLE;
static rf_frontend_mode_t s_reserved_mode = RF_FRONTEND_IDLE;
static void *s_reservation_owner;

esp_err_t rf_frontend_claim(rf_frontend_mode_t mode) {
  if (mode <= RF_FRONTEND_IDLE || mode >= RF_FRONTEND_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t result = ESP_OK;
  void *task = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  if ((s_reservation_owner != NULL && (s_reservation_owner != task || s_reserved_mode != mode)) ||
      (s_mode != RF_FRONTEND_IDLE && s_mode != mode)) {
    result = ESP_ERR_INVALID_STATE;
  } else {
    s_mode = mode;
  }
  portEXIT_CRITICAL(&s_lock);
  if (result != ESP_OK) {
    ESP_LOGD(TAG, "Shared antenna is busy");
  }
  return result;
}

void rf_frontend_release(rf_frontend_mode_t mode) {
  void *task = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  if (s_mode == mode && (s_reservation_owner == NULL || s_reservation_owner == task)) {
    s_mode = RF_FRONTEND_IDLE;
  }
  portEXIT_CRITICAL(&s_lock);
}

esp_err_t rf_frontend_reserve(rf_frontend_mode_t mode) {
  if (mode <= RF_FRONTEND_IDLE || mode >= RF_FRONTEND_COUNT)
    return ESP_ERR_INVALID_ARG;
  void *task = xTaskGetCurrentTaskHandle();
  esp_err_t result = ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&s_lock);
  if (s_mode == RF_FRONTEND_IDLE && s_reservation_owner == NULL) {
    s_reservation_owner = task;
    s_reserved_mode = mode;
    result = ESP_OK;
  }
  portEXIT_CRITICAL(&s_lock);
  return result;
}

esp_err_t rf_frontend_unreserve(rf_frontend_mode_t mode) {
  void *task = xTaskGetCurrentTaskHandle();
  esp_err_t result = ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&s_lock);
  if (s_reservation_owner == task && s_reserved_mode == mode && s_mode == RF_FRONTEND_IDLE) {
    s_reservation_owner = NULL;
    s_reserved_mode = RF_FRONTEND_IDLE;
    result = ESP_OK;
  }
  portEXIT_CRITICAL(&s_lock);
  return result;
}

bool rf_frontend_can_access(rf_frontend_mode_t mode) {
  if (mode <= RF_FRONTEND_IDLE || mode >= RF_FRONTEND_COUNT)
    return false;
  void *task = xTaskGetCurrentTaskHandle();
  portENTER_CRITICAL(&s_lock);
  bool can_access =
      s_reservation_owner == NULL || (s_reservation_owner == task && s_reserved_mode == mode);
  portEXIT_CRITICAL(&s_lock);
  return can_access;
}

rf_frontend_mode_t rf_frontend_get_mode(void) {
  portENTER_CRITICAL(&s_lock);
  rf_frontend_mode_t mode = s_mode;
  portEXIT_CRITICAL(&s_lock);
  return mode;
}

const char *rf_frontend_mode_name(rf_frontend_mode_t mode) {
  switch (mode) {
    case RF_FRONTEND_LF_125KHZ:
      return "LF 125 kHz";
    case RF_FRONTEND_NFC_13MHZ:
      return "NFC 13.56 MHz";
    default:
      return "idle";
  }
}
