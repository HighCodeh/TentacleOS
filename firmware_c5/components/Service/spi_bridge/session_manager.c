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

#include "session_manager.h"

#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "spi_bridge.h"

static const char *TAG = "SESSION_MGR";

#define SESSION_TIMEOUT_MS  5000
#define WATCHDOG_PERIOD_MS  1000
#define WATCHDOG_STACK_SIZE 3072
#define WATCHDOG_PRIO SYS_PRIO_SERVICE_HI
#define DROP_LOG_INTERVAL   1000

typedef struct {
  uint32_t id;
  spi_id_t op_id;
  uint64_t last_heartbeat_us;
  uint32_t seq_counter;
  uint32_t last_acked_seq;
  uint32_t dropped_packets;
  uint32_t total_dropped;
  session_kill_cb_t kill_cb;
} session_state_t;

static session_state_t s_session = {0};
static SemaphoreHandle_t s_mutex = NULL;

static uint32_t generate_session_id(void) {
  uint32_t id;
  do {
    id = esp_random();
  } while (id == SPI_SESSION_INVALID_ID);
  return id;
}

static void close_active_locked(const char *reason) {
  if (s_session.id == SPI_SESSION_INVALID_ID)
    return;

  ESP_LOGW(TAG,
           "Closing session 0x%08lx (op 0x%04X): %s",
           (unsigned long)s_session.id,
           s_session.op_id,
           reason);

  if (s_session.total_dropped > 0) {
    ESP_LOGW(TAG,
             "Session dropped %lu packets total due to backpressure",
             (unsigned long)s_session.total_dropped);
  }

  session_kill_cb_t cb = s_session.kill_cb;
  spi_id_t op_id = s_session.op_id;
  memset(&s_session, 0, sizeof(s_session));

  if (cb != NULL)
    cb(op_id);
}

static void emit_session_lost(uint32_t session_id, spi_id_t op_id) {
  spi_session_lost_t payload = {.session_id = session_id, .cmd = (uint16_t)op_id};
  spi_bridge_stream_push(SPI_ID_SESSION_LOST, (const uint8_t *)&payload, sizeof(payload));
}

static void watchdog_task(void *arg) {
  (void)arg;
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(WATCHDOG_PERIOD_MS));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_session.id != SPI_SESSION_INVALID_ID) {
      uint64_t now_us = esp_timer_get_time();
      uint64_t age_ms = (now_us - s_session.last_heartbeat_us) / 1000ULL;
      if (age_ms > SESSION_TIMEOUT_MS) {
        uint32_t lost_id = s_session.id;
        spi_id_t lost_op = s_session.op_id;
        close_active_locked("heartbeat timeout");
        xSemaphoreGive(s_mutex);
        emit_session_lost(lost_id, lost_op);
        continue;
      }
    }
    xSemaphoreGive(s_mutex);
  }
}

void session_manager_init(void) {
  if (s_mutex != NULL)
    return;
  s_mutex = xSemaphoreCreateMutex();
  xTaskCreate(watchdog_task, "session_wdt", WATCHDOG_STACK_SIZE, NULL, WATCHDOG_PRIO, NULL);
  ESP_LOGI(TAG,
           "Session manager started (timeout %dms, window %u)",
           SESSION_TIMEOUT_MS,
           SPI_SESSION_WINDOW);
}

uint32_t session_manager_start(spi_id_t op_id, session_kill_cb_t kill_cb) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_session.id != SPI_SESSION_INVALID_ID) {
    close_active_locked("preempted by new session");
  }

  s_session.id = generate_session_id();
  s_session.op_id = op_id;
  s_session.last_heartbeat_us = esp_timer_get_time();
  s_session.seq_counter = 0;
  s_session.last_acked_seq = 0;
  s_session.dropped_packets = 0;
  s_session.total_dropped = 0;
  s_session.kill_cb = kill_cb;
  uint32_t id = s_session.id;
  xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG, "Session 0x%08lx opened for op 0x%04X", (unsigned long)id, op_id);
  return id;
}

esp_err_t session_manager_stop(uint32_t session_id) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_session.id != session_id || session_id == SPI_SESSION_INVALID_ID) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_NOT_FOUND;
  }
  close_active_locked("explicit stop");
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

bool session_manager_heartbeat(uint32_t session_id, uint32_t last_acked_seq) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool match = (s_session.id == session_id && session_id != SPI_SESSION_INVALID_ID);
  if (match) {
    s_session.last_heartbeat_us = esp_timer_get_time();
    if (last_acked_seq > s_session.last_acked_seq) {
      s_session.last_acked_seq = last_acked_seq;
    }
  }
  xSemaphoreGive(s_mutex);
  return match;
}

esp_err_t session_manager_try_emit(uint32_t session_id, const uint8_t *data, uint8_t len) {
  if (data == NULL && len > 0)
    return ESP_ERR_INVALID_ARG;
  if (len > SPI_MAX_PAYLOAD - sizeof(spi_stream_meta_t))
    return ESP_ERR_INVALID_SIZE;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_session.id != session_id || session_id == SPI_SESSION_INVALID_ID) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  uint32_t in_flight = s_session.seq_counter - s_session.last_acked_seq;
  if (in_flight >= SPI_SESSION_WINDOW) {
    s_session.dropped_packets++;
    s_session.total_dropped++;
    if (s_session.dropped_packets >= DROP_LOG_INTERVAL) {
      ESP_LOGW(TAG,
               "Backpressure: dropped %lu more packets (total %lu)",
               (unsigned long)s_session.dropped_packets,
               (unsigned long)s_session.total_dropped);
      s_session.dropped_packets = 0;
    }
    xSemaphoreGive(s_mutex);
    return ESP_ERR_NO_MEM;
  }

  uint8_t buf[SPI_MAX_PAYLOAD];
  spi_stream_meta_t meta = {.session_id = session_id, .seq = ++s_session.seq_counter};
  memcpy(buf, &meta, sizeof(meta));
  if (len > 0)
    memcpy(buf + sizeof(meta), data, len);
  spi_id_t op = s_session.op_id;
  uint8_t total = sizeof(meta) + len;
  xSemaphoreGive(s_mutex);

  return spi_bridge_stream_push(op, buf, total) ? ESP_OK : ESP_ERR_NO_MEM;
}
