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

#include "board_service.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_pins.h"

#define BOARD_LOCK_MS     20
#define BOARD_WATCHDOG_US 100000
#define BOARD_US_PER_MS   1000

static const char *TAG = "BOARD_SERVICE";
static SemaphoreHandle_t s_mutex;
static esp_timer_handle_t s_timer;
static uint64_t s_client;
static uint64_t s_epoch;
static int64_t s_deadline;
static uint8_t s_values = SPI_BOARD_SAFE_VALUES;
static uint8_t s_valid;
static bool s_ready;

static uint64_t new_epoch(void) {
  uint64_t value;
  do {
    esp_fill_random(&value, sizeof(value));
  } while (value == 0 || value == s_epoch);
  return value;
}

static esp_err_t apply(uint8_t control, bool value) {
  esp_err_t ret;
  switch (control) {
    case SPI_BOARD_AMP:
      ret = board_pins_set_amplifier(value);
      break;
    case SPI_BOARD_HAPTIC:
      ret = board_pins_set_haptic_release(value);
      break;
    case SPI_BOARD_USB:
      ret = board_pins_set_usb_enable(value);
      break;
    default:
      return ESP_ERR_NOT_SUPPORTED;
  }
  uint8_t bit = 1U << control;
  if (ret == ESP_OK) {
    s_values = (s_values & ~bit) | (value ? bit : 0);
    s_valid |= bit;
  } else {
    s_valid &= ~bit;
    ESP_LOGE(TAG, "Control %u failed: %s", control, esp_err_to_name(ret));
  }
  return ret;
}

static void safe_state(void) {
  for (uint8_t control = 0; control < SPI_BOARD_CONTROL_COUNT; control++) {
    (void)apply(control, (SPI_BOARD_SAFE_VALUES & (1U << control)) != 0);
  }
}

static void expire(void) {
  if (s_deadline != 0 && esp_timer_get_time() >= s_deadline) {
    s_client = 0;
    s_deadline = 0;
    s_epoch = new_epoch();
    safe_state();
    ESP_LOGW(TAG, "Control lease expired; safe state requested");
  } else if (s_deadline == 0 && s_valid != SPI_BOARD_OUTPUT_MASK) {
    safe_state();
  }
}

static void watchdog(void *arg) {
  (void)arg;
  if (xSemaphoreTake(s_mutex, 0) != pdTRUE)
    return;
  expire();
  xSemaphoreGive(s_mutex);
}

static void reply_state(board_service_reply_t *out_reply) {
  spi_board_state_t state = {
      .epoch = s_epoch,
      .values = s_values,
      .valid = s_valid,
      .inputs = (board_pins_haptic_int_high() ? SPI_BOARD_INPUT_INT_HIGH : 0) |
                (board_pins_haptic_irq_pending() ? SPI_BOARD_INPUT_IRQ_PENDING : 0),
  };
  memcpy(out_reply->data, &state, sizeof(state));
  out_reply->length = sizeof(state);
}

esp_err_t board_service_init(void) {
  if (s_ready)
    return ESP_OK;
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Control mutex allocation failed");
    return ESP_ERR_NO_MEM;
  }
  s_epoch = new_epoch();
  safe_state();
  const esp_timer_create_args_t config = {.callback = watchdog, .name = "board_safe"};
  esp_err_t ret = esp_timer_create(&config, &s_timer);
  if (ret == ESP_OK)
    ret = esp_timer_start_periodic(s_timer, BOARD_WATCHDOG_US);
  if (ret != ESP_OK) {
    if (s_timer != NULL) {
      esp_timer_delete(s_timer);
      s_timer = NULL;
    }
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    return ret;
  }
  s_ready = true;
  return ESP_OK;
}

spi_status_t board_service_dispatch(spi_id_t id,
                                    const uint8_t *data,
                                    uint8_t length,
                                    board_service_reply_t *out_reply) {
  if (out_reply == NULL)
    return SPI_STATUS_INVALID_ARG;
  out_reply->length = 0;
  if (!s_ready)
    return SPI_STATUS_UNSUPPORTED;
  if (data == NULL)
    return SPI_STATUS_INVALID_ARG;
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(BOARD_LOCK_MS)) != pdTRUE)
    return SPI_STATUS_BUSY;
  expire();
  spi_status_t status = SPI_STATUS_INVALID_ARG;
  if (id == SPI_ID_BOARD_NEGOTIATE && length == sizeof(spi_board_negotiate_t)) {
    spi_board_negotiate_t req;
    memcpy(&req, data, sizeof(req));
    if (req.reserved != 0 || req.client == 0)
      goto cleanup;
    if (req.protocol != SPI_PROTOCOL_VERSION || req.revision != SPI_BOARD_REVISION ||
        req.model != SPI_BOARD_MODEL_V203) {
      status = SPI_STATUS_UNSUPPORTED;
      goto cleanup;
    }
    if (req.client != s_client) {
      safe_state();
      s_epoch = new_epoch();
      s_client = req.client;
    }
    spi_board_caps_t caps = {
        .protocol = SPI_PROTOCOL_VERSION,
        .revision = SPI_BOARD_REVISION,
        .model = SPI_BOARD_MODEL_V203,
        .capabilities = SPI_BOARD_CAPS,
        .lease_ms = SPI_BOARD_LEASE_MS,
        .epoch = s_epoch,
    };
    memcpy(out_reply->data, &caps, sizeof(caps));
    out_reply->length = sizeof(caps);
    status = SPI_STATUS_OK;
  } else if (id == SPI_ID_BOARD_STATE && length == sizeof(spi_board_query_t)) {
    spi_board_query_t req;
    memcpy(&req, data, sizeof(req));
    if (s_client == 0 || req.epoch != s_epoch) {
      status = SPI_STATUS_BUSY;
      goto cleanup;
    }
    reply_state(out_reply);
    status = SPI_STATUS_OK;
  } else if (id == SPI_ID_BOARD_SET && length == sizeof(spi_board_set_t)) {
    spi_board_set_t req;
    memcpy(&req, data, sizeof(req));
    if (req.value > 1)
      goto cleanup;
    if (req.revision != SPI_BOARD_REVISION || req.control >= SPI_BOARD_CONTROL_COUNT) {
      status = SPI_STATUS_UNSUPPORTED;
      goto cleanup;
    }
    if (s_client == 0 || req.epoch != s_epoch) {
      status = SPI_STATUS_BUSY;
      goto cleanup;
    }
    // Level assignment is idempotent; retries never generate a reset pulse.
    uint8_t bit = 1U << req.control;
    if (!(s_valid & bit) || !!(s_values & bit) != req.value) {
      if (apply(req.control, req.value != 0) != ESP_OK) {
        s_client = 0;
        s_deadline = 0;
        s_epoch = new_epoch();
        safe_state();
        status = SPI_STATUS_ERROR;
        goto cleanup;
      }
    }
    reply_state(out_reply);
    status = SPI_STATUS_OK;
  } else if (id != SPI_ID_BOARD_NEGOTIATE && id != SPI_ID_BOARD_STATE && id != SPI_ID_BOARD_SET) {
    status = SPI_STATUS_UNSUPPORTED;
  }
  if (status == SPI_STATUS_OK) {
    s_deadline = esp_timer_get_time() + (int64_t)SPI_BOARD_LEASE_MS * BOARD_US_PER_MS;
  }
cleanup:
  xSemaphoreGive(s_mutex);
  return status;
}
