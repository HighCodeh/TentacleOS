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

#include "spi_session.h"

#include <string.h>

#include "esp_log.h"
#include "led_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "spi_bridge.h"
#include "spi_timeouts.h"

static const char *TAG = "SPI_SESSION";

#define HEARTBEAT_INTERVAL_MS 2000
#define HEARTBEAT_FAIL_LIMIT  3
#define HEARTBEAT_TIMEOUT_MS  1000
#define HEARTBEAT_STACK_SIZE  3072
#define HEARTBEAT_PRIO SYS_PRIO_SERVICE_HI

typedef struct {
  uint32_t session_id;
  spi_id_t op_id;
  uint32_t last_acked_seq;
  spi_session_stream_cb_t on_stream;
  spi_session_lost_cb_t on_lost;
  TaskHandle_t heartbeat_task;
  bool stop_requested;
} session_state_t;

static session_state_t s_state = {0};
static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

static void notify_lost_locked(const char *reason) {
  if (s_state.session_id == SPI_SESSION_INVALID_ID)
    return;

  ESP_LOGW(TAG,
           "Session 0x%08lx (op 0x%04X) lost: %s",
           (unsigned long)s_state.session_id,
           s_state.op_id,
           reason);
  led_signal_warning();

  uint32_t lost_id = s_state.session_id;
  spi_id_t lost_op = s_state.op_id;
  spi_session_lost_cb_t cb = s_state.on_lost;
  spi_id_t stream_id = s_state.op_id;

  spi_bridge_unregister_stream_cb(stream_id);
  memset(&s_state, 0, sizeof(s_state));

  if (cb != NULL)
    cb(lost_id, lost_op);
}

static void on_session_stream(spi_id_t id, const uint8_t *payload, uint8_t len) {
  (void)id;
  if (len < sizeof(spi_stream_meta_t))
    return;

  spi_stream_meta_t meta;
  memcpy(&meta, payload, sizeof(meta));

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_state.session_id == SPI_SESSION_INVALID_ID || meta.session_id != s_state.session_id) {
    xSemaphoreGive(s_mutex);
    return; // stale or alien stream — drop silently
  }
  if (meta.seq > s_state.last_acked_seq) {
    s_state.last_acked_seq = meta.seq;
  }
  spi_session_stream_cb_t cb = s_state.on_stream;
  xSemaphoreGive(s_mutex);

  if (cb != NULL) {
    cb(payload + sizeof(meta), (uint8_t)(len - sizeof(meta)));
  }
}

static void on_session_lost_stream(spi_id_t id, const uint8_t *payload, uint8_t len) {
  (void)id;
  if (len < sizeof(spi_session_lost_t))
    return;

  spi_session_lost_t lost;
  memcpy(&lost, payload, sizeof(lost));

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (lost.session_id == s_state.session_id && s_state.session_id != SPI_SESSION_INVALID_ID) {
    notify_lost_locked("slave watchdog");
  }
  xSemaphoreGive(s_mutex);
}

static void heartbeat_task(void *arg) {
  uint32_t session_id = (uint32_t)(uintptr_t)arg;
  uint8_t consecutive_fails = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool still_mine = (s_state.session_id == session_id) && !s_state.stop_requested;
    spi_heartbeat_req_t req = {.session_id = session_id, .last_acked_seq = s_state.last_acked_seq};
    xSemaphoreGive(s_mutex);
    if (!still_mine)
      break;

    spi_header_t resp_header = {0};
    spi_heartbeat_resp_t resp = {0};
    esp_err_t ret = spi_bridge_send_command(SPI_ID_SESSION_HEARTBEAT,
                                            (uint8_t *)&req,
                                            sizeof(req),
                                            &resp_header,
                                            (uint8_t *)&resp,
                                            HEARTBEAT_TIMEOUT_MS);
    bool ok = (ret == ESP_OK) && (resp.alive != 0);
    if (ok) {
      consecutive_fails = 0;
      continue;
    }

    consecutive_fails++;
    ESP_LOGW(TAG,
             "Heartbeat fail %u/%d for session 0x%08lx",
             consecutive_fails,
             HEARTBEAT_FAIL_LIMIT,
             (unsigned long)session_id);
    led_signal_warning();
    if (consecutive_fails >= HEARTBEAT_FAIL_LIMIT) {
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      if (s_state.session_id == session_id) {
        notify_lost_locked("heartbeat fail limit");
      }
      xSemaphoreGive(s_mutex);
      break;
    }
  }

  vTaskDelete(NULL);
}

