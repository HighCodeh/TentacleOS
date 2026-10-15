// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rf_frontend.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static rf_frontend_mode_t s_mode;

static void ensure_lock(void) {
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
  }
}

esp_err_t rf_frontend_claim(rf_frontend_mode_t mode) {
  if (mode == RF_FRONTEND_IDLE) {
    return ESP_ERR_INVALID_ARG;
  }
  ensure_lock();
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  esp_err_t result = ESP_OK;
  if (s_mode != RF_FRONTEND_IDLE && s_mode != mode) {
    result = ESP_ERR_INVALID_STATE;
  } else {
    s_mode = mode;
  }
  xSemaphoreGive(s_lock);
  return result;
}

void rf_frontend_release(rf_frontend_mode_t mode) {
  ensure_lock();
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (s_mode == mode) {
      s_mode = RF_FRONTEND_IDLE;
    }
    xSemaphoreGive(s_lock);
  }
}

rf_frontend_mode_t rf_frontend_get_mode(void) {
  ensure_lock();
  rf_frontend_mode_t mode = RF_FRONTEND_IDLE;
  if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
    mode = s_mode;
    xSemaphoreGive(s_lock);
  }
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
