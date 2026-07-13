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

#include "spi_bridge.h"

#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "spi_bridge_phy.h"
#include "spi_timeouts.h"

static const char *TAG = "SPI_BRIDGE_P4";

#define SPI_STREAM_TASK_STACK 16384
#define SPI_STREAM_TASK_PRIO  5
#define SPI_MUTEX_TIMEOUT_MS  50
#define SPI_STREAM_POLL_MS    10
#define SPI_STREAM_IDLE_MS    20
#define SPI_STREAM_YIELD_MS   1
#define SPI_IRQ_WAIT_MS       100
#define SPI_STREAM_CB_SLOTS   4

// POLL-mode handshake: re-clock the bus until the slave answers. The first few
// tries spin tight (low latency for fast commands); after that we yield so a
// long slave op (e.g. a ~27 s WiFi scan) does not hog the CPU.
#define SPI_POLL_FAST_TRIES 32
#define SPI_POLL_FAST_US    150
#define SPI_POLL_SLOW_MS    2

typedef struct {
  spi_id_t id;
  spi_stream_cb_t cb;
} stream_cb_slot_t;

static SemaphoreHandle_t s_spi_mutex = NULL;
static TaskHandle_t s_stream_task_handle = NULL;
static volatile bool s_is_command_in_flight = false;
static volatile bool s_bridge_alive = true;
static stream_cb_slot_t s_stream_cbs[SPI_STREAM_CB_SLOTS] = {0};
static spi_bridge_mode_t s_bridge_mode = SPI_BRIDGE_MODE_IRQ;

static void stream_task(void *arg);
static esp_err_t fetch_stream(const uint8_t **out_records, uint16_t *out_batch_len);
static esp_err_t recv_frame(uint8_t *tx_buf, uint8_t *rx_buf, size_t frame_size, uint16_t expect_cmd,
                            bool match_cmd, uint32_t timeout_ms);
static spi_stream_cb_t get_stream_cb(spi_id_t id);
static bool has_any_stream_cb(void);

static esp_err_t status_to_err(spi_status_t status) {
  switch (status) {
    case SPI_STATUS_OK:
      return ESP_OK;
    case SPI_STATUS_BUSY:
      return ESP_ERR_INVALID_STATE;
    case SPI_STATUS_UNSUPPORTED:
      return ESP_ERR_NOT_SUPPORTED;
    case SPI_STATUS_INVALID_ARG:
      return ESP_ERR_INVALID_ARG;
    case SPI_STATUS_ERROR:
    default:
      return ESP_FAIL;
  }
}

// Public functions

esp_err_t spi_bridge_master_init(void) {
  return spi_bridge_master_init_mode(SPI_BRIDGE_MODE_IRQ);
}

esp_err_t spi_bridge_master_init_mode(spi_bridge_mode_t mode) {
  s_bridge_mode = mode;
  if (s_spi_mutex == NULL) {
    s_spi_mutex = xSemaphoreCreateMutex();
  }
  return spi_bridge_phy_init_ex(mode == SPI_BRIDGE_MODE_IRQ);
}

