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

#include "host_transport.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host_link_gatt.h"
#include "spi_bridge.h"

static const char *TAG = "HOST_TRANSPORT";

#define TRANSPORT_TX_FRAME_MAX     4096
#define TRANSPORT_CHUNK_HDR_SIZE   sizeof(spi_mesh_chunk_hdr_t)
#define TRANSPORT_MUTEX_TIMEOUT_MS 50

typedef struct {
  bool is_active;
  uint8_t seq;
  uint8_t total_chunks;
  uint8_t next_chunk_idx;
  uint16_t accumulated_len;
  uint8_t buf[TRANSPORT_TX_FRAME_MAX];
} host_reassembly_t;

static bool s_is_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static host_reassembly_t s_tx_in = {0};
static uint8_t s_rx_seq = 0;

static bool take_mutex(void);
static void give_mutex(void);

esp_err_t host_transport_init(void) {
  if (s_is_initialized) {
    return ESP_OK;
  }
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create transport mutex");
    return ESP_ERR_NO_MEM;
  }
  memset(&s_tx_in, 0, sizeof(s_tx_in));
  s_rx_seq = 0;
  s_is_initialized = true;
  spi_bridge_stream_enable(SPI_ID_HOST_RX, true);
  ESP_LOGI(TAG, "Transport initialized");
  return ESP_OK;
}

void host_transport_inject_tx_chunk(const uint8_t *payload, uint8_t len) {
  if (payload == NULL || len < (uint8_t)TRANSPORT_CHUNK_HDR_SIZE) {
    return;
  }
  if (!take_mutex()) {
    return;
  }

  spi_mesh_chunk_hdr_t hdr;
  memcpy(&hdr, payload, sizeof(hdr));
  const uint8_t *data = payload + sizeof(hdr);
  uint8_t data_len = (uint8_t)(len - sizeof(hdr));

  if (hdr.total_chunks == 0) {
    give_mutex();
    return;
  }

  if (hdr.chunk_idx == 0) {
    s_tx_in.is_active = true;
    s_tx_in.seq = hdr.seq;
    s_tx_in.total_chunks = hdr.total_chunks;
    s_tx_in.next_chunk_idx = 0;
    s_tx_in.accumulated_len = 0;
  } else if (!s_tx_in.is_active) {
    give_mutex();
    return;
  }

  if (hdr.seq != s_tx_in.seq || hdr.chunk_idx != s_tx_in.next_chunk_idx ||
      hdr.total_chunks != s_tx_in.total_chunks) {
    s_tx_in.is_active = false;
    give_mutex();
    return;
  }

  if ((uint16_t)(s_tx_in.accumulated_len + data_len) > sizeof(s_tx_in.buf)) {
    s_tx_in.is_active = false;
    give_mutex();
    return;
  }

  memcpy(s_tx_in.buf + s_tx_in.accumulated_len, data, data_len);
  s_tx_in.accumulated_len = (uint16_t)(s_tx_in.accumulated_len + data_len);
  s_tx_in.next_chunk_idx++;

  if (s_tx_in.next_chunk_idx >= s_tx_in.total_chunks) {
    static uint8_t snapshot[TRANSPORT_TX_FRAME_MAX];
    uint16_t snapshot_len = s_tx_in.accumulated_len;
    memcpy(snapshot, s_tx_in.buf, snapshot_len);
    s_tx_in.is_active = false;
    give_mutex();

    host_link_gatt_notify(snapshot, snapshot_len);
    return;
  }

  give_mutex();
}

bool host_transport_send_to_p4(const uint8_t *frame, uint16_t len) {
  if (frame == NULL || len == 0) {
    return false;
  }
  if (!spi_bridge_stream_is_enabled(SPI_ID_HOST_RX)) {
    return false;
  }

  uint16_t total_u16 =
      (uint16_t)((len + SPI_MESH_CHUNK_PAYLOAD_MAX - 1) / SPI_MESH_CHUNK_PAYLOAD_MAX);
  if (total_u16 == 0 || total_u16 > 255) {
    return false;
  }
  uint8_t total_chunks = (uint8_t)total_u16;
  uint8_t seq;
  if (take_mutex()) {
    seq = s_rx_seq++;
    give_mutex();
  } else {
    return false;
  }

  uint8_t buf[SPI_MESH_PAYLOAD_LIMIT];
  spi_mesh_chunk_hdr_t hdr;
  uint16_t offset = 0;

  for (uint8_t idx = 0; idx < total_chunks; idx++) {
    uint16_t remaining = (uint16_t)(len - offset);
    uint16_t this_chunk =
        remaining > SPI_MESH_CHUNK_PAYLOAD_MAX ? SPI_MESH_CHUNK_PAYLOAD_MAX : remaining;

    hdr.seq = seq;
    hdr.chunk_idx = idx;
    hdr.total_chunks = total_chunks;
    hdr.flags = (idx == (uint8_t)(total_chunks - 1)) ? SPI_MESH_CHUNK_FLAG_LAST : 0;

    memcpy(buf, &hdr, sizeof(hdr));
    memcpy(buf + sizeof(hdr), frame + offset, this_chunk);

    uint8_t push_len = (uint8_t)(sizeof(hdr) + this_chunk);
    if (!spi_bridge_stream_push(SPI_ID_HOST_RX, buf, push_len)) {
      return false;
    }
    offset += this_chunk;
  }
  return true;
}

void host_transport_get_status(spi_host_status_t *out_status) {
  if (out_status == NULL) {
    return;
  }
  out_status->ble_connected = host_link_gatt_is_connected() ? 1 : 0;
  out_status->ble_subscribed = host_link_gatt_is_subscribed() ? 1 : 0;
  out_status->reserved[0] = 0;
  out_status->reserved[1] = 0;
}

void host_transport_reset(void) {
  if (!take_mutex()) {
    return;
  }
  memset(&s_tx_in, 0, sizeof(s_tx_in));
  s_rx_seq = 0;
  give_mutex();
}

static bool take_mutex(void) {
  if (s_mutex == NULL) {
    return false;
  }
  return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(TRANSPORT_MUTEX_TIMEOUT_MS)) == pdTRUE;
}

static void give_mutex(void) {
  if (s_mutex != NULL) {
    xSemaphoreGive(s_mutex);
  }
}
