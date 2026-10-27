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

#include "host_link_dispatch.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "HOST_DISP";

#define HOST_LINK_MAX_HANDLERS 32

static host_link_handler_t s_handlers[HOST_LINK_MAX_HANDLERS];
static size_t s_count = 0;
static SemaphoreHandle_t s_lock = NULL;

static bool ensure_lock(void) {
  if (s_lock == NULL)
    s_lock = xSemaphoreCreateMutex();
  return s_lock != NULL;
}

esp_err_t host_link_register(const host_link_handler_t *h) {
  if (h == NULL || h->handle == NULL || h->op_min > h->op_max)
    return ESP_ERR_INVALID_ARG;
  if (!ensure_lock())
    return ESP_ERR_NO_MEM;

  esp_err_t err = ESP_OK;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (size_t i = 0; i < s_count; i++) {
    if (s_handlers[i].category == h->category && s_handlers[i].op_min == h->op_min) {
      ESP_LOGW(TAG, "handler cat 0x%02X op 0x%02X already registered", h->category, h->op_min);
      err = ESP_ERR_INVALID_STATE;
      goto out;
    }
  }
  if (s_count >= HOST_LINK_MAX_HANDLERS) {
    ESP_LOGE(TAG, "dispatch table full (%d), dropping '%s'", HOST_LINK_MAX_HANDLERS, h->name);
    err = ESP_ERR_NO_MEM;
    goto out;
  }
  s_handlers[s_count++] = *h;

out:
  xSemaphoreGive(s_lock);
  return err;
}

esp_err_t host_link_unregister(uint8_t category, uint8_t op_min) {
  if (s_lock == NULL)
    return ESP_ERR_INVALID_STATE;

  esp_err_t err = ESP_ERR_NOT_FOUND;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (size_t i = 0; i < s_count; i++) {
    if (s_handlers[i].category == category && s_handlers[i].op_min == op_min) {
      for (size_t j = i + 1; j < s_count; j++)
        s_handlers[j - 1] = s_handlers[j];
      s_count--;
      err = ESP_OK;
      break;
    }
  }
  xSemaphoreGive(s_lock);
  return err;
}

bool host_link_dispatch_lookup(uint16_t cmd, host_link_handler_t *out_copy) {
  if (out_copy == NULL || s_lock == NULL)
    return false;

  uint8_t cat = (uint8_t)(cmd >> 8);
  uint8_t op = (uint8_t)(cmd & 0xFF);
  bool found = false;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  for (size_t i = 0; i < s_count; i++) {
    const host_link_handler_t *e = &s_handlers[i];
    if (e->category == cat && op >= e->op_min && op <= e->op_max) {
      *out_copy = *e;
      found = true;
      break;
    }
  }
  xSemaphoreGive(s_lock);
  return found;
}
