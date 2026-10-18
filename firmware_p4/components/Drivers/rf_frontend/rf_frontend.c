// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rf_frontend.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "RF_FRONTEND";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static rf_frontend_mode_t s_mode = RF_FRONTEND_IDLE;

esp_err_t rf_frontend_claim(rf_frontend_mode_t mode) {
  if (mode <= RF_FRONTEND_IDLE || mode >= RF_FRONTEND_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_err_t result = ESP_OK;
  portENTER_CRITICAL(&s_lock);
  if (s_mode != RF_FRONTEND_IDLE && s_mode != mode) {
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
  portENTER_CRITICAL(&s_lock);
  if (s_mode == mode) {
    s_mode = RF_FRONTEND_IDLE;
  }
  portEXIT_CRITICAL(&s_lock);
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
