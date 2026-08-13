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

#include "meshcore_phone_bridge.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "meshcore_phoneapi.h"
#include "spi_bridge.h"

static const char *TAG = "MC_BRIDGE";

#define BRIDGE_TASK_STACK         4096
#define BRIDGE_TASK_PRIO SYS_PRIO_SERVICE_HI
#define BRIDGE_STATUS_TICK_MS     200
#define BRIDGE_SPI_TIMEOUT_MS     1000
#define BRIDGE_RX_FRAME_MAX       512
#define BRIDGE_TX_FRAME_MAX       240
#define BRIDGE_CHUNK_PAYLOAD_MAX  SPI_MESH_CHUNK_PAYLOAD_MAX
#define BRIDGE_MAX_CHUNKS         255
#define BRIDGE_RECONCILE_INTERVAL 10
#define BRIDGE_NAME_PREFIX_MAX    16

typedef struct {
  bool is_active;
  uint8_t seq;
  uint8_t total_chunks;
  uint8_t next_chunk_idx;
  uint16_t accumulated_len;
  uint8_t buf[BRIDGE_RX_FRAME_MAX];
} bridge_reassembly_t;

static bool s_is_initialized = false;
static volatile bool s_is_running = false;
static char s_name_prefix[BRIDGE_NAME_PREFIX_MAX] = "Highboy-MC";
static TaskHandle_t s_status_task = NULL;
static SemaphoreHandle_t s_status_mutex = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;
static spi_mcore_status_t s_cached_status = {0};
static uint8_t s_tx_seq = 0;
static bridge_reassembly_t s_rx = {0};
static volatile bool s_want_ble_active = false;
static bool s_ble_active_on_c5 = false;
static uint16_t s_reconcile_ticks_remaining = BRIDGE_RECONCILE_INTERVAL;
static bool s_was_connected = false;

static void status_task(void *pvParameters);
static void on_rx_stream(spi_id_t id, const uint8_t *payload, uint8_t len);
static void on_phoneapi_outbound(void *ctx, const uint8_t *data, uint16_t len);
static esp_err_t push_frame(const uint8_t *frame, uint16_t len);
static esp_err_t fetch_status(spi_mcore_status_t *out_status);
static void store_status(const spi_mcore_status_t *status);
static esp_err_t request_ble_init(void);
static esp_err_t request_ble_stop(void);
static void reconcile_transports(void);

esp_err_t meshcore_phone_bridge_init(const char *name_prefix) {
  if (s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  if (name_prefix != NULL && name_prefix[0] != 0) {
    strncpy(s_name_prefix, name_prefix, sizeof(s_name_prefix) - 1);
    s_name_prefix[sizeof(s_name_prefix) - 1] = 0;
  }

  memset(&s_rx, 0, sizeof(s_rx));
  memset(&s_cached_status, 0, sizeof(s_cached_status));
  s_tx_seq = 0;
  s_want_ble_active = false;
  s_ble_active_on_c5 = false;
  s_reconcile_ticks_remaining = BRIDGE_RECONCILE_INTERVAL;
  s_was_connected = false;

  s_status_mutex = xSemaphoreCreateMutex();
  if (s_status_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create status mutex");
    return ESP_ERR_NO_MEM;
  }
  s_tx_mutex = xSemaphoreCreateMutex();
  if (s_tx_mutex == NULL) {
    vSemaphoreDelete(s_status_mutex);
    s_status_mutex = NULL;
    ESP_LOGE(TAG, "Failed to create tx mutex");
    return ESP_ERR_NO_MEM;
  }

  spi_bridge_register_stream_cb(SPI_ID_MCORE_RX_STREAM, on_rx_stream);
  meshcore_phoneapi_set_outbound(on_phoneapi_outbound, NULL);

  s_is_running = true;
  BaseType_t ok = xTaskCreatePinnedToCore(
      status_task, "mc_bridge", BRIDGE_TASK_STACK, NULL, BRIDGE_TASK_PRIO, &s_status_task,
      SYS_CORE_RADIO);
  if (ok != pdPASS) {
    s_is_running = false;
    meshcore_phoneapi_set_outbound(NULL, NULL);
    spi_bridge_unregister_stream_cb(SPI_ID_MCORE_RX_STREAM);
    vSemaphoreDelete(s_tx_mutex);
    vSemaphoreDelete(s_status_mutex);
    s_tx_mutex = NULL;
    s_status_mutex = NULL;
    ESP_LOGE(TAG, "Failed to create status task");
    return ESP_ERR_NO_MEM;
  }

  s_is_initialized = true;
  ESP_LOGI(TAG, "Initialized — prefix='%s'", s_name_prefix);
  return ESP_OK;
}

void meshcore_phone_bridge_stop(void) {
  if (!s_is_initialized) {
    return;
  }
  s_is_running = false;
  meshcore_phoneapi_set_outbound(NULL, NULL);
  spi_bridge_unregister_stream_cb(SPI_ID_MCORE_RX_STREAM);
  s_is_initialized = false;
}

esp_err_t meshcore_phone_bridge_ble_start(void) {
  s_want_ble_active = true;
  return request_ble_init();
}

esp_err_t meshcore_phone_bridge_ble_stop(void) {
  s_want_ble_active = false;
  return request_ble_stop();
}

bool meshcore_phone_bridge_is_connected(void) {
  bool is_connected = false;
  if (s_status_mutex == NULL) {
    return false;
  }
  if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(BRIDGE_STATUS_TICK_MS)) == pdTRUE) {
    is_connected = (s_cached_status.ble_connected != 0);
    xSemaphoreGive(s_status_mutex);
  }
  return is_connected;
}

