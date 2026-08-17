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

#include "meshtastic_phone_bridge.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "meshtastic_phoneapi.h"
#include "spi_bridge.h"

static const char *TAG = "MESH_BRIDGE";

#define BRIDGE_NOTIFY_TASK_STACK  4096
#define BRIDGE_NOTIFY_TASK_PRIO   SYS_PRIO_SERVICE_HI
#define BRIDGE_NOTIFY_TICK_MS     100
#define BRIDGE_SPI_TIMEOUT_MS     1000
#define BRIDGE_FROMRADIO_BUF_SIZE 512
#define BRIDGE_TORADIO_BUF_SIZE   512
#define BRIDGE_LOG_RING_SIZE      2048
#define BRIDGE_LOG_LINE_MAX       256
#define BRIDGE_CHUNK_PAYLOAD_MAX  SPI_MESH_CHUNK_PAYLOAD_MAX
#define BRIDGE_LOG_CHUNK_BYTES    SPI_MESH_CHUNK_PAYLOAD_MAX
#define BRIDGE_MAX_CHUNKS         255
#define BRIDGE_RETRY_TICKS        50

typedef struct {
  bool is_active;
  uint8_t seq;
  uint8_t total_chunks;
  uint8_t next_chunk_idx;
  uint16_t accumulated_len;
  uint8_t buf[BRIDGE_TORADIO_BUF_SIZE];
} bridge_reassembly_t;

static bool s_is_initialized = false;
static volatile bool s_is_running = false;
static uint32_t s_node_num = 0;
static TaskHandle_t s_notify_task = NULL;
static SemaphoreHandle_t s_status_mutex = NULL;
static spi_mesh_status_t s_cached_status = {0};
static uint8_t s_tx_seq = 0;
static uint8_t s_log_seq = 0;
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_log_ring[BRIDGE_LOG_RING_SIZE];
static uint16_t s_log_head = 0;
static uint16_t s_log_tail = 0;
static vprintf_like_t s_prev_vprintf = NULL;
static bridge_reassembly_t s_toradio = {0};
static volatile bool s_want_ble_active = false;
static volatile bool s_want_wifi_active = false;
static bool s_ble_active_on_c5 = false;
static bool s_wifi_active_on_c5 = false;
static uint16_t s_retry_ticks_remaining = 0;

static void notify_task(void *pvParameters);
static esp_err_t push_frame(spi_id_t id, uint8_t *seq_counter, const uint8_t *frame, uint16_t len);
static void on_toradio_stream(spi_id_t id, const uint8_t *payload, uint8_t len);
static int log_vprintf(const char *fmt, va_list args);
static void log_ring_push(const uint8_t *buf, size_t len);
static uint16_t log_ring_pop(uint8_t *out_buf, uint16_t max_len);
static esp_err_t fetch_status(spi_mesh_status_t *out_status);
static void store_status(const spi_mesh_status_t *status);
static esp_err_t request_ble_init(void);
static esp_err_t request_ble_stop(void);
static esp_err_t request_wifi_init(void);
static esp_err_t request_wifi_stop(void);
static void reconcile_transports(void);