// Read one response/stream frame from the slave into rx_buf.
//   IRQ mode:  wait for the C5's IRQ pulse, then clock a single frame.
//   POLL mode: re-clock the bus until a frame with a valid sync byte (and, when
//              match_cmd, the expected category/op) comes back. While the slave
//              is still busy it has no TX armed and reads back as junk, so we
//              retry until it answers or the timeout elapses.
// tx_buf is used as the (zeroed) outgoing dummy; frame_size is the transfer size.
static esp_err_t recv_frame(uint8_t *tx_buf, uint8_t *rx_buf, size_t frame_size, uint16_t expect_cmd,
                            bool match_cmd, uint32_t timeout_ms) {
  memset(tx_buf, 0, frame_size);

  if (s_bridge_mode == SPI_BRIDGE_MODE_IRQ) {
    esp_err_t ret = spi_bridge_phy_wait_irq(timeout_ms);
    if (ret != ESP_OK) {
      return ret;
    }
    memset(rx_buf, 0, frame_size);
    return spi_bridge_phy_transmit(tx_buf, rx_buf, frame_size);
  }

  // POLL mode.
  int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
  uint32_t tries = 0;
  do {
    memset(rx_buf, 0, frame_size);
    esp_err_t ret = spi_bridge_phy_transmit(tx_buf, rx_buf, frame_size);
    if (ret != ESP_OK) {
      return ret;
    }
    const spi_header_t *resp = (const spi_header_t *)rx_buf;
    bool armed = (resp->sync == SPI_SYNC_BYTE) &&
                 (resp->type == SPI_TYPE_RESP || resp->type == SPI_TYPE_STREAM);
    if (armed && (!match_cmd || spi_header_cmd(resp) == expect_cmd)) {
      return ESP_OK;
    }
    if (tries++ < SPI_POLL_FAST_TRIES) {
      esp_rom_delay_us(SPI_POLL_FAST_US);
    } else {
      vTaskDelay(pdMS_TO_TICKS(SPI_POLL_SLOW_MS));
    }
  } while (esp_timer_get_time() < deadline);

  return ESP_ERR_TIMEOUT;
}

void spi_bridge_register_stream_cb(spi_id_t id, spi_stream_cb_t cb) {
  if (cb == NULL) {
    spi_bridge_unregister_stream_cb(id);
    return;
  }

  int free_slot = -1;
  for (int i = 0; i < SPI_STREAM_CB_SLOTS; i++) {
    if (s_stream_cbs[i].cb != NULL && s_stream_cbs[i].id == id) {
      s_stream_cbs[i].cb = cb;
      return;
    }
    if (s_stream_cbs[i].cb == NULL && free_slot < 0) {
      free_slot = i;
    }
  }
  if (free_slot < 0) {
    ESP_LOGE(TAG, "No free stream cb slot for id 0x%04X", id);
    return;
  }
  s_stream_cbs[free_slot].id = id;
  s_stream_cbs[free_slot].cb = cb;

  if (s_stream_task_handle == NULL) {
    if (s_spi_mutex == NULL) {
      s_spi_mutex = xSemaphoreCreateMutex();
    }
    BaseType_t created = xTaskCreate(stream_task,
                                     "spi_stream",
                                     SPI_STREAM_TASK_STACK,
                                     NULL,
                                     SPI_STREAM_TASK_PRIO,
                                     &s_stream_task_handle);
    if (created != pdPASS) {
      s_stream_task_handle = NULL;
      ESP_LOGE(TAG, "Failed to create SPI stream task");
    }
  }
}

void spi_bridge_unregister_stream_cb(spi_id_t id) {
  for (int i = 0; i < SPI_STREAM_CB_SLOTS; i++) {
    if (s_stream_cbs[i].cb != NULL && s_stream_cbs[i].id == id) {
      s_stream_cbs[i].cb = NULL;
      s_stream_cbs[i].id = 0;
      return;
    }
  }
}

void spi_bridge_set_alive(bool alive) {
  s_bridge_alive = alive;
}

bool spi_bridge_is_alive(void) {
  return s_bridge_alive;
}

uint32_t spi_bridge_get_timeout(spi_id_t id) {
  if (id >= SPI_ID_WIFI_SCAN && id <= SPI_ID_WIFI_APP_PROBE_MON) {
    return SPI_TIMEOUT_WIFI_MS;
  }
  return SPI_TIMEOUT_DEFAULT_MS;
}

