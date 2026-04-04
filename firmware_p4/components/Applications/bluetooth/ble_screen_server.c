// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ble_screen_server.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "BLE_SCREEN";

static uint8_t *compression_buffer = NULL;
static bool is_initialized = false;

#define MAX_CHUNK_SIZE 240  // SPI_MAX_PAYLOAD - header overhead

esp_err_t ble_screen_server_init(void) {
  if (is_initialized) return ESP_OK;

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_SCREEN_INIT,
      NULL, 0,
      &resp_hdr, resp_buf, 5000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    ESP_LOGE(TAG, "Failed to init screen server on C5");
    return ESP_FAIL;
  }

  compression_buffer = heap_caps_malloc(32 * 1024, MALLOC_CAP_SPIRAM);
  if (!compression_buffer) return ESP_ERR_NO_MEM;

  is_initialized = true;
  ESP_LOGI(TAG, "Screen Stream Service initialized");
  return ESP_OK;
}

void ble_screen_server_deinit(void) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  spi_bridge_send_command(SPI_ID_BT_SCREEN_DEINIT,
      NULL, 0,
      &resp_hdr, resp_buf, 2000);

  if (compression_buffer) {
    heap_caps_free(compression_buffer);
    compression_buffer = NULL;
  }
  is_initialized = false;
}

void ble_screen_server_send_partial(const uint16_t *px_map, int x, int y, int w, int h) {
  if (!is_initialized || !compression_buffer || px_map == NULL) return;

  size_t comp_idx = 0;

  // Header: [Magic: 0xFE] [X:2] [Y:2] [W:2] [H:2]
  compression_buffer[comp_idx++] = 0xFE;
  compression_buffer[comp_idx++] = x & 0xFF; compression_buffer[comp_idx++] = (x >> 8) & 0xFF;
  compression_buffer[comp_idx++] = y & 0xFF; compression_buffer[comp_idx++] = (y >> 8) & 0xFF;
  compression_buffer[comp_idx++] = w & 0xFF; compression_buffer[comp_idx++] = (w >> 8) & 0xFF;
  compression_buffer[comp_idx++] = h & 0xFF; compression_buffer[comp_idx++] = (h >> 8) & 0xFF;

  // RLE Compression
  uint16_t current_pixel = px_map[0];
  uint8_t run_count = 0;
  size_t total_pixels = w * h;

  for (size_t i = 0; i < total_pixels; i++) {
    uint16_t px = px_map[i];

    if (px == current_pixel && run_count < 255) {
      run_count++;
    } else {
      compression_buffer[comp_idx++] = run_count;
      compression_buffer[comp_idx++] = current_pixel & 0xFF;
      compression_buffer[comp_idx++] = (current_pixel >> 8) & 0xFF;

      current_pixel = px;
      run_count = 1;
    }

    if (comp_idx > 32000) break;
  }

  if (run_count > 0 && comp_idx < 32765) {
    compression_buffer[comp_idx++] = run_count;
    compression_buffer[comp_idx++] = current_pixel & 0xFF;
    compression_buffer[comp_idx++] = (current_pixel >> 8) & 0xFF;
  }

  // Send compressed data in chunks via SPI
  size_t total_len = comp_idx;
  size_t sent = 0;

  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  while (sent < total_len) {
    size_t chunk_len = total_len - sent;
    if (chunk_len > MAX_CHUNK_SIZE) chunk_len = MAX_CHUNK_SIZE;

    spi_bridge_send_command(SPI_ID_BT_SCREEN_SEND_PARTIAL,
        compression_buffer + sent, chunk_len,
        &resp_hdr, resp_buf, 1000);

    sent += chunk_len;
  }
}

bool ble_screen_server_is_active(void) {
  spi_header_t resp_hdr;
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(SPI_ID_BT_SCREEN_IS_ACTIVE,
      NULL, 0,
      &resp_hdr, resp_buf, 2000);

  if (ret != ESP_OK || resp_buf[0] != SPI_STATUS_OK) {
    return false;
  }

  return resp_buf[1];
}