void spi_session_init(void) {
  if (s_initialized)
    return;
  s_mutex = xSemaphoreCreateMutex();
  spi_bridge_register_stream_cb(SPI_ID_SESSION_LOST, on_session_lost_stream);
  s_initialized = true;
  ESP_LOGI(TAG, "Session client started");
}

uint32_t spi_session_start(spi_id_t op_id,
                           const uint8_t *params,
                           uint8_t params_len,
                           spi_session_stream_cb_t on_stream,
                           spi_session_lost_cb_t on_lost) {
  if (!s_initialized)
    spi_session_init();

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_state.session_id != SPI_SESSION_INVALID_ID) {
    ESP_LOGW(TAG,
             "Replacing active session 0x%08lx with new start of op 0x%02X",
             (unsigned long)s_state.session_id,
             op_id);
    led_signal_warning();
    notify_lost_locked("preempted by local start");
  }
  xSemaphoreGive(s_mutex);

  spi_header_t resp_header = {0};
  spi_session_resp_t resp = {0};
  esp_err_t ret = spi_bridge_send_command(
      op_id, params, params_len, &resp_header, (uint8_t *)&resp, spi_bridge_get_timeout(op_id));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "START op 0x%04X bridge error: %s", op_id, esp_err_to_name(ret));
    led_signal_error();
    return SPI_SESSION_INVALID_ID;
  }
  if (resp.session_id == SPI_SESSION_INVALID_ID) {
    ESP_LOGE(TAG, "START op 0x%04X did not return a session id", op_id);
    led_signal_error();
    return SPI_SESSION_INVALID_ID;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_state.session_id = resp.session_id;
  s_state.op_id = op_id;
  s_state.last_acked_seq = 0;
  s_state.on_stream = on_stream;
  s_state.on_lost = on_lost;
  s_state.stop_requested = false;
  spi_bridge_register_stream_cb(op_id, on_session_stream);
  BaseType_t ok = xTaskCreatePinnedToCore(heartbeat_task,
                                          "spi_session_hb",
                                          HEARTBEAT_STACK_SIZE,
                                          (void *)(uintptr_t)resp.session_id,
                                          HEARTBEAT_PRIO,
                                          &s_state.heartbeat_task,
                                          SYS_CORE_RADIO);
  uint32_t session_id = s_state.session_id;
  xSemaphoreGive(s_mutex);

  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create heartbeat task");
    led_signal_error();
    spi_session_stop(session_id);
    return SPI_SESSION_INVALID_ID;
  }

  ESP_LOGI(TAG, "Session 0x%08lx started for op 0x%04X", (unsigned long)session_id, op_id);
  return session_id;
}

esp_err_t spi_session_stop(uint32_t session_id) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_state.session_id != session_id || session_id == SPI_SESSION_INVALID_ID) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_NOT_FOUND;
  }
  s_state.stop_requested = true;
  xSemaphoreGive(s_mutex);

  spi_session_stop_req_t req = {.session_id = session_id};
  spi_bridge_send_command(SPI_ID_SESSION_STOP,
                          (uint8_t *)&req,
                          sizeof(req),
                          NULL,
                          NULL,
                          spi_bridge_get_timeout(SPI_ID_SESSION_STOP));

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_state.session_id == session_id) {
    spi_bridge_unregister_stream_cb(s_state.op_id);
    memset(&s_state, 0, sizeof(s_state));
  }
  xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG, "Session 0x%08lx stopped", (unsigned long)session_id);
  return ESP_OK;
}

bool spi_session_is_active(uint32_t session_id) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool active = (s_state.session_id == session_id) && (session_id != SPI_SESSION_INVALID_ID);
  xSemaphoreGive(s_mutex);
  return active;
}