esp_err_t meshtastic_phone_bridge_init(uint32_t node_num) {
  if (s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  s_node_num = node_num;
  s_tx_seq = 0;
  s_log_seq = 0;
  memset(&s_toradio, 0, sizeof(s_toradio));
  memset(&s_cached_status, 0, sizeof(s_cached_status));
  s_log_head = 0;
  s_log_tail = 0;
  s_want_ble_active = false;
  s_want_wifi_active = false;
  s_ble_active_on_c5 = false;
  s_wifi_active_on_c5 = false;
  s_retry_ticks_remaining = BRIDGE_RETRY_TICKS;

  s_status_mutex = xSemaphoreCreateMutex();
  if (s_status_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create status mutex");
    return ESP_ERR_NO_MEM;
  }

  spi_bridge_register_stream_cb(SPI_ID_MESH_TORADIO_STREAM, on_toradio_stream);

  s_is_running = true;
  BaseType_t ok = xTaskCreatePinnedToCore(notify_task,
                                          "mesh_bridge",
                                          BRIDGE_NOTIFY_TASK_STACK,
                                          NULL,
                                          BRIDGE_NOTIFY_TASK_PRIO,
                                          &s_notify_task,
                                          SYS_CORE_RADIO);
  if (ok != pdPASS) {
    s_is_running = false;
    spi_bridge_unregister_stream_cb(SPI_ID_MESH_TORADIO_STREAM);
    vSemaphoreDelete(s_status_mutex);
    s_status_mutex = NULL;
    ESP_LOGE(TAG, "Failed to create notify task");
    return ESP_ERR_NO_MEM;
  }

  s_prev_vprintf = esp_log_set_vprintf(log_vprintf);

  s_is_initialized = true;
  ESP_LOGI(TAG, "Initialized — node 0x%08lX", (unsigned long)node_num);
  return ESP_OK;
}

void meshtastic_phone_bridge_stop(void) {
  if (!s_is_initialized) {
    return;
  }

  if (s_prev_vprintf != NULL) {
    esp_log_set_vprintf(s_prev_vprintf);
    s_prev_vprintf = NULL;
  }

  s_is_running = false;
  spi_bridge_unregister_stream_cb(SPI_ID_MESH_TORADIO_STREAM);
  s_is_initialized = false;
}

esp_err_t meshtastic_phone_bridge_ble_start(void) {
  s_want_ble_active = true;
  return request_ble_init();
}

esp_err_t meshtastic_phone_bridge_ble_stop(void) {
  s_want_ble_active = false;
  return request_ble_stop();
}

esp_err_t meshtastic_phone_bridge_wifi_start(void) {
  s_want_wifi_active = true;
  return request_wifi_init();
}

esp_err_t meshtastic_phone_bridge_wifi_stop(void) {
  s_want_wifi_active = false;
  return request_wifi_stop();
}

bool meshtastic_phone_bridge_is_connected(void) {
  bool is_connected = false;
  if (s_status_mutex == NULL) {
    return false;
  }
  if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(BRIDGE_NOTIFY_TICK_MS)) == pdTRUE) {
    is_connected = (s_cached_status.ble_connected != 0) || (s_cached_status.tcp_clients != 0);
    xSemaphoreGive(s_status_mutex);
  }
  return is_connected;
}

static void notify_task(void *pvParameters) {
  (void)pvParameters;
  uint8_t frame_buf[BRIDGE_FROMRADIO_BUF_SIZE];
  uint8_t log_buf[BRIDGE_LOG_CHUNK_BYTES];

  while (s_is_running) {
    vTaskDelay(pdMS_TO_TICKS(BRIDGE_NOTIFY_TICK_MS));

    if (s_retry_ticks_remaining == 0) {
      reconcile_transports();
      s_retry_ticks_remaining = BRIDGE_RETRY_TICKS;
    } else {
      s_retry_ticks_remaining--;
    }

    spi_mesh_status_t status = {0};
    bool has_status = (fetch_status(&status) == ESP_OK);
    if (has_status) {
      store_status(&status);
    }

    bool is_phone_active = has_status && ((status.ble_connected != 0) || (status.tcp_clients != 0));

    while (is_phone_active && phoneapi_has_data()) {
      uint16_t flen = phoneapi_poll_fromradio(frame_buf, sizeof(frame_buf));
      if (flen == 0) {
        break;
      }
      if (push_frame(SPI_ID_MESH_FROMRADIO_PUSH, &s_tx_seq, frame_buf, flen) != ESP_OK) {
        ESP_LOGW(TAG, "FromRadio push failed");
        break;
      }
    }

    bool is_log_active = has_status && (status.logradio_subscribed != 0);
    while (is_log_active) {
      uint16_t n = log_ring_pop(log_buf, sizeof(log_buf));
      if (n == 0) {
        break;
      }
      if (push_frame(SPI_ID_MESH_LOG_PUSH, &s_log_seq, log_buf, n) != ESP_OK) {
        break;
      }
    }
  }

  s_notify_task = NULL;
  vTaskDelete(NULL);
}

