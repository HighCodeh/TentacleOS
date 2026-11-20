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

#include <stdatomic.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "spi_bridge.h"
#include "sys_prio.h"

#define BOARD_POLL_MS    500
#define BOARD_RPC_MS     250
#define BOARD_FRESH_MS   1500
#define BOARD_STACK_SIZE 4096
#define BOARD_US_PER_MS  1000

_Static_assert(sizeof(spi_board_negotiate_t) == 16, "board negotiate wire size");
_Static_assert(sizeof(spi_board_caps_t) == 24, "board capabilities wire size");
_Static_assert(sizeof(spi_board_set_t) == 12, "board set wire size");
_Static_assert(sizeof(spi_board_state_t) == 12, "board state wire size");
_Static_assert(BOARD_CONTROL_COUNT == SPI_BOARD_CONTROL_COUNT, "board control count");

typedef struct {
  spi_id_t id;
  const void *data;
  uint8_t length;
  uint8_t expected;
} board_service_rpc_t;

static const char *TAG = "BOARD_SERVICE";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static bool s_started;
static atomic_flag s_poll_busy = ATOMIC_FLAG_INIT;
static uint64_t s_client;
static int64_t s_observed_at;
static board_control_status_t s_status = {.link_result = ESP_ERR_INVALID_STATE};

static esp_err_t exchange(const board_service_rpc_t *request, void *out) {
  // One extra byte detects an oversized response despite bridge copy clamping.
  uint8_t reply[sizeof(spi_board_caps_t) + 1] = {0};
  spi_header_t header = {0};
  esp_err_t ret = spi_bridge_send_command(
      request->id, request->data, request->length, &header, reply, sizeof(reply), BOARD_RPC_MS);
  if (ret != ESP_OK)
    return ret;
  if (header.length != request->expected)
    return ESP_ERR_INVALID_RESPONSE;
  memcpy(out, reply, request->expected);
  return ESP_OK;
}

static void invalidate(esp_err_t result) {
  portENTER_CRITICAL(&s_lock);
  s_status.is_compatible = false;
  s_status.is_input_known = false;
  s_status.link_result = result;
  for (size_t i = 0; i < BOARD_CONTROL_COUNT; i++) {
    s_status.outputs[i].is_known = false;
    if (s_status.outputs[i].is_pending) {
      s_status.outputs[i].is_pending = false;
      s_status.outputs[i].result = result;
    }
  }
  portEXIT_CRITICAL(&s_lock);
}