esp_err_t spi_bridge_send_command(spi_id_t id,
                                  const uint8_t *payload,
                                  uint8_t len,
                                  spi_header_t *out_header,
                                  uint8_t *out_payload,
                                  uint32_t timeout_ms) {
  if (!s_bridge_alive) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_spi_mutex == NULL) {
    s_spi_mutex = xSemaphoreCreateMutex();
  }
  if (xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  s_is_command_in_flight = true;

  spi_header_t header = {.sync = SPI_SYNC_BYTE,
                         .type = SPI_TYPE_CMD,
                         .category = SPI_CMD_CAT(id),
                         .op = SPI_CMD_OP(id),
                         .length = len};

  if (len > SPI_MAX_PAYLOAD) {
    s_is_command_in_flight = false;
    xSemaphoreGive(s_spi_mutex);
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t tx_buf[SPI_FRAME_SIZE];
  uint8_t rx_buf[SPI_FRAME_SIZE];
  memset(tx_buf, 0, sizeof(tx_buf));
  memcpy(tx_buf, &header, sizeof(header));
  if (payload != NULL && len > 0) {
    memcpy(tx_buf + sizeof(header), payload, len);
  }

  esp_err_t ret = spi_bridge_phy_transmit(tx_buf, NULL, SPI_FRAME_SIZE);
  if (ret != ESP_OK) {
    s_is_command_in_flight = false;
    xSemaphoreGive(s_spi_mutex);
    return ret;
  }

  ret = recv_frame(tx_buf, rx_buf, SPI_FRAME_SIZE, id, true, timeout_ms);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Command 0x%04X timeout", id);
    s_is_command_in_flight = false;
    xSemaphoreGive(s_spi_mutex);
    return ret;
  }

  spi_header_t *resp = (spi_header_t *)rx_buf;

  if (resp->sync != SPI_SYNC_BYTE) {
    ESP_LOGE(TAG, "Sync error: 0x%02X", resp->sync);
    s_is_command_in_flight = false;
    xSemaphoreGive(s_spi_mutex);
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (resp->type != SPI_TYPE_RESP) {
    ESP_LOGE(TAG, "Invalid response type: 0x%02X", resp->type);
    s_is_command_in_flight = false;
    xSemaphoreGive(s_spi_mutex);
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (spi_header_cmd(resp) != id) {
    // A previous command timed out (e.g. a long scan) and its response arrived
    // late, landing on this read. Reject it rather than handing the caller the
    // wrong data; the slave has already re-armed its RX, so the next command
    // resyncs on its own.
    ESP_LOGW(TAG, "Response ID mismatch (req 0x%04X, resp 0x%04X), dropping", id,
             spi_header_cmd(resp));
    s_is_command_in_flight = false;
    xSemaphoreGive(s_spi_mutex);
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (resp->length > SPI_MAX_PAYLOAD) {
    ESP_LOGE(TAG, "Invalid response length: %u", resp->length);
    s_is_command_in_flight = false;
    xSemaphoreGive(s_spi_mutex);
    return ESP_ERR_INVALID_RESPONSE;
  }

  spi_status_t status = SPI_STATUS_ERROR;
  uint8_t data_len = 0;
  if (resp->length >= SPI_RESP_STATUS_SIZE) {
    status = (spi_status_t)rx_buf[sizeof(spi_header_t)];
    data_len = (uint8_t)(resp->length - SPI_RESP_STATUS_SIZE);
  }

  if (out_header != NULL) {
    *out_header = *resp;
    out_header->length = data_len;
  }

  if (data_len > 0 && out_payload != NULL) {
    memcpy(out_payload, rx_buf + sizeof(spi_header_t) + SPI_RESP_STATUS_SIZE, data_len);
  }

  esp_err_t out = status_to_err(status);
  s_is_command_in_flight = false;
  xSemaphoreGive(s_spi_mutex);
  return out;
}

// Static functions

static spi_stream_cb_t get_stream_cb(spi_id_t id) {
  for (int i = 0; i < SPI_STREAM_CB_SLOTS; i++) {
    if (s_stream_cbs[i].cb != NULL && s_stream_cbs[i].id == id) {
      return s_stream_cbs[i].cb;
    }
  }
  return NULL;
}

static bool has_any_stream_cb(void) {
  for (int i = 0; i < SPI_STREAM_CB_SLOTS; i++) {
    if (s_stream_cbs[i].cb != NULL) {
      return true;
    }
  }
  return false;
}

// Fetches one batched stream frame. On ESP_OK, *out_records points into a
// static buffer holding the records region ([u16 op][u8 len][data]...) and
// *out_batch_len is its length in bytes (0 = no data pending). The returned
// pointer is valid until the next fetch_stream call (single consumer task).
static esp_err_t fetch_stream(const uint8_t **out_records, uint16_t *out_batch_len) {
  static uint8_t s_stream_tx[SPI_STREAM_FRAME_SIZE]; // stays zero; master clocks zeros out
  static uint8_t s_stream_rx[SPI_STREAM_FRAME_SIZE];

  spi_header_t header = {.sync = SPI_SYNC_BYTE,
                         .type = SPI_TYPE_CMD,
                         .category = SPI_CMD_CAT(SPI_ID_SYSTEM_STREAM),
                         .op = SPI_CMD_OP(SPI_ID_SYSTEM_STREAM),
                         .length = 0};

  uint8_t cmd_tx[SPI_FRAME_SIZE];
  memset(cmd_tx, 0, sizeof(cmd_tx));
  memcpy(cmd_tx, &header, sizeof(header));

  esp_err_t ret = spi_bridge_phy_transmit(cmd_tx, NULL, SPI_FRAME_SIZE);
  if (ret != ESP_OK)
    return ret;

  // Accept either a STREAM frame (data or empty) or a RESP (error status).
  ret = recv_frame(s_stream_tx, s_stream_rx, SPI_STREAM_FRAME_SIZE, 0, false, SPI_IRQ_WAIT_MS);
  if (ret != ESP_OK)
    return ret;

  spi_header_t *resp = (spi_header_t *)s_stream_rx;
  if (resp->sync != SPI_SYNC_BYTE)
    return ESP_ERR_INVALID_RESPONSE;

  if (resp->type == SPI_TYPE_STREAM) {
    uint16_t batch_len = (uint16_t)s_stream_rx[sizeof(spi_header_t)] |
                         ((uint16_t)s_stream_rx[sizeof(spi_header_t) + 1] << 8);
    uint16_t cap = SPI_STREAM_FRAME_SIZE - sizeof(spi_header_t) - sizeof(uint16_t);
    if (batch_len > cap)
      batch_len = cap;
    if (out_records != NULL)
      *out_records = s_stream_rx + sizeof(spi_header_t) + sizeof(uint16_t);
    if (out_batch_len != NULL)
      *out_batch_len = batch_len;
    return ESP_OK;
  }

  if (resp->type == SPI_TYPE_RESP && resp->length >= SPI_RESP_STATUS_SIZE) {
    spi_status_t status = (spi_status_t)s_stream_rx[sizeof(spi_header_t)];
    return status_to_err(status);
  }

  return ESP_ERR_INVALID_RESPONSE;
}

static void stream_task(void *arg) {
  while (1) {
    if (!has_any_stream_cb()) {
      s_stream_task_handle = NULL;
      vTaskDelete(NULL);
      return;
    }

    if (s_is_command_in_flight) {
      vTaskDelay(pdMS_TO_TICKS(SPI_STREAM_POLL_MS));
      continue;
    }

    if (xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(SPI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(SPI_STREAM_POLL_MS));
      continue;
    }

    const uint8_t *records = NULL;
    uint16_t batch_len = 0;
    esp_err_t ret = fetch_stream(&records, &batch_len);
    xSemaphoreGive(s_spi_mutex);
    if (ret != ESP_OK || batch_len == 0 || records == NULL) {
      vTaskDelay(pdMS_TO_TICKS(SPI_STREAM_IDLE_MS));
      continue;
    }

    // Unpack [u16 op][u8 len][data]... and dispatch each record to its callback,
    // exactly as if it had arrived in its own frame.
    uint16_t off = 0;
    while (off + 3u <= batch_len) {
      uint16_t op = (uint16_t)records[off] | ((uint16_t)records[off + 1] << 8);
      uint8_t rec_len = records[off + 2];
      if ((uint32_t)off + 3u + rec_len > batch_len)
        break; // truncated/malformed — stop
      spi_stream_cb_t cb = get_stream_cb(op);
      if (cb != NULL)
        cb(op, records + off + 3, rec_len);
      off += 3u + rec_len;
    }
    vTaskDelay(pdMS_TO_TICKS(SPI_STREAM_YIELD_MS));
  }
}
