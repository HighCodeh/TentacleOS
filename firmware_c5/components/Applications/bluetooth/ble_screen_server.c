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
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE_SCREEN";

static const ble_uuid128_t gatt_svc_screen_uuid =
  BLE_UUID128_INIT(0x00, 0x00, 0x64, 0x6c, 0x6f, 0x00, 0x41, 0x89,
                   0xc0, 0x46, 0x72, 0x69, 0xb6, 0x56, 0x5a, 0x59);

static const ble_uuid128_t gatt_chr_stream_uuid =
  BLE_UUID128_INIT(0x01, 0x00, 0x64, 0x6c, 0x6f, 0x00, 0x41, 0x89,
                   0xc0, 0x46, 0x72, 0x69, 0xb6, 0x56, 0x5a, 0x59);

static uint16_t screen_chr_val_handle;
static uint8_t *compression_buffer = NULL;
static bool is_initialized = false;

#define MAX_CHUNK_SIZE 480 // MTU Optimized

static int screen_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
  return 0;
}

static const struct ble_gatt_svc_def screen_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &gatt_svc_screen_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]) {
      {
        .uuid = &gatt_chr_stream_uuid.u,
        .access_cb = screen_access_cb,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &screen_chr_val_handle,
      },
      { 0 }
    },
  },
  { 0 }
};

esp_err_t ble_screen_server_init(void) {
  if (is_initialized) return ESP_OK;

  int rc = ble_gatts_count_cfg(screen_svcs);
  if (rc != 0) return rc;

  rc = ble_gatts_add_svcs(screen_svcs);
  if (rc != 0) return rc;

  compression_buffer = heap_caps_malloc(32 * 1024, MALLOC_CAP_SPIRAM);
  if (!compression_buffer) return ESP_ERR_NO_MEM;

  is_initialized = true;
  ESP_LOGI(TAG, "Screen Stream Service Initialized");
  return ESP_OK;
}

void ble_screen_server_deinit(void) {
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

  // RLE Compression of the partial map
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

    // Safety check to avoid overflow of compression_buffer (32KB)
    if (comp_idx > 32000) break;
  }

  if (run_count > 0 && comp_idx < 32765) {
    compression_buffer[comp_idx++] = run_count;
    compression_buffer[comp_idx++] = current_pixel & 0xFF;
    compression_buffer[comp_idx++] = (current_pixel >> 8) & 0xFF;
  }

  size_t total_len = comp_idx;
  size_t sent = 0;

  while (sent < total_len) {
    size_t chunk_len = total_len - sent;
    if (chunk_len > MAX_CHUNK_SIZE) chunk_len = MAX_CHUNK_SIZE;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(compression_buffer + sent, chunk_len);
    if (om) {
      ble_gattc_notify_custom(0, screen_chr_val_handle, om);
    }
    sent += chunk_len;
  }
}

bool ble_screen_server_is_active(void) {
  return is_initialized;
}