static esp_err_t observe(const spi_board_state_t *state, uint64_t epoch, uint32_t caps) {
  if (state->epoch != epoch || state->reserved != 0 ||
      (state->values & ~SPI_BOARD_OUTPUT_MASK) != 0 ||
      (state->valid & ~SPI_BOARD_OUTPUT_MASK) != 0 ||
      (state->inputs & ~(SPI_BOARD_INPUT_INT_HIGH | SPI_BOARD_INPUT_IRQ_PENDING)) != 0)
    return ESP_ERR_INVALID_RESPONSE;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  s_status.epoch = epoch;
  s_status.capabilities = caps;
  s_status.is_compatible = true;
  s_status.link_result = ESP_OK;
  s_status.is_haptic_irq_pending = (state->inputs & SPI_BOARD_INPUT_IRQ_PENDING) != 0;
  s_status.is_haptic_int_high = (state->inputs & SPI_BOARD_INPUT_INT_HIGH) != 0;
  s_status.is_input_known = (caps & SPI_BOARD_CAP_HAPTIC_INPUT) != 0;
  for (size_t i = 0; i < BOARD_CONTROL_COUNT; i++) {
    s_status.outputs[i].confirmed = (state->values & (1U << i)) != 0;
    s_status.outputs[i].is_known = (state->valid & caps & (1U << i)) != 0;
  }
  s_observed_at = now;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

static esp_err_t get_status(board_control_status_t *out_status) {
  if (xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  if (out_status == NULL)
    return ESP_ERR_INVALID_ARG;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  *out_status = s_status;
  int64_t age = now - s_observed_at;
  portEXIT_CRITICAL(&s_lock);
  if (out_status->is_compatible && age >= BOARD_FRESH_MS * BOARD_US_PER_MS) {
    out_status->is_compatible = false;
    out_status->is_input_known = false;
    out_status->link_result = ESP_ERR_TIMEOUT;
    for (size_t i = 0; i < BOARD_CONTROL_COUNT; i++)
      out_status->outputs[i].is_known = false;
  }
  return ESP_OK;
}

static esp_err_t request(board_control_id_t control, bool value) {
  if (xPortInIsrContext())
    return ESP_ERR_INVALID_STATE;
  int64_t now = esp_timer_get_time();
  esp_err_t ret = ESP_OK;
  portENTER_CRITICAL(&s_lock);
  if (!s_started || !s_status.is_compatible ||
      now - s_observed_at >= BOARD_FRESH_MS * BOARD_US_PER_MS) {
    ret = s_status.link_result == ESP_ERR_NOT_SUPPORTED ? ESP_ERR_NOT_SUPPORTED
                                                        : ESP_ERR_INVALID_STATE;
  } else if (!(s_status.capabilities & (1U << control))) {
    ret = ESP_ERR_NOT_SUPPORTED;
  } else if (s_status.outputs[control].is_pending) {
    ret = ESP_ERR_INVALID_STATE;
  } else {
    s_status.outputs[control].has_request = true;
    s_status.outputs[control].requested = value;
    s_status.outputs[control].is_pending = true;
    s_status.outputs[control].result = ESP_ERR_NOT_FINISHED;
  }
  portEXIT_CRITICAL(&s_lock);
  if (ret == ESP_OK)
    xTaskNotifyGive(s_task);
  return ret;
}

static esp_err_t request_amplifier(bool enabled) {
  return request(BOARD_CONTROL_AMP, enabled);
}
static esp_err_t request_haptic(bool released) {
  return request(BOARD_CONTROL_HAPTIC, released);
}
static esp_err_t request_usb(bool enabled) {
  return request(BOARD_CONTROL_USB, enabled);
}

static const board_control_ops_t CONTROL_OPS = {
    .request_amplifier = request_amplifier,
    .request_haptic_release = request_haptic,
    .request_usb_enable = request_usb,
    .get_status = get_status,
};

const board_control_ops_t *board_service_controls(void) {
  return &CONTROL_OPS;
}

// Only this worker performs SPI; no snapshot lock is held across a transaction.
static void poll_once(void) {
  board_control_status_t view;
  (void)get_status(&view);
  spi_board_caps_t caps = {0};
  uint64_t epoch = view.epoch;
  uint32_t capabilities = view.capabilities;
  esp_err_t ret;
  if (!view.is_compatible) {
    // Clear all outstanding intent before negotiating a different/expired peer.
    invalidate(view.link_result == ESP_OK ? ESP_ERR_INVALID_STATE : view.link_result);
    spi_board_negotiate_t hello = {
        .protocol = SPI_PROTOCOL_VERSION,
        .revision = SPI_BOARD_REVISION,
        .model = SPI_BOARD_MODEL_V203,
        .client = s_client,
    };
    const board_service_rpc_t rpc = {
        SPI_ID_BOARD_NEGOTIATE,
        &hello,
        sizeof(hello),
        sizeof(caps),
    };
    ret = exchange(&rpc, &caps);
    if (ret != ESP_OK) {
      invalidate(ret);
      return;
    }
    if (caps.protocol != SPI_PROTOCOL_VERSION || caps.revision != SPI_BOARD_REVISION ||
        caps.model != SPI_BOARD_MODEL_V203 || caps.reserved != 0 || caps.epoch == 0 ||
        caps.lease_ms != SPI_BOARD_LEASE_MS) {
      invalidate(ESP_ERR_NOT_SUPPORTED);
      return;
    }
    epoch = caps.epoch;
    capabilities = caps.capabilities;
  }
  spi_board_query_t query = {.epoch = epoch};
  spi_board_state_t state;
  const board_service_rpc_t read = {SPI_ID_BOARD_STATE, &query, sizeof(query), sizeof(state)};
  ret = exchange(&read, &state);
  if (ret == ESP_OK)
    ret = observe(&state, epoch, capabilities);
  if (ret != ESP_OK) {
    invalidate(ret);
    return;
  }

  for (uint8_t control = 0; control < BOARD_CONTROL_COUNT; control++) {
    (void)get_status(&view);
    if (!view.outputs[control].is_pending)
      continue;
    spi_board_set_t set = {
        .epoch = epoch,
        .revision = SPI_BOARD_REVISION,
        .control = control,
        .value = view.outputs[control].requested ? 1 : 0,
    };
    const board_service_rpc_t write = {SPI_ID_BOARD_SET, &set, sizeof(set), sizeof(state)};
    ret = exchange(&write, &state);
    if (ret == ESP_OK)
      ret = observe(&state, epoch, capabilities);
    if (ret == ESP_OK &&
        (!(state.valid & (1U << control)) || !!(state.values & (1U << control)) != set.value)) {
      ret = ESP_ERR_INVALID_RESPONSE;
    }
    if (ret != ESP_OK) {
      invalidate(ret);
      return;
    }
    portENTER_CRITICAL(&s_lock);
    s_status.outputs[control].is_pending = false;
    s_status.outputs[control].result = ESP_OK;
    portEXIT_CRITICAL(&s_lock);
  }
}

esp_err_t board_service_reconcile(void) {
  if (!s_started || atomic_flag_test_and_set(&s_poll_busy))
    return ESP_ERR_INVALID_STATE;
  invalidate(ESP_ERR_INVALID_STATE);
  poll_once();
  board_control_status_t status;
  (void)get_status(&status);
  atomic_flag_clear(&s_poll_busy);
  return status.is_compatible ? ESP_OK : status.link_result;
}

static void worker(void *arg) {
  (void)arg;
  for (;;) {
    if (!atomic_flag_test_and_set(&s_poll_busy)) {
      poll_once();
      atomic_flag_clear(&s_poll_busy);
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BOARD_POLL_MS));
  }
}

esp_err_t board_service_init(void) {
  // Boot-only initializer; consumers use the immutable callback interface.
  if (s_started)
    return ESP_OK;
  do {
    esp_fill_random(&s_client, sizeof(s_client));
  } while (s_client == 0);
  if (xTaskCreatePinnedToCore(worker,
                              "board_control",
                              BOARD_STACK_SIZE,
                              NULL,
                              SYS_PRIO_SERVICE_LO,
                              &s_task,
                              SYS_CORE_RADIO) != pdPASS) {
    ESP_LOGE(TAG, "Control worker allocation failed");
    return ESP_ERR_NO_MEM;
  }
  portENTER_CRITICAL(&s_lock);
  s_started = true;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}