static void status_task(void *pvParameters) {
  (void)pvParameters;

  while (s_is_running) {
    vTaskDelay(pdMS_TO_TICKS(BRIDGE_STATUS_TICK_MS));

    if (s_reconcile_ticks_remaining == 0) {
      reconcile_transports();
      s_reconcile_ticks_remaining = BRIDGE_RECONCILE_INTERVAL;
    } else {
      s_reconcile_ticks_remaining--;
    }

    spi_mcore_status_t status = {0};
    if (fetch_status(&status) == ESP_OK) {
      store_status(&status);
      bool is_connected = (status.ble_connected != 0);
      if (s_was_connected && !is_connected) {
        meshcore_phoneapi_on_disconnect();
      }
      s_was_connected = is_connected;
    }
  }

  s_status_task = NULL;
  vTaskDelete(NULL);
}

static void on_phoneapi_outbound(void *ctx, const uint8_t *data, uint16_t len) {
  (void)ctx;
  if (data == NULL || len == 0) {
    return;
  }
  if (!s_was_connected) {
    return;
  }
  if (push_frame(data, len) != ESP_OK) {
    ESP_LOGW(TAG, "TX push failed (%u bytes)", len);
  }
}

static esp_err_t push_frame(const uint8_t *frame, uint16_t len) {
  if (frame == NULL || len == 0 || len > BRIDGE_TX_FRAME_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t total_u16 = (uint16_t)((len + BRIDGE_CHUNK_PAYLOAD_MAX - 1) / BRIDGE_CHUNK_PAYLOAD_MAX);
  if (total_u16 == 0 || total_u16 > BRIDGE_MAX_CHUNKS) {
    return ESP_ERR_INVALID_SIZE;
  }
  uint8_t total_chunks = (uint8_t)total_u16;

  if (s_tx_mutex == NULL ||
      xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(BRIDGE_SPI_TIMEOUT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  uint8_t seq = s_tx_seq++;
  xSemaphoreGive(s_tx_mutex);

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
    esp_err_t ret = spi_bridge_send_command(
        SPI_ID_MCORE_TX_PUSH, buf, cmd_len, NULL, NULL, BRIDGE_SPI_TIMEOUT_MS);
    if (ret != ESP_OK) {
      return ret;
    }
    offset += this_chunk;
  }
  return ESP_OK;
}

static void on_rx_stream(spi_id_t id, const uint8_t *payload, uint8_t len) {
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
    s_rx.is_active = true;
    s_rx.seq = hdr.seq;
    s_rx.total_chunks = hdr.total_chunks;
    s_rx.next_chunk_idx = 0;
    s_rx.accumulated_len = 0;
  } else if (!s_rx.is_active) {
    return;
  }

  if (hdr.seq != s_rx.seq || hdr.chunk_idx != s_rx.next_chunk_idx ||
      hdr.total_chunks != s_rx.total_chunks) {
    s_rx.is_active = false;
    return;
  }

  if ((uint16_t)(s_rx.accumulated_len + data_len) > sizeof(s_rx.buf)) {
    s_rx.is_active = false;
    return;
  }

  memcpy(s_rx.buf + s_rx.accumulated_len, data, data_len);
  s_rx.accumulated_len = (uint16_t)(s_rx.accumulated_len + data_len);
  s_rx.next_chunk_idx++;

  if (s_rx.next_chunk_idx >= s_rx.total_chunks) {
    meshcore_phoneapi_on_inbound(s_rx.buf, s_rx.accumulated_len);
    s_rx.is_active = false;
  }
}

static esp_err_t fetch_status(spi_mcore_status_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  spi_header_t resp;
  return spi_bridge_send_command(
      SPI_ID_MCORE_STATUS, NULL, 0, &resp, (uint8_t *)out_status, BRIDGE_SPI_TIMEOUT_MS);
}

static void store_status(const spi_mcore_status_t *status) {
  if (status == NULL || s_status_mutex == NULL) {
    return;
  }
  if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(BRIDGE_STATUS_TICK_MS)) == pdTRUE) {
    s_cached_status = *status;
    xSemaphoreGive(s_status_mutex);
  }
}

static esp_err_t request_ble_init(void) {
  spi_mcore_init_t req = {0};
  strncpy(req.name_prefix, s_name_prefix, sizeof(req.name_prefix) - 1);
  req.pin = meshcore_phoneapi_get_pin();
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_MCORE_BLE_INIT, (uint8_t *)&req, sizeof(req), NULL, NULL, BRIDGE_SPI_TIMEOUT_MS);
  if (ret == ESP_OK) {
    s_ble_active_on_c5 = true;
  }
  return ret;
}

static esp_err_t request_ble_stop(void) {
  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_MCORE_BLE_STOP, NULL, 0, NULL, NULL, BRIDGE_SPI_TIMEOUT_MS);
  if (ret == ESP_OK) {
    s_ble_active_on_c5 = false;
  }
  return ret;
}

static void reconcile_transports(void) {
  if (s_want_ble_active && !s_ble_active_on_c5) {
    request_ble_init();
  } else if (!s_want_ble_active && s_ble_active_on_c5) {
    request_ble_stop();
  }
}
