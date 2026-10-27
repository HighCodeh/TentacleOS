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

#include "config_cache.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "mbedtls/platform_util.h"

#include "spi_protocol.h"
#include "storage_assets.h"

static const char *TAG = "CONFIG_CACHE";

#define CFG_STAGE_MAX 4096
#define CFG_NVS_NS    "cfgcache"

// Relative paths (from the assets mount) for each spi_cfg_id_t, in id order.
static const char *const s_cfg_paths[SPI_CFG_COUNT] = {
    [SPI_CFG_WIFI_AP] = "config/wifi/wifi_ap.conf",
    [SPI_CFG_BLE_ANNOUNCE] = "config/bluetooth/ble_announce.conf",
    [SPI_CFG_BLE_SPAM] = "config/bluetooth/beacon_list.conf",
    [SPI_CFG_CHAT] = "config/chat/chat.conf",
    [SPI_CFG_CHAT_ADDRS] = "config/chat/addresses.conf",
    [SPI_CFG_GROK] = "config/grok.json",
};

static uint8_t s_stage[CFG_STAGE_MAX + 1];
static uint32_t s_stage_size;
static uint32_t s_stage_hash;
static uint8_t s_stage_id = 0xFF;

// Pairing credentials must not remain in the shared staging buffer after the
// transaction ends or is rejected. Other configuration behavior is unchanged.
static void discard_grok_stage(void) {
  if (s_stage_id != SPI_CFG_GROK)
    return;
  mbedtls_platform_zeroize(s_stage, sizeof(s_stage));
  s_stage_id = 0xFF;
  s_stage_size = 0;
  s_stage_hash = 0;
}

static void hash_key(uint8_t id, char *out) {
  out[0] = 'h';
  out[1] = "0123456789abcdef"[(id >> 4) & 0xF];
  out[2] = "0123456789abcdef"[id & 0xF];
  out[3] = '\0';
}

uint32_t config_cache_get_hash(uint8_t id) {
  if (id >= SPI_CFG_COUNT)
    return 0;
  nvs_handle_t h;
  if (nvs_open(CFG_NVS_NS, NVS_READONLY, &h) != ESP_OK)
    return 0;
  char key[4];
  hash_key(id, key);
  uint32_t val = 0;
  if (nvs_get_u32(h, key, &val) != ESP_OK)
    val = 0;
  nvs_close(h);
  return val;
}

esp_err_t config_cache_begin(uint8_t id, uint32_t size, uint32_t hash) {
  if (id >= SPI_CFG_COUNT || size > CFG_STAGE_MAX) {
    discard_grok_stage();
    return ESP_ERR_INVALID_ARG;
  }
  discard_grok_stage();
  s_stage_id = id;
  s_stage_size = size;
  s_stage_hash = hash;
  memset(s_stage, 0, sizeof(s_stage));
  return ESP_OK;
}

esp_err_t config_cache_chunk(uint8_t id, uint32_t offset, const uint8_t *data, uint16_t len) {
  if (id != s_stage_id || data == NULL) {
    discard_grok_stage();
    return ESP_ERR_INVALID_STATE;
  }
  if (offset > s_stage_size || len > s_stage_size - offset || offset > CFG_STAGE_MAX ||
      len > CFG_STAGE_MAX - offset) {
    discard_grok_stage();
    return ESP_ERR_INVALID_SIZE;
  }
  memcpy(s_stage + offset, data, len);
  return ESP_OK;
}

esp_err_t config_cache_commit(uint8_t id) {
  if (id != s_stage_id || id >= SPI_CFG_COUNT) {
    discard_grok_stage();
    return ESP_ERR_INVALID_STATE;
  }

  s_stage[s_stage_size] = '\0'; // configs are text; storage_assets writes a string
  esp_err_t err = storage_assets_write_file(s_cfg_paths[id], (const char *)s_stage);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "cfg %u write %s failed: %s", id, s_cfg_paths[id], esp_err_to_name(err));
    discard_grok_stage();
    s_stage_id = 0xFF;
    return err;
  }

  nvs_handle_t h;
  if (nvs_open(CFG_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    char key[4];
    hash_key(id, key);
    nvs_set_u32(h, key, s_stage_hash);
    nvs_commit(h);
    nvs_close(h);
  }
  ESP_LOGI(TAG,
           "cfg %u updated (%s, %u bytes, hash 0x%08x)",
           id,
           s_cfg_paths[id],
           (unsigned)s_stage_size,
           (unsigned)s_stage_hash);
  discard_grok_stage();
  s_stage_id = 0xFF;
  return ESP_OK;
}
