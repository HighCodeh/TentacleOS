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

// BLE companion relay on the P4. BLE terminates on the C5; this module ferries
// opaque host frames over the SPI bridge and presents BLE as just another
// host-link transport. The host-link writer for BLE chunks frames to the C5
// (SPI_ID_HOST_TX); inbound app bytes arrive on the SPI_ID_HOST_RX stream,
// reassemble, and feed the host-link core (which terminates the crypto).

#include "host_link_ble.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "host_link.h"
#include "spi_bridge.h"
#include "spi_protocol.h"

static const char *TAG = "HOST_LINK_BLE";

#define BLE_TASK_STACK         4096
#define BLE_TASK_PRIO SYS_PRIO_SERVICE_HI
#define BLE_STATUS_TICK_MS     200
#define BLE_SPI_TIMEOUT_MS     1000
#define BLE_RX_FRAME_MAX       512 // one C5 BLE-write worth; host_link_feed reframes
#define BLE_CHUNK_PAYLOAD_MAX  SPI_MESH_CHUNK_PAYLOAD_MAX
#define BLE_MAX_CHUNKS         255
#define BLE_RECONCILE_INTERVAL 10
#define BLE_NAME_PREFIX        "Tentacle"

typedef struct {
  bool is_active;
  uint8_t seq;
  uint8_t total_chunks;
  uint8_t next_chunk_idx;
  uint16_t accumulated_len;
  uint8_t buf[BLE_RX_FRAME_MAX];
} ble_reassembly_t;

static bool s_is_initialized = false;
static volatile bool s_is_running = false;
static TaskHandle_t s_status_task = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;
static uint8_t s_tx_seq = 0;
static ble_reassembly_t s_rx = {0};
static volatile bool s_want_ble_active = false;
static bool s_ble_active_on_c5 = false;
static uint16_t s_reconcile_ticks_remaining = BLE_RECONCILE_INTERVAL;
static bool s_was_connected = false;

static void status_task(void *pvParameters);
static void on_rx_stream(spi_id_t id, const uint8_t *payload, uint8_t len);
static void ble_write(const uint8_t *frame, size_t len);
static esp_err_t fetch_status(spi_host_status_t *out_status);
static esp_err_t request_ble_init(void);
static esp_err_t request_ble_stop(void);
static void reconcile_transports(void);