static esp_err_t push_frame(spi_id_t id, uint8_t *seq_counter, const uint8_t *frame, uint16_t len) {
  if (frame == NULL || seq_counter == NULL || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t total_u16 = (uint16_t)((len + BRIDGE_CHUNK_PAYLOAD_MAX - 1) / BRIDGE_CHUNK_PAYLOAD_MAX);
  if (total_u16 > BRIDGE_MAX_CHUNKS) {
    return ESP_ERR_INVALID_SIZE;
  }
  uint8_t total_chunks = (uint8_t)total_u16;
  uint8_t seq = (*seq_counter)++;

  uint8_t buf[SPI_MAX_PAYLOAD];
  spi_mesh_chunk_hdr_t hdr;
  uint16_t offset = 0;

  for (uint8_t idx = 0; idx < total_chunks; idx++) {
    uint16_t remaining = (uint16_t)(len - offset);
    uint16_t this_chunk =
        remaining > BRIDGE_CHUNK_PAYLOAD_MAX ? BRIDGE_CHUNK_PAYLOAD_MAX : remaining;

    hdr.seq = seq;
    hdr.chunk_idx = idx;
    hdr.total_chunks = total_chunks;
    hdr.flags = (idx == (uint8_t)(total_chunks - 1)) ? SPI_MESH_CHUNK_FLAG_LAST : 0;

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), frame + offset, this_chunk);

    uint8_t cmd_len = (uint8_t)(sizeof(hdr) + this_chunk);
    esp_err_t ret = spi_bridge_send_command(id, buf, cmd_len, NULL, NULL, 0, BRIDGE_SPI_TIMEOUT_MS);
    if (ret != ESP_OK) {
      return ret;
    }
    offset += this_chunk;
  }
  return ESP_OK;
}

static void on_toradio_stream(spi_id_t id, const uint8_t *payload, uint8_t len) {
  (void)id;
  if (payload == NULL || len < (uint8_t)sizeof(spi_mesh_chunk_hdr_t)) {
    return;
  }

  spi_mesh_chunk_hdr_t hdr;
  memcpy(&hdr, payload, sizeof(hdr));
  const uint8_t *data = payload + sizeof(hdr);
  uint8_t data_len = (uint8_t)(len - sizeof(hdr));

  if (hdr.total_chunks == 0) {
    return;
  }

  if (hdr.chunk_idx == 0) {
    s_toradio.is_active = true;
    s_toradio.seq = hdr.seq;
    s_toradio.total_chunks = hdr.total_chunks;
    s_toradio.next_chunk_idx = 0;
    s_toradio.accumulated_len = 0;
  } else if (!s_toradio.is_active) {
    return;
  }

  if (hdr.seq != s_toradio.seq || hdr.chunk_idx != s_toradio.next_chunk_idx ||
      hdr.total_chunks != s_toradio.total_chunks) {
    s_toradio.is_active = false;
    return;
  }

  if ((uint16_t)(s_toradio.accumulated_len + data_len) > sizeof(s_toradio.buf)) {
    s_toradio.is_active = false;
    return;
  }

  memcpy(s_toradio.buf + s_toradio.accumulated_len, data, data_len);
  s_toradio.accumulated_len = (uint16_t)(s_toradio.accumulated_len + data_len);
  s_toradio.next_chunk_idx++;

  if (s_toradio.next_chunk_idx >= s_toradio.total_chunks) {
    phoneapi_on_toradio(s_toradio.buf, s_toradio.accumulated_len);
    s_toradio.is_active = false;
  }
}

