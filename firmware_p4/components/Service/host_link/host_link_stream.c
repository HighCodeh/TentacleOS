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

// Companion streaming + heartbeat proxy. The app starts a long-running op (the
// WiFi sniffer) over the host link; this module runs it through the existing
// spi_session model and forwards every record to the app as a STREAM frame.
// Two-level liveness: the app heartbeats the P4 here; the spi_session layer
// keeps heartbeating the C5 on its own. If the app goes silent (watchdog) or
// the link drops, we stop the session, which stops the P4→C5 heartbeat → the
// C5 watchdog reaps it.

#include "host_link_stream.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "host_link.h"
#include "spi_protocol.h"
#include "spi_session.h"

static const char *TAG = "HOST_LINK_STREAM";

#define STREAM_APP_TIMEOUT_MS  6000 // tear down if no app heartbeat within this
#define STREAM_WATCHDOG_TICK_MS 1000
#define STREAM_WD_TASK_STK     3072
#define STREAM_WD_TASK_PRIO SYS_PRIO_SERVICE_LO

// Ops the proxy owns (start via spi_session and push records to the app).
static const uint16_t SESSION_OPS[] = {SPI_ID_WIFI_APP_SNIFFER};

static SemaphoreHandle_t s_lock = NULL;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;
static uint16_t s_session_op = 0;
static int64_t s_last_app_hb_us = 0;
static TaskHandle_t s_watchdog = NULL;

static void on_stream(const uint8_t *data, uint8_t len);
static void on_lost(uint32_t session_id, spi_id_t op_id);
static void watchdog_task(void *arg);

esp_err_t host_link_stream_init(void) {
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL)
      return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool host_stream_is_session_op(uint16_t cmd) {
  for (size_t i = 0; i < sizeof(SESSION_OPS) / sizeof(SESSION_OPS[0]); i++) {
    if (SESSION_OPS[i] == cmd)
      return true;
  }
  return false;
}

static void stop_locked(void) {
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    uint32_t sid = s_session_id;
    s_session_id = SPI_SESSION_INVALID_ID;
    s_session_op = 0;
    xSemaphoreGive(s_lock);
    spi_session_stop(sid); // sends STOP to C5 + kills the P4→C5 heartbeat
    xSemaphoreTake(s_lock, portMAX_DELAY);
  }
}

void host_stream_teardown(void) {
  if (s_lock == NULL)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  stop_locked();
  xSemaphoreGive(s_lock);
}

uint8_t host_stream_start(uint16_t cmd, const uint8_t *payload, uint16_t plen, uint8_t *out_data,
                          uint16_t out_cap, uint16_t *out_len) {
  *out_len = 0;
  if (out_cap < sizeof(uint32_t)) {
    return SPI_STATUS_ERROR;
  }
  if (plen > SPI_MAX_PAYLOAD) {
    return SPI_STATUS_INVALID_ARG;
  }

  // spi_session takes one global session; replace any prior one we held.
  host_stream_teardown();

  uint32_t sid = spi_session_start(cmd, payload, (uint8_t)plen, on_stream, on_lost);
  if (sid == SPI_SESSION_INVALID_ID) {
    return SPI_STATUS_ERROR;
  }

  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_session_id = sid;
  s_session_op = cmd;
  s_last_app_hb_us = esp_timer_get_time();
  xSemaphoreGive(s_lock);

  if (s_watchdog == NULL) {
    xTaskCreatePinnedToCore(watchdog_task, "hl_stream_wd", STREAM_WD_TASK_STK, NULL,
                            STREAM_WD_TASK_PRIO, &s_watchdog, SYS_CORE_RADIO);
  }

  memcpy(out_data, &sid, sizeof(sid));
  *out_len = sizeof(sid);
  ESP_LOGI(TAG, "Stream session 0x%08lx started for op 0x%04X", (unsigned long)sid, cmd);
  return SPI_STATUS_OK;
}

uint8_t host_stream_session_ctrl(uint16_t cmd, const uint8_t *payload, uint16_t plen,
                                 uint8_t *out_data, uint16_t out_cap, uint16_t *out_len) {
  (void)payload;
  (void)plen;
  *out_len = 0;

  switch (cmd) {
    case SPI_ID_SESSION_HEARTBEAT: {
      // App proved it is alive; refresh the deadline. The P4 keeps the C5
      // session alive via spi_session's own heartbeat, so we do not relay.
      xSemaphoreTake(s_lock, portMAX_DELAY);
      s_last_app_hb_us = esp_timer_get_time();
      bool active = (s_session_id != SPI_SESSION_INVALID_ID);
      xSemaphoreGive(s_lock);
      if (out_cap >= 1) {
        out_data[0] = active ? 1 : 0; // mirrors spi_heartbeat_resp_t.alive
        *out_len = 1;
      }
      return SPI_STATUS_OK;
    }

    case SPI_ID_SESSION_STOP:
      host_stream_teardown();
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}

// spi_session callback: meta already stripped; for the sniffer this is
// [i8 rssi][u8 channel][u8 len][frame...]. Push it to the app verbatim.
static void on_stream(const uint8_t *data, uint8_t len) {
  uint16_t op;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  op = s_session_op;
  bool active = (s_session_id != SPI_SESSION_INVALID_ID);
  xSemaphoreGive(s_lock);
  if (!active) {
    return;
  }
  host_link_emit_stream(SPI_CMD_CAT(op), SPI_CMD_OP(op), data, len);
}

static void on_lost(uint32_t session_id, spi_id_t op_id) {
  (void)session_id;
  // The C5 (or a preempting start) ended the session. Tell the app the stream
  // is over via a SESSION/LOST STREAM frame, then clear local state.
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool was_ours = (op_id == s_session_op && s_session_id != SPI_SESSION_INVALID_ID);
  s_session_id = SPI_SESSION_INVALID_ID;
  s_session_op = 0;
  xSemaphoreGive(s_lock);

  if (was_ours) {
    host_link_emit_stream(SPI_CAT_SESSION, SPI_CMD_OP(SPI_ID_SESSION_LOST), NULL, 0);
  }
}

static void watchdog_task(void *arg) {
  (void)arg;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(STREAM_WATCHDOG_TICK_MS));

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool active = (s_session_id != SPI_SESSION_INVALID_ID);
    int64_t since_us = esp_timer_get_time() - s_last_app_hb_us;
    bool stale = active && (since_us > (int64_t)STREAM_APP_TIMEOUT_MS * 1000);
    if (stale) {
      ESP_LOGW(TAG, "App heartbeat stale; tearing down stream session");
      stop_locked();
    }
    xSemaphoreGive(s_lock);
  }
}