esp_err_t host_link_ble_init(void) {
  if (s_is_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  memset(&s_rx, 0, sizeof(s_rx));
  s_tx_seq = 0;
  s_want_ble_active = false;
  s_ble_active_on_c5 = false;
  s_reconcile_ticks_remaining = BLE_RECONCILE_INTERVAL;
  s_was_connected = false;

  s_tx_mutex = xSemaphoreCreateMutex();
  if (s_tx_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create tx mutex");
    return ESP_ERR_NO_MEM;
  }

  spi_bridge_register_stream_cb(SPI_ID_HOST_RX, on_rx_stream);
  host_link_mark_ble_writer(ble_write); // lets the log-over-BLE toggle gate BLE only

  s_is_running = true;
  if (xTaskCreatePinnedToCore(status_task, "hl_ble", BLE_TASK_STACK, NULL, BLE_TASK_PRIO, &s_status_task, SYS_CORE_RADIO) !=
      pdPASS) {
    s_is_running = false;
    spi_bridge_unregister_stream_cb(SPI_ID_HOST_RX);
    vSemaphoreDelete(s_tx_mutex);
    s_tx_mutex = NULL;
    ESP_LOGE(TAG, "Failed to create status task");
    return ESP_ERR_NO_MEM;
  }

  s_is_initialized = true;
  ESP_LOGI(TAG, "BLE relay initialized");
  return ESP_OK;
}

esp_err_t host_link_ble_start(void) {
  s_want_ble_active = true;
  return request_ble_init();
}

esp_err_t host_link_ble_stop(void) {
  s_want_ble_active = false;
  return request_ble_stop();
}

bool host_link_ble_is_connected(void) {
  return s_was_connected;
}

bool host_link_ble_is_active(void) {
  return s_want_ble_active;
}

static void status_task(void *pvParameters) {
  (void)pvParameters;

  while (s_is_running) {
    vTaskDelay(pdMS_TO_TICKS(BLE_STATUS_TICK_MS));

    if (s_reconcile_ticks_remaining == 0) {
      reconcile_transports();
      s_reconcile_ticks_remaining = BLE_RECONCILE_INTERVAL;
    } else {
      s_reconcile_ticks_remaining--;
    }

    spi_host_status_t status = {0};
    if (fetch_status(&status) != ESP_OK) {
      continue;
    }

    bool is_connected = (status.ble_connected != 0);
    if (is_connected && !s_was_connected) {
      // New BLE companion: claim the single session for the BLE writer.
      if (!host_link_session_acquire(ble_write)) {
        ESP_LOGW(TAG, "BLE connect but session busy (other transport active)");
      }
    } else if (!is_connected && s_was_connected) {
      host_link_session_release(ble_write);
    }
    s_was_connected = is_connected;
  }

  s_status_task = NULL;
  vTaskDelete(NULL);
}

// Host-link writer for BLE: chunk the frame and push each chunk to the C5,
// which reassembles and notifies the companion. Runs under the host-link lock.
static void ble_write(const uint8_t *frame, size_t len) {
  if (frame == NULL || len == 0) {
    return;
  }

  uint16_t total_u16 = (uint16_t)((len + BLE_CHUNK_PAYLOAD_MAX - 1) / BLE_CHUNK_PAYLOAD_MAX);
  if (total_u16 == 0 || total_u16 > BLE_MAX_CHUNKS) {
    return;
  }
  uint8_t total_chunks = (uint8_t)total_u16;

  if (s_tx_mutex == NULL ||
      xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(BLE_SPI_TIMEOUT_MS)) != pdTRUE) {
    return;
  }
  uint8_t seq = s_tx_seq++;
  xSemaphoreGive(s_tx_mutex);

  uint8_t buf[SPI_MAX_PAYLOAD];
  spi_mesh_chunk_hdr_t hdr;
  uint16_t offset = 0;

  for (uint8_t idx = 0; idx < total_chunks; idx++) {
    uint16_t remaining = (uint16_t)(len - offset);
    uint16_t this_chunk = remaining > BLE_CHUNK_PAYLOAD_MAX ? BLE_CHUNK_PAYLOAD_MAX : remaining;

    hdr.seq = seq;
    hdr.chunk_idx = idx;
    hdr.total_chunks = total_chunks;
    hdr.flags = (idx == (uint8_t)(total_chunks - 1)) ? SPI_MESH_CHUNK_FLAG_LAST : 0;

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), frame + offset, this_chunk);

    uint8_t cmd_len = (uint8_t)(sizeof(hdr) + this_chunk);
    if (spi_bridge_send_command(SPI_ID_HOST_TX, buf, cmd_len, NULL, NULL, 0, BLE_SPI_TIMEOUT_MS) !=
        ESP_OK) {
      return;
    }
    offset += this_chunk;
  }
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
    // Claim the session for BLE on the first inbound bytes if it is free (the
    // HELLO that establishes the session must not be dropped to a connect-edge
    // race). If another transport already owns it, acquire fails and we drop.
    if (!host_link_session_owns(ble_write)) {
      host_link_session_acquire(ble_write);
    }
    if (host_link_session_owns(ble_write)) {
      host_link_feed(s_rx.buf, s_rx.accumulated_len);
    }
    s_rx.is_active = false;
  }
}

static esp_err_t fetch_status(spi_host_status_t *out_status) {
  if (out_status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  spi_header_t resp;
  return spi_bridge_send_command(
      SPI_ID_HOST_STATUS, NULL, 0, &resp, (uint8_t *)out_status, sizeof(*out_status), BLE_SPI_TIMEOUT_MS);
}

static esp_err_t request_ble_init(void) {
  spi_host_init_t req = {0};
  strncpy(req.name_prefix, BLE_NAME_PREFIX, sizeof(req.name_prefix) - 1);
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_HOST_BLE_INIT, (uint8_t *)&req, sizeof(req), NULL, NULL, 0, BLE_SPI_TIMEOUT_MS);
  if (ret == ESP_OK) {
    s_ble_active_on_c5 = true;
  }
  return ret;
}

static esp_err_t request_ble_stop(void) {
  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_HOST_BLE_STOP, NULL, 0, NULL, NULL, 0, BLE_SPI_TIMEOUT_MS);
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