static int log_vprintf(const char *fmt, va_list args) {
  char line[BRIDGE_LOG_LINE_MAX];
  va_list copy;
  va_copy(copy, args);
  int len = vsnprintf(line, sizeof(line), fmt, copy);
  va_end(copy);

  if (len > 0) {
    if (len >= (int)sizeof(line)) {
      len = (int)sizeof(line) - 1;
    }
    log_ring_push((const uint8_t *)line, (size_t)len);
  }

  if (s_prev_vprintf != NULL) {
    return s_prev_vprintf(fmt, args);
  }
  return len;
}

static void log_ring_push(const uint8_t *buf, size_t len) {
  portENTER_CRITICAL(&s_log_mux);
  for (size_t i = 0; i < len; i++) {
    uint16_t next = (uint16_t)((s_log_tail + 1) % BRIDGE_LOG_RING_SIZE);
    if (next == s_log_head) {
      s_log_head = (uint16_t)((s_log_head + 1) % BRIDGE_LOG_RING_SIZE);
    }
    s_log_ring[s_log_tail] = buf[i];
    s_log_tail = next;
  }
  portEXIT_CRITICAL(&s_log_mux);
}

static uint16_t log_ring_pop(uint8_t *out_buf, uint16_t max_len) {
  uint16_t n = 0;
  portENTER_CRITICAL(&s_log_mux);
  while (n < max_len && s_log_head != s_log_tail) {
    out_buf[n++] = s_log_ring[s_log_head];
    s_log_head = (uint16_t)((s_log_head + 1) % BRIDGE_LOG_RING_SIZE);
  }
  portEXIT_CRITICAL(&s_log_mux);
  return n;
}

static esp_err_t fetch_status(spi_mesh_status_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  spi_header_t resp;
  return spi_bridge_send_command(SPI_ID_MESH_STATUS,
                                 NULL,
                                 0,
                                 &resp,
                                 (uint8_t *)out_status,
                                 sizeof(*out_status),
                                 BRIDGE_SPI_TIMEOUT_MS);
}

static void store_status(const spi_mesh_status_t *status) {
  if (status == NULL || s_status_mutex == NULL) {
    return;
  }
  if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(BRIDGE_NOTIFY_TICK_MS)) == pdTRUE) {
    s_cached_status = *status;
    xSemaphoreGive(s_status_mutex);
  }
}

static esp_err_t request_ble_init(void) {
  spi_mesh_init_t req = {.node_num = s_node_num};
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_MESH_BLE_INIT, (uint8_t *)&req, sizeof(req), NULL, NULL, 0, BRIDGE_SPI_TIMEOUT_MS);
  if (ret == ESP_OK) {
    s_ble_active_on_c5 = true;
  }
  return ret;
}

static esp_err_t request_ble_stop(void) {
  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_MESH_BLE_STOP, NULL, 0, NULL, NULL, 0, BRIDGE_SPI_TIMEOUT_MS);
  if (ret == ESP_OK) {
    s_ble_active_on_c5 = false;
  }
  return ret;
}

static esp_err_t request_wifi_init(void) {
  spi_mesh_init_t req = {.node_num = s_node_num};
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_MESH_WIFI_INIT, (uint8_t *)&req, sizeof(req), NULL, NULL, 0, BRIDGE_SPI_TIMEOUT_MS);
  if (ret == ESP_OK) {
    s_wifi_active_on_c5 = true;
  }
  return ret;
}

static esp_err_t request_wifi_stop(void) {
  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_MESH_WIFI_STOP, NULL, 0, NULL, NULL, 0, BRIDGE_SPI_TIMEOUT_MS);
  if (ret == ESP_OK) {
    s_wifi_active_on_c5 = false;
  }
  return ret;
}

static void reconcile_transports(void) {
  if (s_want_ble_active && !s_ble_active_on_c5) {
    request_ble_init();
  } else if (!s_want_ble_active && s_ble_active_on_c5) {
    request_ble_stop();
  }
  if (s_want_wifi_active && !s_wifi_active_on_c5) {
    request_wifi_init();
  } else if (!s_want_wifi_active && s_wifi_active_on_c5) {
    request_wifi_stop();
  }
}
